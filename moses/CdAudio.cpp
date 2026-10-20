// CD audio over a folder of WAV track files (see CdAudio.h).  Original: moses.dll 0x10012857..0x10012bb2 (MCI cdaudio).
#include "moses/CdAudio.h"

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace moses {

namespace {
u32 DefaultClock() { return SDL_GetTicks(); }
CCdAudio::ClockFn g_clock = DefaultClock;

bool FileThere(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

std::string Pat(const char* fmt, int n) {
    char buf[64];
    std::snprintf(buf, sizeof buf, fmt, n);
    return buf;
}

// finds the file of `track` in `dir`
std::string FindTrack(const std::string& dir, int n) {
    static const char* const pats[] = {"track%02d.wav", "Track %02d.wav", "Track%02d.wav", "%02d.wav", "track%d.wav", "TRACK%02d.WAV"};
    for (const char* p : pats) {
        std::string path = dir.empty() ? Pat(p, n) : dir + "/" + Pat(p, n);
        if (FileThere(path)) return path;
    }
    return std::string();
}

// length in ms from the RIFF header only (no decoding); 0 when unknown
u32 WavLengthMs(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    unsigned char h[4096];
    size_t n = std::fread(h, 1, sizeof h, f);
    long total = 0;
    std::fseek(f, 0, SEEK_END);
    total = std::ftell(f);
    std::fclose(f);
    if (n < 44 || std::memcmp(h, "RIFF", 4) != 0) return 0;
    auto le32 = [&](size_t o) { return u32(h[o]) | u32(h[o + 1]) << 8 | u32(h[o + 2]) << 16 | u32(h[o + 3]) << 24; };
    u32 rate = 0, align = 0;
    size_t pos = 12;
    while (pos + 8 <= n) {
        u32 len = le32(pos + 4);
        if (std::memcmp(h + pos, "fmt ", 4) == 0 && pos + 24 <= n) {
            rate = le32(pos + 12);
            align = u32(h[pos + 8 + 12]) | u32(h[pos + 8 + 13]) << 8;
        } else if (std::memcmp(h + pos, "data", 4) == 0) {
            uint64_t avail = uint64_t(total) > pos + 8 ? uint64_t(total) - (pos + 8) : 0;
            uint64_t bytes = len > avail ? avail : len;
            if (!rate || !align) return 0;
            return u32(bytes / align * 1000ull / rate);
        }
        pos += 8 + size_t(len) + (len & 1);
    }
    return 0;
}
} // namespace

void CCdAudio::SetClock(ClockFn fn) { g_clock = fn ? fn : DefaultClock; }

CCdAudio::CCdAudio() {}
CCdAudio::~CCdAudio() { stop_channel(); }

void CCdAudio::SetFolder(const std::string& dir) { folder_ = dir; }

// look for the disc: first folder that has track 1; tracks continue while the next file exists
bool CCdAudio::scan() {
    std::vector<std::string> dirs;
    if (!folder_.empty()) dirs.push_back(folder_);
    if (const char* e = std::getenv("RAPTURE_CD_DIR")) dirs.push_back(e);
    dirs.push_back("cd");
    dirs.push_back("");
    tracks_.clear();
    for (const std::string& d : dirs) {
        if (FindTrack(d, 1).empty()) continue;
        for (int n = 1; n <= 99; ++n) {
            std::string p = FindTrack(d, n);
            if (p.empty()) break;
            Track t;
            t.path = p;
            tracks_.push_back(t);
        }
        break;
    }
    return !tracks_.empty();
}

bool CCdAudio::load(u8 track) {
    if (track < 1 || track > tracks_.size()) return false;
    Track& t = tracks_[size_t(track - 1)];
    if (t.pcm) return true;
    if (t.failed) return false;
    auto pcm = std::make_shared<PcmBuffer>();
    if (!LoadWavFile(t.path, *pcm)) {
        t.failed = true;
        return false;
    }
    t.pcm = pcm;
    return true;
}

u32 CCdAudio::TrackLengthMs(u8 track) {
    if (track < 1 || track > tracks_.size()) return 0;
    Track& t = tracks_[size_t(track - 1)];
    if (t.pcm) return u32(uint64_t(t.pcm->Frames()) * 1000ull / uint64_t(t.pcm->rate ? t.pcm->rate : 1));
    u32 ms = WavLengthMs(t.path);
    if (ms) return ms;
    return load(track) ? TrackLengthMs(track) : 0;
}

// FUN_10012857
bool CCdAudio::Open() {
    if (open_) return true;
    if (!scan()) return false;
    open_ = true;
    return true;
}

// FUN_10012a31 (opens the device if needed and closes it again when it was closed before)
u8 CCdAudio::MaxTrack() {
    bool was = open_;
    if (!Open()) return 0;
    u8 n = u8(tracks_.size());
    if (!was) Stop();
    return n;
}

// FUN_10012958
u32 CCdAudio::GetLengths(u8 from, u8 to, u32* out) {
    bool was = open_;
    if (!Open()) return 0;
    u32 count = 0;
    for (u8 t = from; t <= to; ++t) {
        u32 ms = TrackLengthMs(t);
        if (ms == 0 && (t < 1 || t > tracks_.size())) break; // MCI error: stop
        if (out) *out = ms;                                   // ORIGINAL QUIRK: always the same destination
        ++count;
        if (t == 255) break;
    }
    if (!was) Stop();
    return count;
}

double CCdAudio::elapsed() const {
    return startOffset_ + double(g_clock() - startMs_) / 1000.0;
}

// FUN_10012a71
bool CCdAudio::GetPosition(CDPOS& pos) {
    if (!open_) return false;
    double e = paused_ ? pausedAt_ : playing_ ? elapsed() : (cur_ ? double(TrackLengthMs(cur_)) / 1000.0 : 0.0);
    double len = cur_ ? double(TrackLengthMs(cur_)) / 1000.0 : 0.0;
    if (len > 0 && e > len) e = len;
    int whole = int(e);
    pos.track = cur_ ? cur_ : u8(1);
    pos.min = u8(whole / 60);
    pos.sec = u8(whole % 60);
    pos.frame = u8(int((e - whole) * 75.0 + 1e-6));
    return true;
}

void CCdAudio::stop_channel() {
    if (ch_) {
        Audio::Get().Stop(ch_);
        ch_ = 0;
    }
}

void CCdAudio::SetGain(float g) {
    gain_ = g;
    if (ch_) Audio::Get().SetVolume(ch_, g);
}

void CCdAudio::start_track(u8 track, double offsetSec) {
    stop_channel();
    cur_ = track;
    startMs_ = g_clock();
    startOffset_ = offsetSec;
    playing_ = false;
    if (!load(track)) return;
    playing_ = true;
    if (device_) {
        std::shared_ptr<const PcmBuffer> pcm = tracks_[size_t(track - 1)].pcm;
        if (offsetSec > 0.0) { // resume: a copy of the tail
            auto tail = std::make_shared<PcmBuffer>();
            tail->channels = pcm->channels;
            tail->rate = pcm->rate;
            size_t skip = size_t(offsetSec * pcm->rate) * size_t(pcm->channels);
            if (skip < pcm->samples.size()) tail->samples.assign(pcm->samples.begin() + long(skip), pcm->samples.end());
            pcm = tail;
        }
        Audio& a = Audio::Get();
        if (!a.IsOpen()) a.Open();
        ch_ = a.Play(pcm, false, gain_, 0.0f); // no device -> 0 -> the clock simulates the playback
    }
}

// FUN_10012bb2
void CCdAudio::Play(u8 from, u8 to) {
    if (!Open() || from < 1 || from > tracks_.size()) {
        Stop();
        return;
    }
    paused_ = false;
    if (to == 0 || to > tracks_.size()) to = u8(tracks_.size());
    from_ = from;
    to_ = to;
    start_track(from, 0.0);
}

// FUN_100127d0
void CCdAudio::Stop() {
    stop_channel();
    open_ = false;
    paused_ = false;
    playing_ = false;
    cur_ = 0;
}

// FUN_10012af7
void CCdAudio::Pause(bool on) {
    if (on == paused_) return;
    paused_ = on;
    if (on) {
        pausedAt_ = playing_ ? elapsed() : 0.0;
        stop_channel();
        playing_ = false;
    } else if (open_ && cur_) {
        start_track(cur_, pausedAt_); // MCI play from the saved position to the saved end
    }
}

void CCdAudio::Service() {
    if (!playing_) return;
    double len = double(TrackLengthMs(cur_)) / 1000.0;
    bool done;
    if (ch_) done = !Audio::Get().IsPlaying(ch_);
    else done = elapsed() >= len;
    if (!done) return;
    if (cur_ < to_) {
        u32 boundary = u32(double(startMs_) + (len - startOffset_) * 1000.0); // exact end of this track on the clock
        start_track(u8(cur_ + 1), 0.0);
        if (!ch_ && playing_) startMs_ = boundary;      // simulated playback: no gap between tracks
    } else {
        stop_channel();
        playing_ = false;                  // the device stays open (CdIsPlaying) until CdStop, like the MCI device
        startOffset_ = len;
        startMs_ = g_clock();
    }
}

} // namespace moses
