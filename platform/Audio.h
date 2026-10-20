// SDL2 software mixer with channels (replaces the original CAudioLib / DirectSound buffers).
//
// Mixes up to kMaxChannels PcmBuffers into a 22050 Hz stereo S16 device stream (SDL converts
// to the real device format). Sources of any rate/channel count (1 or 2) are resampled with
// linear interpolation. Channel ids are generation-tagged, so a stale id after the sound ended
// or was stopped is ignored safely. If no audio device exists, Open() returns false and every
// call is a no-op (Play returns 0).
#pragma once

#include "platform/Sound.h"

#include <cstdint>
#include <memory>

using ChannelId = int; // 0 = invalid / not playing

class Audio {
public:
    static constexpr int kMaxChannels = 32;
    static Audio& Get();

    bool Open();
    void Close();
    bool IsOpen() const { return device_ != 0; }

    // volume 0..1, pan -1 (left) .. +1 (right).
    ChannelId Play(std::shared_ptr<const PcmBuffer> pcm, bool loop = false, float volume = 1.0f, float pan = 0.0f);
    void Stop(ChannelId ch);
    void StopAll();
    void SetVolume(ChannelId ch, float volume);
    void SetPan(ChannelId ch, float pan);
    bool IsPlaying(ChannelId ch);
    // Playback position in seconds of the source (for A/V sync); -1 if not playing.
    double PositionSeconds(ChannelId ch);
    void SetMasterVolume(float volume);

private:
    Audio() = default;
    struct Channel {
        std::shared_ptr<const PcmBuffer> pcm;
        uint32_t gen = 0;
        bool active = false, loop = false;
        int gainL = 256, gainR = 256; // 8.8 fixed point (volume * pan)
        float volume = 1.0f, pan = 0.0f;
        uint64_t pos = 0;  // 16.16 source frame position
        uint64_t step = 0; // 16.16 source frames per output frame
    };
    static void Callback(void* userdata, uint8_t* stream, int len);
    void Mix(int16_t* out, int frames);
    Channel* Find(ChannelId ch);

    uint32_t device_ = 0;
    int outRate_ = 22050;
    float master_ = 1.0f;
    Channel ch_[kMaxChannels];
    void UpdateGain(Channel& c);
};
