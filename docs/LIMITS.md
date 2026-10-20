# LIMITS: the known limits of the engine, and what was done about them (branch `limits`, 2026-10-20)

The `HANDOFF.md` of 2026-10-20 listed five known limits: MIDI without live mute/tempo, real audio never checked, unsupported EMF records, a non-Windows build
never tried, a joystick tried only with fake devices. This document says what each one was, what was found in the original, what was built, how to test it and
what is left. Tests: `<game>.exe --engine-test 10` (MIDI sequencer + EMF + joystick + platform checks, no game data) and `--engine-test 11 [GAME_FOLDER]` (disk audio).

## 1. MIDI software sequencer (`platform/MidiSeq.*`, `platform/Midi.*`, `platform/Midi_win.cpp`)

### What the original does (moses.dll, MIDI engine 0x10011c5f..0x10013250)
Verified in the decompiled `CMidiPlayer::thread_proc` (0x10012de3), `SetTempo` (0x100130ae), `SetChannelMute` (0x10012ff9), `BreakLoops` (0x10012fa9), `Play` (0x10013120):

* **The unit of a delay is the millisecond.** The thread keeps one absolute target `t = timeGetTime()` and adds the delay of every `bit31` dword to it
  (`Sleep(t - now)` if `now < t`). The old port comment "the unit was never verified" is closed: 1 unit = 1 ms (`MidiTempoScale` only affects `CompactMidiToSmf`).
* The target is **never re-synchronised**: a late thread fires the next events at once (catch-up), no drift builds up. The last delay of the stream is waited for before the
  end (or the loop).
* **Tempo** (`MidiSetTempo(num, den)`): each delay is read as `(delay * num) / den` in 32 bit unsigned arithmetic when it is fetched, `den == 0` = off, `num == den`
  stores (0, 0). The change is live, but only affects the delays read afterwards (the one being waited for keeps its target).
* **Channel mute** (`MidiSetChannel(ch, mute)`): one flag per channel (`ch % 16`), live, kept across `Play`. Only **note-on** messages (`status & 0xf0 == 0x90`, velocity 0
  included) of a muted channel are dropped; note-off and controllers pass.
* **RemapDrums** (ini `[audio]`): a message whose low nibble is 9 gets `| 0xf` BEFORE the mute test (so the drums are muted through channel 16, index 15).
* **Loop**: at the end `idx = 0` and the target simply continues (seamless); `BreakLoops` clears the flag live. (An empty looping stream would spin forever in the original; the port ends it.)
* **Stop / end**: 16 x CC 0x7b (all notes off), `midiOutReset`, `midiOutClose` (0x10012d87, 0x10012d00).
* **Pause / Activate do NOT pause the MIDI**: `CAudioLib::Pause(1)` / `Activate(0)` reach `FUN_10011be9`, which STOPS the player; when neither is set any more the current
  sound is played again FROM THE BEGINNING (and a "started" notification is sent). The port now does exactly that (`CAudioLib::midi_gate`).

### What the port has
* `MidiSequencer`: a pure state machine `Poll(nowMs)` reproducing the thread loop (events due, absolute target, tempo, mute, remap, loop, finish). No threads, no clock of its own.
* `MidiPlayer` (`Midi.h`): drives it from a worker thread with a real millisecond clock, or in tests from a virtual clock (`SetManualClock`). Control calls (mute, tempo,
  break loops, stop) lock the same mutex the thread holds while it emits, like the original.
* `MidiSink`: where the short messages go. `Midi_win.cpp`: winmm `midiOutShortMsg` (+ `timeBeginPeriod(1)`); everywhere else (and with `RAPTURE_FORCE_PORTABLE`) a no-op sink that
  logs to stderr with `RAPTURE_MIDI_LOG=1`; `MidiRecordingSink` + `MidiManualClock` for the tests (events with scheduled time and emission time). The MCI sequencer player and the
  SMF temp file are gone; `CompactMidiToSmf` stays as a tool/test helper.
* `CAudioLib::MidiSetChannel / MidiSetTempo / MidiBreakLoops` are live now; `CAudioLib::Midi()` gives tests access to the player.

### Tests (`tests/MidiSeq_Test.cpp`, 160+ checks)
ordering/timing/catch-up, live mute (incl. velocity-0 note-on, `% 16`, persistence), RemapDrums + mute order, tempo (half/double, truncation, live change mid-wait, 32 bit wrap),
loop + BreakLoops, stop, restart, empty/zero-delay loops, hold, `MidiPlayer` with a virtual clock, one real-time worker thread test (order, never early, length), and `CAudioLib`
on top (notifications, priority, Pause/Activate restart, loop). Everything deterministic except the threaded test, which only asserts machine-independent properties.

### Left
* Nothing audible was verified: a real synthesiser (GS Wavetable / a hardware port) must be listened to by the user. The winmm sink is the same call sequence as the original but
  only compiles here (a smoke open/close runs in `--engine-test 10` when a device exists).
* Non-Windows: no sound (no-op sink). A real backend (ALSA sequencer, CoreMIDI, a software synthesiser such as TinySoundFont) can implement `MidiSink` (4 methods).

## 2. Real audio without speakers (`tests/AudioDisk_Test.cpp`, `tools/audio_disk_check.py`, `AUTOPLAY.md` last section)

SDL's `disk` audio driver writes the PCM that would reach the device to a file. `--engine-test 11 [GAME_FOLDER]` selects it (`SDL_AUDIODRIVER=disk`, `SDL_DISKAUDIOFILE`), plays a synthetic
tone, a hard-panned stereo buffer and the waves of `musfx.rrs` / `mus.rrs` (read at run time) through the real mixer, also via `CAudioLib::SoundPlay` + `Service`, and checks the captured
file: not silent, audible length equals the source length at 22050 Hz (+-1 frame), peak equals the source peak, the end notification came from the real mixer, and a 22050 Hz mono source
is **sample-exact**. `--autoplay` now keeps `SDL_AUDIODRIVER=disk` if it is set (it forces `dummy` otherwise), so `tools/audio_disk_check.py ... hit_hun.txt` captures a whole game session
(reference: 34.1 s, peak 32768 = clipping while sounds overlap, rms ~4000).

Pitfall found: SDL 2.32 on Windows returns `SDL_getenv()` strings through one shared buffer and the disk driver reads `SDL_DISKAUDIODELAY` after `SDL_DISKAUDIOFILE`, so with both set the output
file is named after the delay (a file called `1`). Never set `SDL_DISKAUDIODELAY`.

Left: what the capture sounds like (volume balance, voices, music) still needs a human ear; the capture can be played with `ffplay -f s16le -ar 22050 -ac 2 capture.raw`. MIDI and CD audio are not in the capture.

## 3. EMF records (`moses/MetaPlay.cpp`, `tests/Emf_Test.cpp`, 86 checks)

Before: unsupported (counted in `Metafile::skipped()`): regions (`FILLRGN`, `FRAMERGN`, `PAINTRGN`, `INVERTRGN`, `EXTSELECTCLIPRGN`), `ALPHABLEND`, `TRANSPARENTBLT`, `GRADIENTFILL`, `MASKBLT`,
`PLGBLT`, `POLYDRAW`, `ARCTO`, `SETARCDIRECTION`, `EXTFLOODFILL`, `SMALLTEXTOUT`, `POLYTEXTOUT`, `SETROP2`, `SETMETARGN`, `OFFSETCLIPRGN`, `WIDENPATH`, palette and colour-space records,
pattern/monochrome brushes, plus approximations (dashed pens solid, hatch brushes solid, winding fill even-odd, text paths, rotated text, ISOTROPIC as ANISOTROPIC, one excluded rectangle,
bounding-box clip paths). (The old header also listed "EXTCREATEREGION": there is no such EMF record, regions only appear inside the region records.)

Now (full list in `DrawMeta.h`): everything above is implemented except EMF+ (`GDICOMMENT` "EMF+" data is counted in `Metafile::emfPlusRecords()`, dual files draw their EMF part) and bitmaps that cannot be
decoded (RLE, embedded JPEG/PNG). The records without a visible meaning on a true-colour software surface (palettes, ICM, OpenGL, escapes, miter limit...) are accepted and ignored, so
`skipped()` now only lists record numbers that are not defined by MS-EMF, undecodable bitmaps and BitBlt-family records whose ROP needs a source the record does not carry.
Infrastructure added (additive): `GdiCtx::rop2/polyWinding/clipMask/maskRect/dashPat/dashN/brushPat*`, `gdi::PutPixelCopy/GetPixel/FillPixel`, hatch brushes (`brushStyle` 2), pattern brushes
(3), dashed pens (1..4, 7) and winding fill in the shared rasteriser (also available to `CDrawRect`/`CDrawPoly` if a game ever sets them: the games only use solid pens/brushes).

Left: EMF+, rotated glyphs from a world transform (only `lfEscapement` rotates text), pen end caps/joins, `SetBrushOrgEx`, `ETO_GLYPH_INDEX`, RLE bitmaps. Pixel-exact equality with GDI is not claimed (different
rasteriser); there is no Windows-side reference run in the tests.

## 4. Portable build (`RAPTURE_FORCE_PORTABLE`, `platform/Config.h`)

* CMake option `RAPTURE_FORCE_PORTABLE=ON` defines `RAPTURE_FORCE_PORTABLE`; `platform/Config.h` then does not define `RAPTURE_WIN32`. All optional Win32 backends are behind `RAPTURE_WIN32`
  (never `_WIN32`): winmm MIDI sink (`Midi_win.cpp`), GDI text (`GdiRaster_win.cpp`, else the stb_truetype backend), console attach in `HostMain.cpp`, `%APPDATA%` settings path (`RegKey.cpp`). `gdi32`/`winmm`
  are not linked: the resulting `mushoot.exe` imports no GDI32/USER32/WINMM. Added `find_package(Threads)` (MIDI thread) and `-lm` on Linux.
* Built and tested on MinGW with the option ON: `--engine-test 1 2 3 4 9 10 11`, `--game-test arcade|host|mushoot`, `--rrs-test --roundtrip`, a hidden-window autoplay (`hit_hun.txt`: +750 for a hit): all
  green with the same counts as the Windows build (except tests that assert GDI text pixels, now guarded by `RAPTURE_WIN32`). `engine\tools\build-engine.bat portable test` does the standalone engine build.
* Real porting bugs fixed on the way: `aaron::CombinePath/ExecutablePath` converted every separator to `\` (a path built on Linux would not open) and recognised drive letters: now host separator / no drives
  off Windows and a `/proc/self/exe` / `_NSGetExecutablePath` implementation; settings default to `$XDG_CONFIG_HOME` then `$HOME/.config` instead of the working directory.
* WSL: `wsl -l -v` shows no installed distribution on this machine (the Windows feature is not enabled), so **a real Linux/macOS build was not tried**. What MinGW cannot reveal: missing includes with
  other standard libraries (libc++), POSIX-only code paths (`readlink`, `_NSGetExecutablePath`, `setenv` in the test) which were only reviewed, file-name case, SDL on X11/Wayland.

## 5. Joystick (`platform/Joystick.cpp`, `aaron/Joystick.cpp`, `tests/Joystick_Test.cpp`, `tests/VirtualJoy.h`)

Review against the original (Aaron `JoystickDev` 0x10012688.., `JoystickSource` 0x10009d92..0x10009f80, `joyGetPos/joyGetDevCaps/joySetCapture`):

* Message ids, `wParam` encoding of the button messages (`JOY_BUTTONnCHG` | state), the `(max-min)/3` threshold, the centre `(max+min)/2`, `PickDevice`, `Activate`, capture only while the
  application is active and not paused, one move message per 10 ms, button messages only on change: all match the decompiled code (the lParam of the button messages is not used there either).
* SDL layer: left stick / D-pad + A B X Y of a game controller, or axes 0/1 / hat 0 / buttons 0..3 of a plain joystick, converted to the WinMM 0..65535 / bit mask form; the hat or D-pad overrides the axes
  with full deflection. Differences kept and documented: the SDL centre is 32768 (WinMM nominal 32767; harmless against the 10922 threshold), no dead zone and no calibration (the original also relies on the
  Windows calibration), two devices (ids 0/1) like Aaron.
* One fidelity fix: `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1`, because `joyGetPos` reads the device whatever the window focus is (Aaron itself stops polling when the application is inactive), whereas SDL
  would silently drop the input of an unfocused window.
* Tests: SDL **virtual joysticks** (SDL >= 2.0.14) give a real SDL device without hardware: axes, hat, buttons, the game-controller mapping path, a second device and the `kMaxDevices` limit, fakes overriding SDL,
  hot-unplug and re-plug; and the whole chain virtual device -> `pjoy` -> `JoystickDev` -> `JoystickSource` -> `Controller` -> `Scene::OnInput` (`--engine-test 3`). The tests skip themselves if a real
  joystick is plugged in.

Left (needs the user): a real gamepad / joystick (button order of a given model, drift, hot-plug through the OS, whether the controller database maps it), and a Windows run with a WinMM-only device
that SDL does not expose as a joystick.

## Not changed on purpose
* The WinMM/MCI fidelity of the game logic and the public API: `MidiPlayer::Play` takes the compact stream instead of an SMF image (its only caller was `CAudioLib`); everything else is additive (`CHANGES.md`).
* No game data in the repository: the disk-audio and archive tests read `mus.rrs`/`musfx.rrs` from the game folder at run time and store nothing.
