// CD audio of moses.dll (the object behind CAudioLib::Cd*, created by CAudioLib::get_cd 0x10010470; class in the original:
// vtable 0x10016a40, 0x14 bytes: +4 MCI device id, +8 saved/start position, +c end position, +10 paused, +11 last track).
// The original talks to the MCI "cdaudio" device (mciSendCommand: open, set time format TMSF, status number of tracks /
// track length / position, play from..to, stop, close).
//
// PORT: SDL2 has no CD API (it was removed in SDL 2.0), and the physical drive is not what a modern PC has.  The
// faithful equivalent is a "virtual disc": the audio tracks are WAV files in a folder,
//     <folder>/track01.wav, track02.wav ...   (also "Track 01.wav" and "01.wav" are accepted; tracks must be contiguous
//     from 1: the first missing number ends the disc)
// searched in this order: the folder given to SetFolder(), $RAPTURE_CD_DIR, ./cd, ./.  Ripped CDs work as they are.  The
// tracks play through the normal mixer (platform/Audio), one after the other from `from` to `to`, so CdPlay(2, 4) plays tracks
// 2, 3 and 4 like the drive would.  Without any track files the disc is empty: CdGetMaxTrack() == 0, CdPlay does nothing
// (the same observable result as an empty drive).
//
// Time format: TMSF (track, minute, second, frame; 75 frames per second) like the original CDPOS structure.
#pragma once

#include "moses/Types.h"
#include "platform/Audio.h"
#include "platform/Sound.h"

#include <memory>
#include <string>
#include <vector>

namespace moses {

// CDPOS (4 bytes): position inside the current track (MCI TMSF status)
struct CDPOS {
    u8 track = 0, min = 0, sec = 0, frame = 0;
};

class CCdAudio {
public:
    CCdAudio();
    ~CCdAudio();

    void SetFolder(const std::string& dir);   // port: where the track files are (call before the first use / after Close)
    void SetDevice(bool on) { device_ = on; } // false = headless: no mixer, the playback is simulated with the clock
    using ClockFn = u32 (*)();
    static void SetClock(ClockFn fn);          // port: millisecond clock (default SDL_GetTicks); tests install their own
    void SetGain(float g);                     // mixer volume * mute of the CD channel (0..1)

    // ---- the six operations of the original (FUN_10012857 .. FUN_10012bb2) -------------------------------------------
    bool Open();                                   // FUN_10012857: open the "device" (scan the disc); true if it is open
    u8 MaxTrack();                                 // FUN_10012a31: number of tracks (0 = no disc)
    // FUN_10012958: for tracks from..to returns how many were measured; *out receives a length in ms.  ORIGINAL QUIRK kept:
    // the same destination is written for every track (the pointer is not advanced), so after the call it holds the length
    // of the LAST track measured.
    u32 GetLengths(u8 from, u8 to, u32* out);
    bool GetPosition(CDPOS& pos);                  // FUN_10012a71: 0 when the device is closed
    void Play(u8 from, u8 to);                     // FUN_10012bb2: tracks from..to (to == 0 or > max: up to the last)
    void Stop();                                   // FUN_100127d0: stop and close
    void Pause(bool on);                           // FUN_10012af7: pause remembers the position, resume plays on to the end

    // ---- state (CdIsPlaying / CdIsPaused) -----------------------------------------------------------------------------
    bool IsOpen() const { return open_; }          // +4 != 0: CdIsPlaying() of the original (stays true after the last track ended)
    bool IsPaused() const { return paused_; }      // +10
    bool IsSounding() const { return playing_; }   // port: audio is actually coming out
    u32 TrackLengthMs(u8 track);                   // port: length of one track (0 = missing)

    void Service();                                // port: main-loop tick (advances to the next track)

private:
    struct Track {
        std::string path;
        std::shared_ptr<PcmBuffer> pcm; // loaded on demand
        bool failed = false;
    };
    bool scan();
    bool load(u8 track);
    void start_track(u8 track, double offsetSec);
    void stop_channel();
    double elapsed() const;

    std::string folder_;
    bool device_ = true;
    bool open_ = false, paused_ = false, playing_ = false;
    std::vector<Track> tracks_;     // index 0 = track 1
    u8 from_ = 0, to_ = 0, cur_ = 0;
    double pausedAt_ = 0;           // seconds inside cur_ when paused
    ChannelId ch_ = 0;
    u32 startMs_ = 0;               // clock at the start of the current track / resume (virtual playback)
    double startOffset_ = 0;        // seconds already played when the current run started
    float gain_ = 1.0f;
};

} // namespace moses
