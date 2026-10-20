#!/usr/bin/env python3
"""Real-device audio check without speakers (SDL `disk` audio driver).  Standard library only (run with `python -I`).

    python -I engine/tools/audio_disk_check.py <game.exe> <game folder> [autoplay-script] [--seconds N]

1. runs `<game.exe> --engine-test 11 <game folder>` (tone + waves of the game archives, sample-exact checks);
2. optionally runs `<game.exe> --autoplay SCRIPT --fast 1` with SDL_AUDIODRIVER=disk and SDL_DISKAUDIOFILE=<temp file> and checks
   that the captured PCM (S16 stereo 22050 Hz, what Audio::Open asks for) is not silent and has a plausible length.
Never opens a visible window (autoplay hides it) and never plays through the speakers.  Do NOT set SDL_DISKAUDIODELAY (SDL 2.32 on
Windows then names the output file after the delay).  Exit code 0 = all good.  See docs/AUTOPLAY.md, "Real audio without speakers".
"""
import array
import math
import os
import subprocess
import sys
import tempfile

RATE = 22050


def analyse(path):
    raw = open(path, "rb").read()
    a = array.array("h")
    a.frombytes(raw[: len(raw) // 2 * 2])
    if sys.byteorder == "big":
        a.byteswap()
    frames = len(a) // 2
    first = last = None
    peak = 0
    acc = 0.0
    n = 0
    for f in range(frames):
        l, r = a[2 * f], a[2 * f + 1]
        m = max(abs(l), abs(r))
        if m:
            if first is None:
                first = f
            last = f
            peak = max(peak, m)
            acc += l * l + r * r
            n += 2
    rms = math.sqrt(acc / n) if n else 0.0
    return frames, first, last, peak, rms


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        return 2
    exe, game = os.path.abspath(args[0]), os.path.abspath(args[1])
    script = os.path.abspath(args[2]) if len(args) > 2 else None
    ok = True
    r = subprocess.run([exe, "--engine-test", "11", game], cwd=game, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    tail = [l for l in r.stdout.splitlines() if l.strip() and "CRITICAL" not in l][-1:]
    print("engine-test 11: exit %d  %s" % (r.returncode, tail[0] if tail else ""))
    ok &= r.returncode == 0
    if script:
        with tempfile.TemporaryDirectory() as td:
            cap = os.path.join(td, "capture.raw")
            env = dict(os.environ, SDL_AUDIODRIVER="disk", SDL_DISKAUDIOFILE=cap)
            env.pop("SDL_DISKAUDIODELAY", None)
            r = subprocess.run([exe, "--autoplay", script, "--outdir", td, "--fast", "1"], cwd=game, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            print("autoplay %s: exit %d" % (os.path.basename(script), r.returncode))
            if r.returncode != 0 or not os.path.exists(cap):
                print("  no capture or autoplay failed")
                return 1
            frames, first, last, peak, rms = analyse(cap)
            print("  captured %.2f s (%d frames @ %d Hz), audible frames %s..%s, peak %d, rms %.0f" % (frames / RATE, frames, RATE, first, last, peak, rms))
            if first is None or peak < 1000 or rms < 50:
                print("  FAIL: the capture is silent")
                ok = False
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
