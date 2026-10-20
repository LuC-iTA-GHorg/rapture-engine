#include "platform/Movie.h"

#include "platform/Audio.h"
#include "platform/Dib.h"
#include "platform/Rrs.h"
#include "platform/Window.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
constexpr uint32_t Fourcc(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}

// Minimal RIFF/AVI demuxer: one video stream and an optional PCM audio stream
// (adapted from the mahjong port).
struct AviInfo {
    uint32_t usPerFrame = 0;
    int width = 0, height = 0, bitCount = 0;
    uint32_t videoHandler = 0, videoCompression = 0;
    int videoStream = -1, audioStream = -1;
    uint16_t audioFormatTag = 0, channels = 0, bits = 0;
    uint32_t sampleRate = 0;
    std::vector<std::pair<size_t, uint32_t>> videoChunks; // (offset, size); size 0 = repeat previous frame
    std::vector<uint8_t> audio;
};

class AviParser {
public:
    AviParser(const std::vector<uint8_t>& f, AviInfo& info) : f_(f), info_(info) {}

    bool Parse() {
        if (f_.size() < 12 || Le32(&f_[0]) != Fourcc("RIFF") || Le32(&f_[8]) != Fourcc("AVI ")) return false;
        Walk(12, std::min<size_t>(f_.size(), 8 + size_t(Le32(&f_[4]))));
        return info_.videoStream >= 0;
    }

private:
    void Walk(size_t pos, size_t end) {
        while (pos + 8 <= end) {
            uint32_t id = Le32(&f_[pos]);
            size_t size = Le32(&f_[pos + 4]);
            size_t body = pos + 8;
            if (body + size > end) size = end - body;
            if (id == Fourcc("LIST") && size >= 4) {
                uint32_t type = Le32(&f_[body]);
                if (type == Fourcc("strl")) ++streamIndex_;
                Walk(body + 4, body + size);
            } else {
                Chunk(id, body, size);
            }
            pos = body + size + (size & 1);
        }
    }

    void Chunk(uint32_t id, size_t body, size_t size) {
        const uint8_t* p = f_.data() + body;
        if (id == Fourcc("avih") && size >= 40) {
            info_.usPerFrame = Le32(p);
        } else if (id == Fourcc("strh") && size >= 8) {
            currentType_ = Le32(p);
            if (currentType_ == Fourcc("vids")) {
                info_.videoStream = streamIndex_;
                info_.videoHandler = Le32(p + 4);
                if (size >= 28 && Le32(p + 20) && Le32(p + 24))
                    info_.usPerFrame = uint32_t(uint64_t(Le32(p + 20)) * 1000000 / Le32(p + 24)); // scale/rate
            } else if (currentType_ == Fourcc("auds")) {
                info_.audioStream = streamIndex_;
            }
        } else if (id == Fourcc("strf")) {
            if (currentType_ == Fourcc("vids") && size >= 40) {
                info_.width = int(Le32(p + 4));
                info_.height = std::abs(int(Le32(p + 8)));
                info_.bitCount = Le16(p + 14);
                info_.videoCompression = Le32(p + 16);
            } else if (currentType_ == Fourcc("auds") && size >= 16) {
                info_.audioFormatTag = Le16(p);
                info_.channels = Le16(p + 2);
                info_.sampleRate = Le32(p + 4);
                info_.bits = Le16(p + 14);
            }
        } else {
            // Stream data chunks are named "NNxx" where NN is the stream number.
            int a = int(id & 0xFF) - '0', b = int((id >> 8) & 0xFF) - '0';
            if (a < 0 || a > 9 || b < 0 || b > 9) return;
            int stream = a * 10 + b;
            uint32_t kind = id >> 16;
            if (stream == info_.videoStream && (kind == ('d' | 'c' << 8) || kind == ('d' | 'b' << 8)))
                info_.videoChunks.emplace_back(body, uint32_t(size));
            else if (stream == info_.audioStream && kind == ('w' | 'b' << 8))
                info_.audio.insert(info_.audio.end(), p, p + size);
        }
    }

    const std::vector<uint8_t>& f_;
    AviInfo& info_;
    int streamIndex_ = -1;
    uint32_t currentType_ = 0;
};

} // namespace

bool Movie::Open(const std::string& path, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = path + ": " + m;
        return false;
    };
    frames_.clear();
    audio_.reset();
    if (!rrs::ReadWholeFile(path, file_)) return fail("cannot read file");
    AviInfo info;
    if (!AviParser(file_, info).Parse()) return fail("not a valid AVI");
    uint32_t h = info.videoHandler | 0x20202020, c = info.videoCompression | 0x20202020;
    bool msv1 = h == Fourcc("msvc") || h == Fourcc("cram") || c == Fourcc("cram") || c == Fourcc("msvc");
    if (!msv1) return fail("video codec is not Microsoft Video 1");
    if (info.bitCount != 16) return fail("only 16-bit Video 1 is supported");
    if (info.usPerFrame == 0 || info.width <= 0 || info.height <= 0) return fail("bad video header");
    width_ = info.width;
    height_ = info.height;
    fps_ = 1000000.0 / double(info.usPerFrame);
    for (auto& v : info.videoChunks) frames_.push_back({v.first, v.second});
    if (frames_.empty()) return fail("no video frames");

    if (info.audioStream >= 0 && info.audioFormatTag == 1 && !info.audio.empty() && info.channels >= 1 &&
        info.channels <= 2 && (info.bits == 16 || info.bits == 8)) {
        auto pcm = std::make_shared<PcmBuffer>();
        pcm->channels = info.channels;
        pcm->rate = int(info.sampleRate);
        if (info.bits == 16) {
            pcm->samples.resize(info.audio.size() / 2);
            for (size_t i = 0; i < pcm->samples.size(); ++i) pcm->samples[i] = int16_t(Le16(&info.audio[2 * i]));
        } else {
            pcm->samples.resize(info.audio.size());
            for (size_t i = 0; i < info.audio.size(); ++i) pcm->samples[i] = int16_t((int(info.audio[i]) - 128) << 8);
        }
        audio_ = pcm;
    }
    picture_.Create(width_, height_);
    Rewind();
    return true;
}

void Movie::Rewind() {
    next_ = 0;
    work_.assign(size_t(width_) * size_t(height_), 0);
    picture_.Clear(0xff000000u);
}

bool Movie::DecodeNext() {
    if (next_ >= int(frames_.size())) return false;
    const Chunk& c = frames_[size_t(next_++)];
    bool ok = true;
    if (c.size && c.offset + c.size <= file_.size()) ok = DecodeMsv1(&file_[c.offset], c.size);
    UpdatePicture(); // an empty chunk repeats the previous picture
    return ok;
}

void Movie::UpdatePicture() {
    for (int y = 0; y < height_; ++y) {
        const uint16_t* s = &work_[size_t(height_ - 1 - y) * width_]; // work_ is bottom-up
        uint32_t* d = picture_.Row(y);
        for (int x = 0; x < width_; ++x) d[x] = Rgb555ToArgb(s[x]);
    }
}

// Microsoft Video 1, 16-bit (RGB555). (Verified empirically on DDLogo/DILogo: mask only when b < 0x80.) 4x4 blocks, stream order = DIB order (bottom-up,
// left to right; within a block, bottom row first). Each block is described by two bytes
// a (low) b (high):
//   (b & 0xFC) == 0x84      : skip (b-0x84)<<8 | a blocks
//   b < 0x80                : 16 bit mask = b<<8|a, followed by color words c0,c1;
//                             if c0 bit15 set: 8-color mode (6 more words, two colors per
//                             2x2 quadrant), else 2-color mode. mask bit 1 -> c0, bit 0 -> c1
//   otherwise (b >= 0x80, not a skip code): fill the block with the 15-bit color b<<8|a
// Skipped blocks keep the previous frame's pixels. (Same scheme as ffmpeg's msvideo1.c.)
bool Movie::DecodeMsv1(const uint8_t* d, size_t n) {
    const int bw = (width_ + 3) / 4, bh = (height_ + 3) / 4;
    size_t p = 0;
    int skip = 0;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            if (skip > 0) {
                --skip;
                continue;
            }
            if (p + 2 > n) return true; // truncated chunk: remaining blocks unchanged
            int a = d[p++], b = d[p++];
            if (a == 0 && b == 0 && p == n) return true; // end marker
            if ((b & 0xfc) == 0x84) {
                skip = ((b - 0x84) << 8) + a - 1;
                continue;
            }
            uint16_t col[8];
            uint16_t px[16]; // 4x4, row 0 = bottom row of the block
            if (b < 0x80) {
                unsigned flags = unsigned(b) << 8 | unsigned(a);
                if (p + 4 > n) return false;
                col[0] = Le16(d + p);
                col[1] = Le16(d + p + 2);
                p += 4;
                if (col[0] & 0x8000) {
                    if (p + 12 > n) return false;
                    for (int i = 2; i < 8; ++i, p += 2) col[i] = Le16(d + p);
                    for (int y = 0; y < 4; ++y)
                        for (int x = 0; x < 4; ++x, flags >>= 1)
                            px[y * 4 + x] = col[((y & 2) << 1) + (x & 2) + ((flags & 1) ^ 1)];
                } else {
                    for (int i = 0; i < 16; ++i, flags >>= 1) px[i] = col[(flags & 1) ^ 1];
                }
            } else {
                uint16_t c = uint16_t(b << 8 | a);
                for (int i = 0; i < 16; ++i) px[i] = c;
            }
            for (int y = 0; y < 4; ++y) {
                int row = by * 4 + y;
                if (row >= height_) break;
                uint16_t* dst = &work_[size_t(row) * width_];
                for (int x = 0; x < 4; ++x) {
                    int col2 = bx * 4 + x;
                    if (col2 < width_) dst[col2] = px[y * 4 + x] & 0x7fff;
                }
            }
        }
    }
    return true;
}

MovieResult PlayMovie(Window& window, const std::string& path) {
    Movie movie;
    if (!movie.Open(path)) return MovieResult::kError;
    Audio& audio = Audio::Get();
    audio.Open(); // no-op if already open; silent playback if no device

    ChannelId ch = movie.AudioTrack() ? audio.Play(movie.AudioTrack(), false, 1.0f, 0.0f) : 0;
    const uint32_t t0 = Window::Ticks();
    FrameTimer timer(60.0);
    MovieResult result = MovieResult::kFinished;
    bool shown = false;
    const double duration = movie.FrameCount() / movie.Fps();

    for (;;) {
        InputEvent ev;
        bool done = false;
        while (window.PollEvent(ev)) {
            // Skip = what CTitleScene::KeyListener (Shoot.exe 0x408f91) reacts to: SPACE/ESC key-up or a mouse button release.
            // The event is handed back to the window queue so the scene's listeners see it too (the one-skip-skips-both-logos quirk).
            if (ev.type == InputEvent::kQuit) {
                result = MovieResult::kQuit;
                window.PushEventFront(ev);
                done = true;
            } else if ((ev.type == InputEvent::kKeyUp && (ev.key == 32 || ev.key == 27)) ||
                       (ev.type == InputEvent::kMouseUp && (ev.button == 1 || ev.button == 3))) {
                result = MovieResult::kSkipped;
                window.PushEventFront(ev);
                done = true;
                break; // the rest of the queue stays for the scene
            }
        }
        if (done) break;

        double elapsed = (Window::Ticks() - t0) / 1000.0;
        if (ch && audio.IsPlaying(ch)) elapsed = audio.PositionSeconds(ch);
        int target = std::min(movie.FrameCount(), int(elapsed * movie.Fps()) + 1); // frames due so far
        bool changed = false;
        while (movie.NextFrameIndex() < target) {
            movie.DecodeNext();
            changed = true;
        }
        if (changed || !shown) {
            CopySurface(window.Framebuffer(), movie.Picture(), 0, 0);
            window.Present();
            shown = true;
        }
        if (movie.NextFrameIndex() >= movie.FrameCount() && elapsed >= duration) break;
        timer.Wait();
    }
    if (ch) audio.Stop(ch);
    return result;
}
