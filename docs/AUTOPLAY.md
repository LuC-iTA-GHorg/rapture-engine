# `--autoplay`: checking the game without disturbing the user

> Note (2026-10-19): this engine is the `rapture-engine` repo, the `engine/` submodule of a per-game repo. MuShoot (`mushoot-rapture-port`) is the **reference game**
> cited in the examples (`mushoot.exe`, `Shoot.exe`/`Shoot.dll`). The analysis of the original binaries is not part of this repository.

## What it is for
`<game>.exe --autoplay SCRIPT` runs the real game (title, then the Mushoot scene) with the SDL window **hidden**, real mouse and
keyboard discarded and audio on the SDL `dummy` driver. Input comes from a text script injected into `::Window::PollEvent`,
so the game follows its real code paths. Nothing appears on the desktop, no focus is taken, the real mouse is not touched:
it is meant to verify changes (by agents and people) while the user is working or playing. Code: `engine/autoplay/`. Without `--autoplay`
no hook is installed.

## Command line
```
<game>.exe --autoplay SCRIPT [--outdir DIR] [--settings INI] [--fast N|max]
```
Run it with the **game folder** (the one containing `mus.rrs`) as the current directory. Note: the program is
a GUI-subsystem executable, so its output has to be captured.

| Option | Effect |
|---|---|
| `--autoplay SCRIPT` | script to run (syntax below). Script errors: exit 2 before the game is opened |
| `--outdir DIR` | folder (created if missing) for the `shot` PNGs and for `autoplay_settings.ini`; default: current folder |
| `--settings INI` | settings file (records, volumes, saves) instead of `%APPDATA%\<folder>\<game>.ini (HostConfig)`. With `--autoplay` the default is a **new** `DIR\autoplay_settings.ini` (deleted at every start): the user's real data is never touched. Pass your own INI to test saves/persistent records across several runs |
| `--fast N` | game clock N times faster (N >= 1, also 4 or 10). Only with `--autoplay` (without it: error, exit 2) |
| `--fast max` | scale 20 and no wait between frames: as fast as possible |

### `--fast`
A single virtual clock at the platform level: `::Window::Ticks()` returns the scaled time, so everything that uses it
(animation queues, timers, sequences, `CAnimView::Now()`, movie pacing) runs N times faster. The sleep of the main loop (`Application`),
`FrameTimer` and the audio mixer position are scaled as well (the end of a sound with the dummy driver arrives
N times sooner, like `SND_ENDED`). `wait` and the `waitfor` timeouts stay in **game milliseconds**: the same script works
with any N and takes 1/N of the real time. Measurements (hit_hun.txt: 35 s at `--fast 1`, 5 s at `--fast 8`; options_menu.txt: 54 s -> 7.8 s).
The logged events are the same, but with a high N the random/position samples change (a `fire` may miss or hit a
different enemy): for fine score checks use N <= 4, for menu/dialog navigation and screenshots a high N is fine.

## Script commands
One command per line, `#` starts a comment. Coordinates in the **640x480** framebuffer, origin at the top left.

| Command | Effect |
|---|---|
| `wait <ms>` | let the game run for `<ms>` (game ms) |
| `waitfor dialog\|nodialog <ms>` | until a modal dialog is open / none is open |
| `waitfor scene <name> <ms>` | until the aaron scene `<name>` (`Title Screen`, `Mushoot`) |
| `waitfor level <ms>` | until Mushoot is in play (no dialog, enemies coming) |
| `waitfor enemy hun\|horseman\|hawk <ms>` | until an enemy of that type is visible |
| `move x y`, `click x y`, `rclick x y` | mouse |
| `button <id>` | click the centre of the button with that command in the open dialog |
| `key <name\|vk>` | `space esc enter f1..f12 up down left right plus minus star a..z 0..9` or a numeric VK |
| `shot <name>` | 640x480 PNG in `<outdir>/<name>.png` |
| `log` | one line on stdout (see below) |
| `fire hun\|horseman\|hawk [lead_ms] [special]` | click on the first visible enemy, led by `lead_ms` for the measured speed (about 200 ms); `special` = right button (special shot) |
| `quit` | terminate |

## Exit code
0 ok; 1 a `waitfor` timed out or a command failed; 2 script / option error (`--fast` without `--autoplay`, wrong value).
If the game does not exit by itself after the end of the script, `exit` is forced after 3 game seconds.

## Reading logs and PNGs
Stdout contains `[autoplay] ...`: settings file, number of steps, then each `log`:
`t=<game ms> scene=<scene> dialog=<class or -> score= ammo= special= houses= level= huns= horsemen= hawks= enemiesLeft= levelDone= state= running= nonblack=<non-black pixels>`.
`nonblack` is useful to tell whether the screen is black (movie/transition). PNGs can be viewed with an image viewer.
An error appears as `AUTOPLAY: ...`. To compare two logs: ignore `t=`.

## Scripts in `tools/autoplay/` (game repo)
- `hit_hun.txt`: New game -> OK (`0x2b`), level 1, 4 shots at Huns with a 300 ms lead: ammo 8 -> 4, +750 points on a hit.
- `hawk_horseman.txt`: Medium difficulty (button `0x26`) = table row 40 already at level 1, with horsemen (WAVE_B[1]) and hawks (WAVE_C[0]) without cheats. Horseman hit: +85 points (3000/35) and +1 special shot (4->5); hawk shot down only with a special shot (-1 +1, +85). For the normal levels: horsemen from level 11, hawks from 31 (Easy difficulty).
- `level1_clear.txt`: level 1 cleared by shooting all the Huns, "Level complete", bonus, level 2.
- `mushu_intro.txt`: Mushu's intro must reach the New game dialog by itself (end of sounds from the PCM with the dummy driver).
- `options_menu.txt`: Options menu (ESC): opens, photographs and closes every sub-popup (menu ids: 1 new, 2 save, 3 restore, 4 quit, 5 audio, 6 records, 7 credits, 8 help, 9 cancel).
- `title_intro.txt`: screenshots during the AVIs and the title card, without skipping.
- `title_skip.txt`: ONE single space during DILogo skips BOTH logos (a quirk of the original), then the title appears.
- `title_timing.txt`: log every 250 ms around the title, with `nonblack`.

## Tips
- To reach the game: `waitfor scene Mushoot`, `wait 1500`, `key space` (skips Mushu's intro), `waitfor dialog`, `button 0x2b`, `waitfor nodialog`, `waitfor level`.
- Button ids: New game OK `0x2b`; Options see above. If you do not know the id, `shot` the dialog and consult the dialog's code.
- A shot takes 0.3-1 s to arrive: use `fire hun 300` (lead); without it the moving target is often missed.
- Follow every important action with `log`: PNGs alone do not tell the score and state.

## Recipe for scripted use (PowerShell, one line)
```powershell
cd '<game-root>'; $o="$env:TEMP\ap1"; Start-Process -NoNewWindow -Wait -FilePath src\build\mushoot.exe -ArgumentList '--autoplay','src\tools\autoplay\hit_hun.txt','--outdir',$o,'--fast','4' -RedirectStandardOutput "$o.log"; Get-Content "$o.log"
```
With paths that contain spaces `-ArgumentList` must be given as **a single string** with inner quotes (`'--autoplay "C:\a b\x.txt" --fast 4'`): as an array the spaces split the argument (error `cannot open script 'D:\Desktop\GAME'`).
Then read the PNGs in `$o`. For the exit code: `-PassThru` and `.ExitCode`. Avoid the normal start, `--movie` or `build.bat run` when you do not want a visible window to open on the user's desktop.

## Limits
- No real audio: `dummy` driver (timings and end-of-sound notifications are verified, not what is heard).
- Only the 640x480 logical window: no test of fullscreen, scaling, DPI, real mouse.
- With a high `--fast`, behaviour that depends on samples (aim, chance) differs from `--fast 1`.
- Dialogs are driven by button id or coordinates; widgets without an id must be clicked with `click`.

## Real audio without speakers (SDL `disk` driver)
`--autoplay` forces the SDL `dummy` audio driver, which only keeps the timing. To check the **real** audio path (SDL device, audio thread,
`Audio::Mix`, the sound-end notifications driven by the mixer) without anything coming out of the speakers, select the SDL `disk` driver:
it writes the PCM that would go to the device into a file (raw signed 16 bit little endian, stereo, 22050 Hz: exactly what `Audio::Open`
asks for).

| Variable | Meaning |
|---|---|
| `SDL_AUDIODRIVER=disk` | use the disk driver. `--autoplay` keeps it if it is already set (any other value is replaced by `dummy`); the headless tests set it themselves |
| `SDL_DISKAUDIOFILE=capture.raw` | output file (default `sdlaudio.raw` in the current folder) |
| `SDL_DISKAUDIODELAY` | **do not set it**: SDL 2.32 on Windows returns the environment strings through one shared buffer and the driver reads the delay after the file name, so the output file ends up NAMED AFTER THE DELAY (a file called `1`). Without it the driver paces the writes like a real device (46 ms per 1024-frame buffer), so a capture takes as long as the game ran |

* **Test**: `<game>.exe --engine-test 11 [GAME_FOLDER]` (cwd = game folder): plays a synthetic 440 Hz tone, a hard-panned stereo buffer and the
  waves of `musfx.rrs` / `mus.rrs` (read at run time, nothing is stored) through the real device and through `CAudioLib::SoundPlay` + `Service`, and checks on the
  captured file that it is not silent, the audible length equals the source length at 22050 Hz (+-1 frame), the peak equals the source peak,
  the end notification arrived, and that a 22050 Hz mono source comes out **sample-exact** (L == R == source) after the buffer-aligned start.
  Without the archives only the synthetic part runs (SKIP is printed).
* **Whole game**: `python -I engine/tools/audio_disk_check.py build/<game>.exe <game folder> src/tools/autoplay/hit_hun.txt` runs the test above and then
  the script with the disk driver at `--fast 1` (`--fast N` scales the mixer position, so the capture would be N times too short) and reports length,
  peak and rms of the capture. Reference run (hit_hun.txt): 34.1 s captured, audible from frame 1036, peak 32768 (clipping while the sounds overlap), rms ~4000.
* Playing a capture back (`ffplay -f s16le -ar 22050 -ac 2 capture.raw`, Audacity "Import raw data") is the way to **listen** to a run later.
* MIDI is not part of the capture (it goes to the MIDI sink, see `docs/MIDI.md`): in the headless tests the recording sink checks the events.
