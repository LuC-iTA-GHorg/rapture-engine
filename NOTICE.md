# Notice

This is an **unofficial**, non-commercial fan project for game preservation and
interoperability. It is **not affiliated with, endorsed by or sponsored by** The Walt Disney
Company, Disney Interactive or Rapture Technologies.

*Disney*, *Hot Shots*, *MuShoot*, *Flying Frenzy*, *Flounder Quest*, *Rooftop Ruckus*, *Rapture*
and the related names and characters are trademarks of their respective owners. They are
mentioned only to identify the games this engine is compatible with.

## What this repository contains

- An engine written from scratch for this project (`platform/`, `moses/`, `aaron/`, `arcade/`,
  `host/`, `autoplay/`): a C++17 + SDL2 reimplementation of the behaviour of the Rapture
  Technologies engine libraries, with headless tests.
- Build scripts and tools that read the files of the original games on the user's computer
  (e.g. `.rrs` archive extractor/packer).
- Documentation of the file formats and of the engine's behaviour.
- Third-party components with their own licenses (`third_party/`).

Comments of the form `// moses.dll 0x10006e80` are only references to the place in the
original binaries where the behaviour was observed.

## What this repository does NOT contain

- No game data: no `.rrs` archives, executables, DLLs, images, sounds, music, videos, texts or
  fonts of the original games.
- No original program code and no decompiled code of the original executables or libraries.
- No game logic: the games themselves (scene code, scripts, resources) are not part of this
  repository; each game has its own port repository that uses this engine as a git submodule
  and needs the user's own copy of the game.
- No tables or other data copied from the original binaries: the ADPCM tables are computed
  from the published IMA/DVI ADPCM algorithm.

To use this software you must own the original game. Antivirus software may flag freshly
built executables (false positive); add an exclusion for your build folder if needed.
