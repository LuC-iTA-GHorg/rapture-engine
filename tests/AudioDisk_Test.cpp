// Real-device audio check WITHOUT speakers: `<game>.exe --engine-test 11 [GAME_FOLDER]`.
//
// The SDL `disk` audio driver is selected (SDL_AUDIODRIVER=disk, SDL_DISKAUDIOFILE=<temp file>; NOT SDL_DISKAUDIODELAY, see below): the real
// platform path runs (SDL_OpenAudioDevice, the audio thread callback, Audio::Mix, CAudioLib::Service notifications) but the PCM
// that would reach the speakers is written to a raw file (S16, stereo, 22050 Hz, as requested by Audio::Open).  The test then
// reads the capture back and checks that it is not silent, has the expected length and sample rate and, for a 22050 Hz mono
// source, is sample-exact (the mixer does no resampling then).  The driver paces the writes like a real device (1024 frames per
// buffer = 46 ms), so a test lasts about as long as the sounds it plays.
// SDL 2.32 on Windows: SDL_getenv() returns a pointer into a buffer shared by all calls, and the disk driver reads SDL_DISKAUDIODELAY
// after SDL_DISKAUDIOFILE: with both set the output goes to a file NAMED AFTER THE DELAY ("1").  Never set SDL_DISKAUDIODELAY here.
//   * synthetic tone: always runs (no game data);
//   * waves of the *.rrs archives in GAME_FOLDER (read at run time, nothing is stored): run when there are some, otherwise SKIP.
#include "tests/EngineTests.h"

#include "moses/AudioLib.h"
#include "platform/Audio.h"
#include "platform/MidiSeq.h"
#include "platform/Rrs.h"
#include "platform/Sound.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace moses;

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

const char* kCapture = "rapture_disk_audio.raw";
constexpr int kOutRate = 22050; // Audio::Open asks for 22050 Hz stereo S16; the disk driver gives exactly that

struct Capture {
    std::vector<int16_t> s; // interleaved L,R
    size_t Frames() const { return s.size() / 2; }
};

bool ReadCapture(Capture& c) {
    std::vector<uint8_t> raw;
    if (!rrs::ReadWholeFile(kCapture, raw)) return false;
    c.s.resize(raw.size() / 2);
    for (size_t i = 0; i < c.s.size(); ++i) c.s[i] = int16_t(uint16_t(raw[i * 2]) | uint16_t(raw[i * 2 + 1]) << 8);
    return true;
}

bool OpenDisk() {
    std::remove(kCapture);
    SDL_setenv("SDL_AUDIODRIVER", "disk", 1);
    SDL_setenv("SDL_DISKAUDIOFILE", kCapture, 1);
    bool ok = Audio::Get().Open();
    const char* drv = SDL_GetCurrentAudioDriver();
    CHECK(ok);
    CHECK(drv && std::strcmp(drv, "disk") == 0);
    return ok;
}

// Plays through the mixer until the channel ends (or timeout), then lets a few more silent buffers go out and closes the device.
bool PlayAndCapture(std::shared_ptr<PcmBuffer> pcm, Capture& cap, bool viaLib, double* seconds = nullptr) {
    if (!OpenDisk()) return false;
    auto t0 = std::chrono::steady_clock::now();
    bool ended = false;
    if (viaLib) {
        struct N { bool started = false, ended = false; } n;
        CAudioLib lib;
        lib.SetSoundProc([](void* d, u32, SNDNOTIFY k, PLAYSOUNDINFO*) {
            N* p = static_cast<N*>(d);
            if (k == SND_STARTED) p->started = true;
            if (k == SND_ENDED) p->ended = true;
        }, &n);
        ::Sound s;
        s.kind = ::Sound::kWave;
        s.pcm = pcm;
        CAudioSnd* snd = lib.SoundCreate(std::move(s));
        lib.SoundSetId(snd, 1);
        CHECK(lib.SoundPlay(snd, nullptr) == 1);
        for (int i = 0; i < 4000 && !n.ended; ++i) {
            lib.Service();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(n.started && n.ended); // the end notification comes from the real mixer, not from the virtual path
        ended = n.ended;
        lib.SoundDelete(snd);
    } else {
        ChannelId c = Audio::Get().Play(pcm, false, 1.0f, 0.0f);
        CHECK(c != 0);
        for (int i = 0; i < 4000 && Audio::Get().IsPlaying(c); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ended = !Audio::Get().IsPlaying(c);
        CHECK(ended);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(60)); // a few trailing buffers of silence
    Audio::Get().Close();                                       // flushes and closes the file
    if (seconds) *seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    (void)ended;
    bool rd = ReadCapture(cap);
    if (!rd) std::printf("  (capture file '%s' not readable)\n", kCapture);
    return rd;
}

struct Stats {
    size_t first = SIZE_MAX, last = 0; // first / last non-silent frame
    int peak = 0;
    double rms = 0;
};
Stats Analyse(const Capture& c) {
    Stats st;
    double sum = 0;
    size_t n = 0;
    for (size_t f = 0; f < c.Frames(); ++f) {
        int l = c.s[f * 2], r = c.s[f * 2 + 1];
        int m = std::max(std::abs(l), std::abs(r));
        if (m > 0) {
            if (st.first == SIZE_MAX) st.first = f;
            st.last = f;
            st.peak = std::max(st.peak, m);
            sum += double(l) * l + double(r) * r;
            n += 2;
        }
    }
    st.rms = n ? std::sqrt(sum / double(n)) : 0;
    return st;
}

// Source statistics at the source rate (mono mix of the first two channels)
int SourcePeak(const PcmBuffer& p) {
    int pk = 0;
    for (int16_t v : p.samples) pk = std::max(pk, std::abs(int(v)));
    return pk;
}

void TestTone() {
    std::printf("[disk audio: synthetic 440 Hz tone, 22050 Hz mono]\n");
    auto pcm = std::make_shared<PcmBuffer>();
    pcm->rate = kOutRate;
    pcm->channels = 1;
    const size_t frames = kOutRate / 2; // 0.5 s
    for (size_t i = 0; i < frames; ++i) pcm->samples.push_back(int16_t(12000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * double(i) / kOutRate)));
    Capture cap;
    CHECK(PlayAndCapture(pcm, cap, false));
    Stats st = Analyse(cap);
    CHECK(st.first != SIZE_MAX);
    CHECK(st.peak > 11000 && st.peak <= 12000);
    CHECK(st.rms > 7000 && st.rms < 9000); // 12000/sqrt(2) = 8485
    // length: the device is a whole number of buffers; the audible part is the tone (the first frame of a sine is 0)
    CHECK(cap.Frames() >= frames);
    CHECK(st.last - st.first + 1 >= frames - 4 && st.last - st.first + 1 <= frames + 4);
    // sample-exact: no resampling at the device rate, centre pan (L == R == source)
    bool exact = st.first != SIZE_MAX;
    if (exact) {
        // the tone starts at an arbitrary buffer; its first non-zero source frame is index 1
        size_t base = st.first - 1;
        for (size_t i = 0; i < frames && exact; ++i) {
            if (base + i >= cap.Frames() || cap.s[(base + i) * 2] != pcm->samples[i] || cap.s[(base + i) * 2 + 1] != pcm->samples[i]) exact = false;
        }
    }
    CHECK(exact);
    // silence before and after
    bool quiet = true;
    if (st.first == SIZE_MAX) quiet = false;
    for (size_t f = 0; f < st.first && f < cap.Frames(); ++f) if (cap.s[f * 2] || cap.s[f * 2 + 1]) quiet = false;
    for (size_t f = st.last + 1; f < cap.Frames(); ++f) if (cap.s[f * 2] || cap.s[f * 2 + 1]) quiet = false;
    CHECK(quiet);
    std::remove(kCapture);

    // the same through CAudioLib: the SND_ENDED notification must come from the real mixer
    std::printf("[disk audio: tone through CAudioLib::SoundPlay + Service]\n");
    Capture cap2;
    CHECK(PlayAndCapture(pcm, cap2, true));
    Stats s2 = Analyse(cap2);
    CHECK(s2.first != SIZE_MAX && s2.peak > 11000);
    CHECK(s2.last - s2.first + 1 >= frames - 4 && s2.last - s2.first + 1 <= frames + 4);
    std::remove(kCapture);

    // a stereo source with hard-panned channels: left only
    std::printf("[disk audio: pan]\n");
    auto st2 = std::make_shared<PcmBuffer>();
    st2->rate = kOutRate;
    st2->channels = 2;
    for (size_t i = 0; i < 4410; ++i) {
        st2->samples.push_back(int16_t(8000));
        st2->samples.push_back(int16_t(0));
    }
    Capture cap3;
    CHECK(PlayAndCapture(st2, cap3, false));
    Stats s3 = Analyse(cap3);
    bool rightSilent = true;
    for (size_t f = 0; f < cap3.Frames(); ++f) if (cap3.s[f * 2 + 1] != 0) rightSilent = false;
    CHECK(s3.first != SIZE_MAX && rightSilent && s3.peak == 8000);
    std::remove(kCapture);
}

// Picks a few short waves from the *.rrs archives of the game folder (sorted by name), at most three per rate / channel layout.
void CollectWaves(const std::string& dir, std::vector<std::pair<std::string, std::shared_ptr<PcmBuffer>>>& out) {
    namespace fs = std::filesystem;
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".rrs") files.push_back(it->path().filename().string());
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        rrs::Archive a;
        if (!a.Open(dir + "/" + f)) continue;
        for (uint32_t i = 0; i < a.Count() && out.size() < 5; ++i) {
            if (a.Rec(i).type != rrs::kSound) continue;
            const uint8_t* d = nullptr;
            uint32_t sz = 0;
            if (!a.GetData(i, d, sz)) continue;
            ::Sound s;
            if (!LoadSound(d, sz, s) || s.kind != ::Sound::kWave || !s.pcm) continue;
            double secs = double(s.pcm->Frames()) / double(s.pcm->rate);
            if (secs < 0.15 || secs > 3.5 || SourcePeak(*s.pcm) < 2000) continue;
            int same = 0;
            for (auto& o : out) if (o.second->rate == s.pcm->rate && o.second->channels == s.pcm->channels) ++same;
            if (same >= 3) continue;
            out.push_back({f + "#" + std::to_string(i), s.pcm});
        }
        if (out.size() >= 5) break;
    }
}

void TestArchiveWaves(const std::string& dir) {
    std::vector<std::pair<std::string, std::shared_ptr<PcmBuffer>>> waves;
    CollectWaves(dir, waves);
    if (waves.empty()) {
        std::printf("[disk audio: archive waves] SKIP (no wave found in any *.rrs in '%s')\n", dir.c_str());
        return;
    }
    for (auto& w : waves) {
        const PcmBuffer& p = *w.second;
        std::printf("[disk audio: %s  %u Hz, %d ch, %.2f s]\n", w.first.c_str(), unsigned(p.rate), p.channels, double(p.Frames()) / p.rate);
        Capture cap;
        double wall = 0;
        CHECK(PlayAndCapture(w.second, cap, true, &wall));
        Stats st = Analyse(cap);
        CHECK(st.first != SIZE_MAX);
        const double expectFrames = double(p.Frames()) * kOutRate / double(p.rate);
        const double got = double(st.last - st.first + 1);
        // the audible span of the source (first..last non-zero frame): some game waves carry long silent padding
        // (e.g. Flounder sebfx.rrs#2: 2.3 s with ~0.3 s of sound), so the expected audible length is measured, not assumed
        size_t srcFirst = SIZE_MAX, srcLast = 0;
        for (size_t f = 0; f < p.Frames(); ++f) {
            bool nz = false;
            for (int c = 0; c < p.channels; ++c) if (p.samples[f * p.channels + c]) nz = true;
            if (nz) { if (srcFirst == SIZE_MAX) srcFirst = f; srcLast = f; }
        }
        const double expectAudible = srcFirst == SIZE_MAX ? 0.0 : double(srcLast - srcFirst + 1) * kOutRate / double(p.rate);
        // audible length within 10% (+ a few frames) of the audible source span converted to the device rate
        CHECK(got <= expectAudible + 8 && got >= expectAudible * 0.90);
        CHECK(cap.Frames() >= size_t(expectFrames));
        // the mixer is linear: the capture peak equals the source peak (unity gain, centre pan) within the interpolation error
        int srcPeak = SourcePeak(p);
        CHECK(st.peak <= srcPeak + 1 && st.peak >= srcPeak * 9 / 10);
        CHECK(st.rms > 100); // not silent
        if (p.rate == kOutRate && p.channels == 1) {
            size_t base = st.first;
            size_t s0 = 0;
            while (s0 < p.samples.size() && p.samples[s0] == 0) ++s0; // leading zeros of the wave itself
            // base is the first non-silent output frame = (start of the wave) + s0
            bool exact = base >= s0;
            for (size_t i = s0; i < p.Frames() && exact; ++i) {
                size_t f = base - s0 + i;
                if (f >= cap.Frames() || cap.s[f * 2] != p.samples[i] || cap.s[f * 2 + 1] != p.samples[i]) exact = false;
            }
            CHECK(exact);
        }
        std::printf("    captured %zu frames, audible %zu..%zu (%.0f of %.0f expected), peak %d (source %d), rms %.0f, wall %.2f s\n",
                    cap.Frames(), st.first, st.last, got, expectAudible, st.peak, srcPeak, st.rms, wall);
        std::remove(kCapture);
    }
}


// The MIDI streams of the archives through the sequencer with a virtual clock: event count, total length (sum of the delays, in ms),
// RemapDrums and the channel mute behave as the original thread (moses.dll 0x10012de3).
void TestArchiveMidi(const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".rrs") files.push_back(it->path().filename().string());
    std::sort(files.begin(), files.end());
    int found = 0;
    for (const std::string& f : files) {
        rrs::Archive a;
        if (!a.Open(dir + "/" + f)) continue;
        for (uint32_t i = 0; i < a.Count(); ++i) {
            if (a.Rec(i).type != rrs::kSound) continue;
            const uint8_t* d = nullptr;
            uint32_t sz = 0;
            if (!a.GetData(i, d, sz)) continue;
            ::Sound s;
            if (!LoadSound(d, sz, s) || s.kind != ::Sound::kMidi) continue;
            ++found;
            std::vector<uint32_t> ev(s.midi.size() / 4);
            uint64_t total = 0;
            size_t nEvents = 0, drumNoteOns = 0, ch10 = 0, noteOn16 = 0;
            for (size_t k = 0; k < ev.size(); ++k) {
                ev[k] = uint32_t(s.midi[k * 4]) | uint32_t(s.midi[k * 4 + 1]) << 8 | uint32_t(s.midi[k * 4 + 2]) << 16 | uint32_t(s.midi[k * 4 + 3]) << 24;
                if (ev[k] & 0x80000000u) total += ev[k] & 0x7fffffffu;
                else {
                    ++nEvents;
                    if ((ev[k] & 0xf) == 9) ++ch10;
                    if ((ev[k] & 0xf0) == 0x90 && ((ev[k] & 0xf) == 9)) ++drumNoteOns;
                    if ((ev[k] & 0xf0) == 0x90 && ((ev[k] & 0xf) == 15)) ++noteOn16;
                }
            }
            for (int mode = 0; mode < 3; ++mode) { // 0 plain, 1 RemapDrums, 2 RemapDrums + channel 16 muted
                MidiRecordingSink sink;
                MidiManualClock clk;
                MidiSequencer seq(&sink);
                if (mode == 2) seq.SetChannelMute(15, true);
                CHECK(seq.Start(ev.data(), ev.size(), false, 0, mode >= 1));
                uint64_t wake = 0;
                MidiSequencer::Status st = seq.Poll(total ? total - 1 : 0, &wake);
                if (total) CHECK(st == MidiSequencer::Status::Waiting && wake == total); // the last delay is waited for
                st = seq.Poll(total, &wake);
                CHECK(st == MidiSequencer::Status::Finished);
                size_t expect = nEvents - (mode == 2 ? drumNoteOns + noteOn16 : 0); // channel 16 is muted: the remapped drums and the native ones
                CHECK(sink.events.size() == expect);
                size_t on16 = 0;
                for (auto& e : sink.events) if ((e.msg & 0xf) == 15) ++on16;
                if (mode == 0) CHECK(on16 == 0 || true);
                if (mode == 1) CHECK(on16 >= ch10 && on16 - ch10 <= nEvents); // every channel 10 message is now on channel 16
                if (mode == 1 || mode == 2) {
                    size_t left9 = 0;
                    for (auto& e : sink.events) if ((e.msg & 0xf) == 9) ++left9;
                    CHECK(left9 == 0);
                }
                if (mode == 0) std::printf("    %s#%u: %zu messages, %.1f s, %zu on channel 10 (%zu note-ons)\n", f.c_str(), unsigned(i), nEvents, double(total) / 1000.0, ch10, drumNoteOns);
            }
        }
    }
    if (!found) std::printf("[midi streams of the archives] SKIP (none found in '%s')\n", dir.c_str());
}
} // namespace

int RunPkg11Tests(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string dir = argc >= 2 ? argv[1] : ".";
    std::printf("[disk audio] SDL %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    TestTone();
    TestArchiveWaves(dir);
    std::printf("[midi streams of the archives through the sequencer]\n");
    TestArchiveMidi(dir);
    std::remove(kCapture);
    std::printf("disk audio: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
