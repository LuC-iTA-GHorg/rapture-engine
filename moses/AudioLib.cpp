// CAudioLib / CAudioSnd on top of platform Audio (mixer), Midi (MCI) and Movie (AVI). Package 2.
// See AudioLib.h for the port design (no threads; notifications queued and delivered by Service()).
#include "moses/AudioLib.h"
#include "moses/Ini.h"

#include "moses/FileIO.h"
#include "platform/Movie.h"

#include <algorithm>
#include <cstring>

namespace moses {

// moses.dll 0x100100ae: HWND ignored; the original loads dsound/msvfw32 and reads ini [audio] RemapDrums.
CAudioLib::CAudioLib(void*) { remap_drums_ = u8(IniGetInt("audio", "RemapDrums", 0)); } // state +0x3a (byte)

// moses.dll 0x100101a7
CAudioLib::~CAudioLib() {
    SoundStop(nullptr);
    pending_.clear();
    for (CAudioSnd* s : all_) delete s;
    all_.clear();
}

// moses.dll 0x10010995 (port form). The record parsing is platform LoadSound.
CAudioSnd* CAudioLib::SoundCreate(::Sound&& s) {
    if (s.kind == ::Sound::kNone) return nullptr;
    CAudioSnd* snd = new CAudioSnd();
    snd->param = s.param; // +08 priority / user
    snd->data = std::move(s);
    all_.push_back(snd);
    return snd;
}

// moses.dll 0x10010e84
void CAudioLib::SoundSetId(CAudioSnd* snd, u32 id) {
    if (snd) snd->id = id;
}

// moses.dll 0x10010dc2: stops and destroys, returns nullptr
CAudioSnd* CAudioLib::SoundDelete(CAudioSnd* snd) {
    if (!snd) return nullptr;
    stop_snd(snd, true);
    auto it = std::find(all_.begin(), all_.end(), snd);
    if (it != all_.end()) all_.erase(it);
    if (midiSnd_ == snd) midiSnd_ = nullptr;
    delete snd;
    return nullptr;
}

void CAudioLib::notify(CAudioSnd* snd, SNDNOTIFY n) {
    pending_.push_back({snd->id, n, snd->info});
}

// Stops one sound (any kind). `emit`: queue SND_ENDED if it was playing.
void CAudioLib::stop_snd(CAudioSnd* snd, bool emit) {
    bool was = snd->playing;
    for (ChannelId c : snd->chans) Audio::Get().Stop(c);
    snd->chans.clear();
    snd->channel = 0;
    snd->virtualPlay = false;
    if (snd->data.kind == ::Sound::kMidi && midiSnd_ == snd) {
        if (device_) midi_.Stop();
        midi_suspended_ = false;
        midiSnd_ = nullptr;
    }
    snd->playing = false;
    if (was && emit) notify(snd, SND_ENDED);
}

// Effective gain applied through the platform master volume: master * wave mixer, mute, activation.
void CAudioLib::apply_master() {
    float g = 1.0f;
    if (mute_[MIX_MASTER] || mute_[MIX_WAVE] || !active_ || paused_) g = 0.0f;
    else g = (vol_[MIX_MASTER] / 100.0f) * (vol_[MIX_WAVE] / 100.0f);
    Audio::Get().SetMasterVolume(g);
}

// Per-instance gain from PLAYSOUNDINFO (the mixer part is in apply_master).
float CAudioLib::wave_gain(CAudioSnd*, const PLAYSOUNDINFO* info) const {
    if (info && (info->flags & PSF_VOLUME)) return std::min(100u, info->volume) / 100.0f;
    return 1.0f;
}

// moses.dll 0x10010e1f (+ wave engine Play, MIDI engine Play 0x10011f17, AVI engine)
int CAudioLib::SoundPlay(CAudioSnd* snd, PLAYSOUNDINFO* info) {
    static PLAYSOUNDINFO defaults; // DAT_1001b580: size 0x18, rest 0
    if (!info) info = &defaults;
    else if (info->size != 0x18) return 0;
    if (!snd) return 0;

    switch (snd->data.kind) {
    case ::Sound::kWave: {
        if (!(info->flags & PSF_MIX)) { // without MIX: stop the running instances of this sound
            if (snd->playing) stop_snd(snd, true);
        }
        snd->info = *info;
        snd->loop = (info->flags & PSF_LOOP) != 0;
        bool dev = device_ && snd->data.pcm;
        if (dev) {
            Audio& a = Audio::Get();
            if (!a.IsOpen()) a.Open();
            apply_master();
            // TODO(pkg2): pan mapping is a hypothesis: (pan_x + pan) / pan_w clamped to -1..1.
            float pan = 0.0f;
            if ((info->flags & PSF_PAN) && pan_w_ > 0)
                pan = std::max(-1.0f, std::min(1.0f, float(pan_x_ + int(info->pan)) / float(pan_w_)));
            ChannelId c = a.Play(snd->data.pcm, snd->loop, wave_gain(snd, info), pan);
            if (c) {
                snd->chans.push_back(c);
                snd->channel = c;
            } else dev = false; // no device: behave as a virtual play
        }
        snd->virtualPlay = !dev && snd->chans.empty();
        snd->playing = true;
        notify(snd, SND_STARTED);
        return 1;
    }
    case ::Sound::kMidi: {
        // priority: a new MIDI with lower priority than the running one is rejected, otherwise it replaces it
        if (midiSnd_) {
            if (snd->param < midiSnd_->param) return 0;
            stop_snd(midiSnd_, true);
        }
        snd->info = *info;
        snd->loop = (info->flags & PSF_LOOP) != 0;
        midi_suspended_ = false;
        if (device_) {
            // The software sequencer applies RemapDrums, the channel mutes and the tempo itself, live (platform/MidiSeq.h).
            if (!midi_.Play(snd->data.midi, snd->loop, remap_drums_ != 0)) return 0;
        } else {
            snd->virtualPlay = true;
        }
        midiSnd_ = snd;
        snd->playing = true;
        notify(snd, SND_STARTED);
        return 1;
    }
    case ::Sound::kAvi: {
        snd->info = *info;
        snd->playing = true;
        notify(snd, SND_STARTED);
        // blocking full-window playback (port decision, see AudioLib.h); ENDED is queued afterwards
        if (device_ && host_) {
            Audio& a = Audio::Get();
            if (!a.IsOpen()) a.Open();
            PlayMovie(*host_, snd->data.aviName); // TODO: kQuit is not propagated
        }
        snd->virtualPlay = true; // ended at the next Service()
        return 1;
    }
    default: return 0;
    }
}

// moses.dll 0x10010ea0: nullptr stops everything
int CAudioLib::SoundStop(CAudioSnd* snd) {
    if (snd) {
        stop_snd(snd, true);
        return 1;
    }
    for (CAudioSnd* s : all_) stop_snd(s, true);
    if (device_) {
        Audio::Get().StopAll();
        midi_.Stop();
    }
    midiSnd_ = nullptr;
    return 1;
}

// moses.dll 0x10010f4c
void CAudioLib::StopAllWaves() {
    for (CAudioSnd* s : all_)
        if (s->data.kind == ::Sound::kWave) stop_snd(s, true);
}

// moses.dll 0x10010f76
void CAudioLib::StopMidi() {
    if (midiSnd_) stop_snd(midiSnd_, true);
}

// moses.dll 0x1001064f: mute note-ons of channel % 16 (live: also applies to the sequence that is playing)
void CAudioLib::MidiSetChannel(u32 channel, int mute) {
    u32 bit = 1u << (channel % 16);
    if (mute) midi_mute_mask_ |= bit;
    else midi_mute_mask_ &= ~bit;
    midi_.SetChannelMute(channel, mute != 0); // live, also while a sequence plays (CMidiPlayer::SetChannelMute 0x10012ff9)
}

// moses.dll 0x10010708: ms = delta*num/den (live: the delays read from now on)
void CAudioLib::MidiSetTempo(u32 num, u32 den) {
    tempo_num_ = num;
    tempo_den_ = den;
    midi_.SetTempo(num, den); // live: the delays read from now on are scaled (CMidiPlayer::SetTempo 0x100130ae)
}

// moses.dll 0x10010629: leave the loop at the end of the current run
void CAudioLib::MidiBreakLoops() {
    if (midiSnd_) midiSnd_->loop = false;
    midi_.BreakLoops(); // CMidiPlayer::BreakLoops: the sequence ends after the current run instead of restarting
}

// moses.dll 0x10010881
void CAudioLib::SetPanInfo(int x, int w) {
    if (w < 0) w = -w;
    pan_x_ = x;
    pan_w_ = w;
}

// moses.dll 0x10010857
void CAudioLib::Scroll(int dx) { SetPanInfo(pan_x_ + dx, pan_w_); }

// moses.dll 0x100108f6
void CAudioLib::SetSoundProc(SOUNDPROC proc, void* data) {
    proc_ = proc;
    proc_data_ = data;
}

// moses.dll 0x1001073f
void CAudioLib::Pause(int on) {
    bool b = on != 0;
    if (paused_ == b) return;
    paused_ = b;
    // TODO(pkg2): the mixer has no pause; paused audio is silenced (master gain 0) but keeps advancing.
    midi_gate();
    if (device_) apply_master();
    apply_cd();
}

// moses.dll 0x1001036d
void CAudioLib::Activate(int on) {
    bool b = on != 0;
    if (active_ == b) return;
    active_ = b;
    midi_gate();
    if (device_) apply_master();
    apply_cd();
}

// moses.dll 0x10011be9 (called by 0x10011df1 = Pause and 0x10011dc9 = Activate): the original does not pause the MIDI. Pause(1) or
// Activate(0) STOPS the player (all notes off, device closed, thread killed) without notification; when the sound is neither paused
// nor deactivated any more the current sound is Played again FROM THE BEGINNING (the engine Play, which also queues "started").
void CAudioLib::midi_gate() {
    if (!device_) return;
    bool suspend = paused_ || !active_;
    if (suspend) {
        if (midiSnd_ && !midi_suspended_ && midi_.IsPlaying()) {
            midi_.Stop();
            midi_suspended_ = true;
        }
    } else if (midi_suspended_) {
        midi_suspended_ = false;
        if (midiSnd_) {
            if (midi_.Play(midiSnd_->data.midi, midiSnd_->loop, remap_drums_ != 0)) notify(midiSnd_, SND_STARTED);
            else {
                midiSnd_->playing = false;
                CAudioSnd* s = midiSnd_;
                midiSnd_ = nullptr;
                notify(s, SND_ENDED);
            }
        }
    }
}

// moses.dll 0x100107fc
void CAudioLib::Reset() { SoundStop(nullptr); }

// moses.dll 0x1000ffc0 (CMixer::mute): set==0 reads
int CAudioLib::mute(EMIXERTYPE type, int set, int val) {
    if (type < 0 || type > 3) return 0;
    if (set) {
        mute_[type] = val ? 1 : 0;
        apply_cd();
        if (device_) apply_master();
    }
    return mute_[type];
}

// moses.dll 0x10010065 (CMixer::volume): 0..100
int CAudioLib::volume(EMIXERTYPE type, int set, int val) {
    if (type < 0 || type > 3) return 0;
    if (set) {
        vol_[type] = std::max(0, std::min(100, val));
        apply_cd();
        if (device_) apply_master();
    }
    return vol_[type];
}

// Port: main-loop tick. Detects finished sounds, restarts nothing by itself except MIDI loops, then delivers
// the queued notifications on the calling (main) thread.
void CAudioLib::Service() {
    if (cd_) cd_->Service();
    for (size_t i = 0; i < all_.size(); ++i) {
        CAudioSnd* s = all_[i];
        if (!s->playing) continue;
        if (s->data.kind == ::Sound::kWave) {
            if (s->virtualPlay) {
                s->virtualPlay = false;
                s->playing = false;
                notify(s, SND_ENDED);
                continue;
            }
            Audio& a = Audio::Get();
            s->chans.erase(std::remove_if(s->chans.begin(), s->chans.end(),
                                          [&](ChannelId c) { return !a.IsPlaying(c); }),
                           s->chans.end());
            if (s->chans.empty()) {
                s->channel = 0;
                s->playing = false;
                notify(s, SND_ENDED);
            }
        } else if (s->data.kind == ::Sound::kMidi) {
            if (s->virtualPlay) {
                if (!s->loop) { // looping virtual midi never ends
                    s->virtualPlay = false;
                    s->playing = false;
                    if (midiSnd_ == s) midiSnd_ = nullptr;
                    notify(s, SND_ENDED);
                }
            } else if (midiSnd_ == s) {
                midi_.Update();
                if (!midi_suspended_ && !s->loop && !midi_.IsPlaying()) {
                    s->playing = false;
                    midiSnd_ = nullptr;
                    notify(s, SND_ENDED);
                }
            }
        } else if (s->data.kind == ::Sound::kAvi) {
            if (s->virtualPlay) {
                s->virtualPlay = false;
                s->playing = false;
                notify(s, SND_ENDED);
            }
        }
    }
    // deliver (the callback may play/stop sounds and so queue more: loop on a moved-out copy)
    while (!pending_.empty()) {
        std::vector<Pending> batch;
        batch.swap(pending_);
        for (Pending& p : batch)
            if (proc_) proc_(proc_data_, p.id, p.n, &p.info);
    }
}


// ---------------------------------------------------------------------------------------------
// moses.dll 0x10010995 (original signature): the record is read from the file
CAudioSnd* CAudioLib::SoundCreate(CFileIO* io, u32 offset, u32 size) {
    if (!io || size == 0 || size > 0x10000000u) return nullptr;
    if (io->Seek(long(offset), 0) != offset) return nullptr;
    std::vector<u8> buf(size);
    if (!io->Read(buf.data(), size)) return nullptr;
    ::Sound s;
    if (!LoadSound(buf.data(), buf.size(), s)) return nullptr;
    return SoundCreate(std::move(s));
}

// ---------------------------------------------------------------------------------------------
// CD audio (CdAudio.h)
// Mixer gain of the CD channel: master * CD volume, 0 when muted / inactive / paused
void CAudioLib::apply_cd() {
    if (!cd_) return;
    float g = (mute_[MIX_MASTER] || mute_[MIX_CD] || !active_ || paused_) ? 0.0f : (vol_[MIX_MASTER] / 100.0f) * (vol_[MIX_CD] / 100.0f);
    cd_->SetGain(g);
}

// moses.dll 0x10010470 get_cd
CCdAudio& CAudioLib::Cd() {
    if (!cd_) {
        cd_.reset(new CCdAudio());
        cd_->SetDevice(device_);
        apply_cd();
    }
    return *cd_;
}

// moses.dll 0x100105b9
int CAudioLib::CdPlay(u8 from, u8 to) {
    CCdAudio& cd = Cd();
    cd.Play(from, to);
    return cd.IsSounding() ? 1 : 0;
}

// moses.dll 0x100105fe
void CAudioLib::CdStop() {
    if (cd_) cd_->Stop();
}

// moses.dll 0x10010581
int CAudioLib::CdIsPlaying() { return cd_ && cd_->IsOpen() ? 1 : 0; }

// moses.dll 0x1001054d
int CAudioLib::CdIsPaused() { return cd_ && cd_->IsPaused() ? 1 : 0; }

// moses.dll 0x100104cc
u8 CAudioLib::CdGetMaxTrack() { return Cd().MaxTrack(); }

// moses.dll 0x1001050c
int CAudioLib::CdGetPosition(CDPOS& pos) { return Cd().GetPosition(pos) ? 1 : 0; }

// moses.dll 0x10010427
u32 CAudioLib::CdGetLengths(u8 from, u8 to, u32* lengthsMs) { return Cd().GetLengths(from, to, lengthsMs); }

void CAudioLib::CdPause(int on) { Cd().Pause(on != 0); }

void CAudioLib::CdSetFolder(const char* dir) { Cd().SetFolder(dir ? dir : ""); }

} // namespace moses
