// CAudioLib: sound engine of moses.dll (moses.md sec. 9) on top of platform/Audio (mixer),
// platform/Midi (MCI sequencer on Windows), platform/Movie (AVI).
//
// PORT DESIGN
//  * No threads: the platform mixer has its own audio callback; EVERYTHING else (start/end
//    notifications, MIDI loop restart, AVI) happens in CAudioLib::Service(), called once per
//    main-loop iteration. Notifications therefore fire on the main thread, never inside the
//    audio callback.
//  * Sound objects are created from an already parsed platform `::Sound` (rrs type 5, see
//    platform/Sound.h) instead of from a file offset: SoundCreate(CFileIO*, off, size) 0x10010995
//    becomes SoundCreate(::Sound&&).
//  * kind 3 (AVI): the original plays through MCIWnd + a child window. The port plays the movie
//    with platform PlayMovie() (blocking, full window) from inside Service()/SoundPlay when a host
//    window was installed with SetHostWindow(); the "end" notification is raised when it returns.
//    TODO(package 2/3): decide if blocking inside SoundPlay is acceptable for the Title scene.
#pragma once

#include "moses/CdAudio.h"
#include "moses/Types.h"
#include "platform/Audio.h"
#include "platform/Midi.h"
#include "platform/Sound.h"

#include <memory>
#include <vector>

class Window; // platform window (global namespace)

namespace moses {

// moses.md sec. 9
struct PLAYSOUNDINFO {
    u32 size = 0x18;   // must be 0x18 (otherwise SoundPlay returns 0)
    u32 flags = 0;     // PSF_*
    u32 volume = 0;    // 0..100                  [PSF_VOLUME]
    u32 pan = 0;       // position; effective pan = pan_x + value [PSF_PAN]
    u32 user0 = 0, user1 = 0; // returned in the notifications
};
enum PlaySoundFlags : u32 {
    PSF_MIX = 0x01,    // do NOT stop the instances already playing (mix); without it Play first stops the sounds with the same id
    PSF_LOOP = 0x04,   // wave: repeat the buffer; midi: repeat the stream
    PSF_VOLUME = 0x08,
    PSF_PAN = 0x10,
};

enum SNDNOTIFY : int { SND_STARTED = 1, SND_ENDED = 2 };
// Mixer channels for volume()/mute(): EMIXERTYPE
enum EMIXERTYPE : int { MIX_MASTER = 0, MIX_WAVE = 1, MIX_MIDI = 2, MIX_CD = 3 };

// notification callback (SetSoundProc). Original __stdcall.
using SOUNDPROC = void (*)(void* data, u32 sndId, SNDNOTIFY n, PLAYSOUNDINFO* info);

// A loaded sound (CAudioSnd in the original: +8 param = priority/user, +0xc id).
struct CAudioSnd {
    ::Sound data;                 // kind, pcm / midi bytes / aviName
    u32 id = 0;                   // +0c  resource id (SoundSetId) - what the notifications report
    u32 param = 0;                // +08  = Sound::param (MIDI priority)
    // runtime
    ChannelId channel = 0;        // wave channel in platform Audio (0 = not playing)
    bool playing = false;
    bool loop = false;
    PLAYSOUNDINFO info;           // copy of the last Play info (for the notifications)
    // port additions (PSF_MIX allows several simultaneous instances of one wave)
    std::vector<ChannelId> chans; // live mixer channels of this sound
    bool virtualPlay = false;     // started without a device (headless): ends at the next Service()
};

class CAudioLib {
public:
    explicit CAudioLib(void* hwnd = nullptr);   // moses.dll 0x100100ae (HWND ignored; opens platform Audio lazily)
    ~CAudioLib();                               // moses.dll 0x100101a7

    // ---- sounds ------------------------------------------------------------------------------
    CAudioSnd* SoundCreate(::Sound&& s);                        // moses.dll 0x10010995 (port form, see header)
    // moses.dll 0x10010995 with the original signature: reads the type-5 record (`size` bytes at `offset`) from the file.
    CAudioSnd* SoundCreate(class CFileIO* io, u32 offset, u32 size);
    void SoundSetId(CAudioSnd* snd, u32 id);                    // moses.dll 0x10010e84
    CAudioSnd* SoundDelete(CAudioSnd* snd);                     // moses.dll 0x10010dc2  stops and frees, returns nullptr
    int SoundPlay(CAudioSnd* snd, PLAYSOUNDINFO* info);         // moses.dll 0x10010e1f  info==NULL -> defaults; info->size != 0x18 -> 0
    int SoundStop(CAudioSnd* snd);                              // moses.dll 0x10010ea0  snd==NULL stops everything (wave, midi, avi)
    void StopAllWaves();                                        // moses.dll 0x10010f4c
    void StopMidi();                                            // moses.dll 0x10010f76

    // ---- MIDI ----------------------------------------------------------------------------------
    void MidiSetChannel(u32 channel, int mute);                 // moses.dll 0x1001064f  mute note-ons of channel%16
    void MidiSetTempo(u32 num, u32 den);                        // moses.dll 0x10010708  ms = delta*num/den (num==den: off)
    void MidiBreakLoops();                                      // moses.dll 0x10010629

    // ---- global state --------------------------------------------------------------------------
    void SetPanInfo(int x, int w);                              // moses.dll 0x10010881
    void Scroll(int dx);                                        // moses.dll 0x10010857  pan_x += dx
    void SetSoundProc(SOUNDPROC proc, void* data);              // moses.dll 0x100108f6
    void Pause(int on);                                         // moses.dll 0x1001073f
    void Activate(int on);                                      // moses.dll 0x1001036d
    void Reset();                                               // moses.dll 0x100107fc
    // System mixer (volume/mute). Port: internal state applied as platform master/channel gain.
    // type: EMIXERTYPE; set==0 reads, set!=0 writes; volume 0..100 (clamped), mute 0/1. Returns the value.
    int mute(EMIXERTYPE type, int set, int val);                // moses.dll 0x1000ffc0
    int volume(EMIXERTYPE type, int set, int val);              // moses.dll 0x10010065

    // ---- CD audio (CdAudio.h: the "disc" is a folder of track01.wav ... files; no files = empty drive) ----------
    // The games never use it (Aaron only calls CdStop); complete for the other users of the engine.
    int CdPlay(u8 from, u8 to);                                        // moses.dll 0x100105b9  tracks from..to (to 0 = last); 1 if started
    void CdStop();                                                     // moses.dll 0x100105fe
    int CdIsPlaying();                                                 // moses.dll 0x10010581  device open (stays set after the last track until CdStop)
    int CdIsPaused();                                                  // moses.dll 0x1001054d
    u8 CdGetMaxTrack();                                                // moses.dll 0x100104cc  0 = no disc
    int CdGetPosition(CDPOS& pos);                                     // moses.dll 0x1001050c  TMSF inside the current track; 0 when stopped
    u32 CdGetLengths(u8 from, u8 to, u32* lengthsMs);                  // moses.dll 0x10010427  see CCdAudio::GetLengths (last length only)
    void CdPause(int on);                                              // port: the original reaches FUN_10012af7 only through its own UI
    void CdSetFolder(const char* dir);                                 // port: where the track files are
    CCdAudio& Cd();                                                    // moses.dll 0x10010470 get_cd (created on first use)

    // ---- port additions ------------------------------------------------------------------------
    void Service();                                  // call every main-loop iteration (notifications, midi loop, avi)
    void SetHostWindow(::Window* w) { host_ = w; }   // needed for kind-3 (AVI) sounds
    // Headless switch (tests): false = never open the audio device nor the MIDI sequencer; plays become
    // "virtual" (STARTED/ENDED notifications are still delivered by Service()). Default true.
    void EnableDevice(bool on) { device_ = on; if (cd_) cd_->SetDevice(on); }
    u8 RemapDrums() const { return remap_drums_; }   // AUDIOSTATE+0x3a, from rapture.ini [audio] RemapDrums (read in the ctor)
    u32 LiveSoundCount() const { return u32(all_.size()); }

private:
    void notify(CAudioSnd* snd, SNDNOTIFY n);   // queues; delivered by Service() on the main thread
    void stop_snd(CAudioSnd* snd, bool emit);
    void apply_master();
    void apply_cd();
    struct Pending { u32 id; SNDNOTIFY n; PLAYSOUNDINFO info; };
    std::vector<Pending> pending_;
    bool device_ = true;
    bool avi_end_pending_ = false;
    float wave_gain(CAudioSnd* snd, const PLAYSOUNDINFO* info) const;

    SOUNDPROC proc_ = nullptr;
    void* proc_data_ = nullptr;
    int pan_x_ = 0, pan_w_ = 0;
    bool active_ = true, paused_ = false;
    int vol_[4] = {100, 100, 100, 100};
    int mute_[4] = {0, 0, 0, 0};
    u32 midi_mute_mask_ = 0;
    u8 remap_drums_ = 0;            // +0x3a
    u32 tempo_num_ = 1, tempo_den_ = 1;
    MidiPlayer midi_;
    CAudioSnd* midiSnd_ = nullptr;
    std::vector<CAudioSnd*> all_;   // every live sound (owned) - for Service()/Reset()
    std::unique_ptr<CCdAudio> cd_;  // +10 of the pimpl (created by get_cd)
    ::Window* host_ = nullptr;
};

} // namespace moses
