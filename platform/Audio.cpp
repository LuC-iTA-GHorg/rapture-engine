#include "platform/Audio.h"

#include "platform/Window.h"

#include <SDL.h>

#include <algorithm>

Audio& Audio::Get() {
    static Audio a;
    return a;
}

bool Audio::Open() {
    if (device_) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec want{}, have{};
    want.freq = 22050;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &Audio::Callback;
    want.userdata = this;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0); // SDL converts to the device format
    if (!dev) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    outRate_ = have.freq;
    device_ = dev;
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

void Audio::Close() {
    if (!device_) return;
    SDL_CloseAudioDevice(device_);
    device_ = 0;
    for (auto& c : ch_) {
        c.active = false;
        c.pcm.reset();
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void Audio::UpdateGain(Channel& c) {
    float l = std::min(1.0f, 1.0f - c.pan), r = std::min(1.0f, 1.0f + c.pan);
    c.gainL = int(std::clamp(c.volume * l * master_, 0.0f, 4.0f) * 256.0f);
    c.gainR = int(std::clamp(c.volume * r * master_, 0.0f, 4.0f) * 256.0f);
}

Audio::Channel* Audio::Find(ChannelId id) {
    if (id <= 0) return nullptr;
    unsigned slot = unsigned(id) & 0xff, gen = unsigned(id) >> 8;
    if (slot >= unsigned(kMaxChannels)) return nullptr;
    Channel& c = ch_[slot];
    return (c.active && c.gen == gen) ? &c : nullptr;
}

ChannelId Audio::Play(std::shared_ptr<const PcmBuffer> pcm, bool loop, float volume, float pan) {
    if (!device_ || !pcm || pcm->Frames() == 0) return 0;
    SDL_LockAudioDevice(device_);
    int slot = -1;
    for (int i = 0; i < kMaxChannels; ++i)
        if (!ch_[i].active) {
            slot = i;
            break;
        }
    ChannelId id = 0;
    if (slot >= 0) {
        Channel& c = ch_[slot];
        c.pcm = std::move(pcm);
        c.gen = (c.gen + 1) & 0x7fffff;
        if (c.gen == 0) c.gen = 1;
        c.loop = loop;
        c.volume = volume;
        c.pan = std::clamp(pan, -1.0f, 1.0f);
        c.pos = 0;
        c.step = uint64_t(double(c.pcm->rate) / double(outRate_) * 65536.0);
        UpdateGain(c);
        c.active = true;
        id = int((c.gen << 8) | unsigned(slot));
    }
    SDL_UnlockAudioDevice(device_);
    return id;
}

void Audio::Stop(ChannelId id) {
    if (!device_) return;
    SDL_LockAudioDevice(device_);
    if (Channel* c = Find(id)) c->active = false;
    SDL_UnlockAudioDevice(device_);
}

void Audio::StopAll() {
    if (!device_) return;
    SDL_LockAudioDevice(device_);
    for (auto& c : ch_) c.active = false;
    SDL_UnlockAudioDevice(device_);
}

void Audio::SetVolume(ChannelId id, float v) {
    if (!device_) return;
    SDL_LockAudioDevice(device_);
    if (Channel* c = Find(id)) {
        c->volume = v;
        UpdateGain(*c);
    }
    SDL_UnlockAudioDevice(device_);
}

void Audio::SetPan(ChannelId id, float p) {
    if (!device_) return;
    SDL_LockAudioDevice(device_);
    if (Channel* c = Find(id)) {
        c->pan = std::clamp(p, -1.0f, 1.0f);
        UpdateGain(*c);
    }
    SDL_UnlockAudioDevice(device_);
}

void Audio::SetMasterVolume(float v) {
    if (!device_) {
        master_ = v;
        return;
    }
    SDL_LockAudioDevice(device_);
    master_ = v;
    for (auto& c : ch_) UpdateGain(c);
    SDL_UnlockAudioDevice(device_);
}

bool Audio::IsPlaying(ChannelId id) {
    if (!device_) return false;
    SDL_LockAudioDevice(device_);
    bool r = Find(id) != nullptr;
    SDL_UnlockAudioDevice(device_);
    return r;
}

double Audio::PositionSeconds(ChannelId id) {
    if (!device_) return -1;
    SDL_LockAudioDevice(device_);
    double r = -1;
    if (Channel* c = Find(id)) r = double(c->pos) / 65536.0 / double(c->pcm->rate);
    SDL_UnlockAudioDevice(device_);
    return r;
}

void Audio::Callback(void* userdata, uint8_t* stream, int len) {
    static_cast<Audio*>(userdata)->Mix(reinterpret_cast<int16_t*>(stream), len / 4);
}

void Audio::Mix(int16_t* out, int frames) {
    static thread_local std::vector<int32_t> acc;
    acc.assign(size_t(frames) * 2, 0);
    const uint64_t mixScale = uint64_t(Window::TimeScale() + 0.5); // --fast N: sounds elapse N times faster (dummy driver timing)
    for (Channel& c : ch_) {
        if (!c.active) continue;
        const PcmBuffer& p = *c.pcm;
        const uint64_t total = uint64_t(p.Frames()) << 16;
        const int nch = p.channels;
        for (int f = 0; f < frames; ++f) {
            if (c.pos >= total) {
                if (c.loop) c.pos -= total;
                else {
                    c.active = false;
                    c.pcm.reset();
                    break;
                }
            }
            size_t i0 = size_t(c.pos >> 16), i1 = i0 + 1;
            int fr = int(c.pos & 0xffff);
            if (i1 >= p.Frames()) i1 = c.loop ? 0 : i0;
            int l0, r0, l1, r1;
            if (nch == 1) {
                l0 = r0 = p.samples[i0];
                l1 = r1 = p.samples[i1];
            } else {
                l0 = p.samples[i0 * 2];
                r0 = p.samples[i0 * 2 + 1];
                l1 = p.samples[i1 * 2];
                r1 = p.samples[i1 * 2 + 1];
            }
            int l = l0 + (((l1 - l0) * fr) >> 16), r = r0 + (((r1 - r0) * fr) >> 16);
            acc[size_t(f) * 2] += (l * c.gainL) >> 8;
            acc[size_t(f) * 2 + 1] += (r * c.gainR) >> 8;
            c.pos += c.step * mixScale;
        }
    }
    for (size_t i = 0; i < acc.size(); ++i) out[i] = int16_t(std::clamp(acc[i], -32768, 32767));
}
