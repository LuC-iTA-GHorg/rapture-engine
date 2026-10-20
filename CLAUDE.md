# rapture-engine – working rules for contributors (human or assistant)

Shared engine of the Disney *Hot Shots* games built on Rapture Technologies' `moses.dll` +
`Aaron.dll` (identical in MuShoot, Flying Frenzy, Flounder Quest and Rooftop Ruckus). Overview:
`README.md`; status: `docs/HANDOFF.md`; plan to make it complete: `docs/COMPLETENESS.md`. This
repository is the `engine/` submodule of one repository per game (e.g. `mushoot-rapture-port`).

## Project rules
- **Portable tools**, no system-wide installs. **No Visual Studio**: MinGW-w64 + CMake + SDL2.
  `tools/setup-build-env.ps1` finds/downloads what is missing and remembers the paths
  (`.buildtools.json`, `build.env.bat`: not in git).
- No game data (graphics, sounds, videos, binaries) and no original or decompiled code in the
  repository: they stay in the user's own game folder. Nothing is copied from the original
  binaries (tables are computed, see `platform/AdpcmTables.h`).
- **1:1 fidelity** to the original: same logic, quirks **preserved** and flagged with a comment,
  `TODO` where the original is unclear. **Every function carries the original address** in its
  comment (`// moses.dll 0x10006e80`, `// Aaron ord 539 0x1000de09`, `// Shoot.exe 0x401b6f`):
  `tools/engine_coverage.py` searches for it to measure coverage, so do not omit addresses.
- The engine **contains no game name**: everything that differs lives in `rapture::HostConfig`
  (in the game repository). In the documents MuShoot appears only as the "reference game".
- Prefer verifying with `--autoplay` (hidden window, scripted input, `dummy` audio) and the
  headless tests; `--movie` and a normal start open a visible window.
- Optional Win32 backends are guarded by `RAPTURE_WIN32` (`platform/Config.h`), never by `_WIN32`; the build must also pass with `RAPTURE_FORCE_PORTABLE=ON`.
- No `<windows.h>` in the `.cpp` files of `moses/`, `aaron/`, `arcade/`, `host/` (only in the
  `*_win.cpp` files of `platform/`): it redefines `PostMessage`, `LoadString`... and breaks linking.
- Keep the build green: `engine\tools\build-engine.bat test` before every commit. Public headers:
  additive changes only, noted in `docs/CHANGES.md`.

## Commands
- `engine\tools\build-engine.bat [test|clean]` – standalone engine build (`<root>\build-engine\`);
  tests without game data: `--engine-test 1,3,4,9,10,11`, `--game-test arcade|host`; `build-engine.bat portable test` = the same with `-DRAPTURE_FORCE_PORTABLE=ON`.
- As a submodule the game repository builds everything with its own `build.bat`; the full tests
  are run from the **game folder** (the one with the `*.rrs`): `<game>.exe --engine-test 1|2 .|3|4`,
  `--game-test arcade|host|<game>`, `--rrs-test`.
- `python -I engine\tools\engine_coverage.py` – regenerates `docs/ENGINE_API_COVERAGE.md` and the
  generated block of `docs/COMPLETENESS.md` (needs `pefile` in the user site-packages and the
  original game folders; name lists from a separate analysis folder are optional).
- `engine\tools\new-game.ps1 -GameDir <game folder> -Name <name>` – creates `<game folder>\src` for
  a new game **with this repository as a submodule**. Always try it on a scratch folder.

## Known pitfalls
- **Antivirus**: false positives may delete freshly built `.exe` files; add an exclusion.
- **GUI subsystem**: the executables (`rapture_tests.exe`, `<game>.exe`) do not write to the
  terminal; capture the output with `Start-Process -NoNewWindow -Wait -RedirectStandardOutput
  file.txt -ArgumentList ...` from PowerShell.
- **`cmd` and the current folder**: if `NoDefaultCurrentDirectoryInExePath=1` is set, `cmd` does
  not look for `.bat` files in the current folder; run them with a path (`.\build.bat`).
- **Submodule**: inside `engine/` of a game repository run `git checkout master` (do not work on
  a detached HEAD), commit and push **here**, then commit the pointer in the game repository.
- Addresses are those of the original file (image base `0x400000` for the host exe, `0x10000000`
  for the DLLs).
- `.gitattributes`: `*.bat` and `*.ps1` use CRLF line endings.

## Working with sub-agents
- The packages of `docs/COMPLETENESS.md` (WP-n) and `docs/WORKPLAN.md` are meant for parallel
  agents: each agent has **its own files** (no conflicts), additive public headers only, new
  headless tests, no `git commit` by the agent (the integrator does it).
- Use persistent worktrees; **weak** temporary stubs (`__attribute__((weak))`) in
  `engine/stubs/`, deleted at integration. Details and sample prompts: `docs/REUSE.md`.
- Give the agent: the documents to read, the addresses to port, the build/test command and the
  report format; ask it to re-run `engine_coverage.py`.
- Do not block on questions: choose the simplest hypothesis, flag it with `TODO(...)` and report it.

## Where things are
`platform/` SDL2 and Win32 replacements; `moses/` and `aaron/` the two DLLs; `arcade/` the Arcade
library; `host/` host exe + `HostConfig`; `autoplay/`; `tests/`; `tools/`; `docs/`.
The reverse-engineering material used to establish the behaviour (Ghidra project, decompiler
output, notes) is kept outside this repository.
