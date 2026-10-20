// AVI playback: minimal RIFF/AVI demuxer + Microsoft Video 1 ("CRAM"/"MSVC") 16-bit decoder
// + uncompressed PCM audio track (DDLogo.avi / DILogo.avi: 640x480, 12 fps, 22050 Hz stereo s16).
// The demuxer is adapted from the mahjong port's Movie.cpp (Cinepak decoder replaced).
#pragma once

#include "platform/Gfx.h"
#include "platform/Sound.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Window;

class Movie {
public:
    // Parses the container and indexes frames/audio. No SDL needed (usable headless).
    bool Open(const std::string& path, std::string* err = nullptr);

    int Width() const { return width_; }
    int Height() const { return height_; }
    int FrameCount() const { return int(frames_.size()); }
    double Fps() const { return fps_; }
    const std::shared_ptr<PcmBuffer>& AudioTrack() const { return audio_; } // may be null

    // Frames must be decoded in order (inter-frame prediction). Rewind() restarts at frame 0.
    void Rewind();
    int NextFrameIndex() const { return next_; }
    // Decodes the next frame into Picture(); false when there are no more frames or on bad data.
    bool DecodeNext();
    // Current picture, 0xFFRRGGBB, top-down, Width() x Height().
    const Surface& Picture() const { return picture_; }

private:
    bool DecodeMsv1(const uint8_t* data, size_t size); // bottom-up 15-bit work buffer -> picture_
    void UpdatePicture();

    struct Chunk {
        uint64_t offset;
        uint32_t size;
    };
    std::vector<uint8_t> file_;
    std::vector<Chunk> frames_;
    std::shared_ptr<PcmBuffer> audio_;
    double fps_ = 12.0;
    int width_ = 0, height_ = 0;
    int next_ = 0;
    std::vector<uint16_t> work_; // previous frame, bottom-up rows (DIB order), RGB555
    Surface picture_;
};

enum class MovieResult { kFinished, kSkipped, kQuit, kError };

// Plays `path` full-window (blocking): video is drawn into the window framebuffer, audio goes
// through Audio::Get() if it is open. SPACE/ESC key-up or a mouse-button release skips it (kSkipped; the event
// is pushed back to the window queue); closing the window returns kQuit (also pushed back). Video is synchronised to the audio clock when available,
// otherwise to the system clock.
MovieResult PlayMovie(Window& window, const std::string& path);
