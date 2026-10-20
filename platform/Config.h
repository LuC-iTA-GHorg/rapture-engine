// Build configuration of the platform layer.
//
// RAPTURE_WIN32 is defined when the Win32-specific code paths (winmm MIDI output, GDI text/raster, registry,
// <windows.h> helpers) are compiled in.  It is the case on Windows unless the CMake option RAPTURE_FORCE_PORTABLE=ON
// (which defines RAPTURE_FORCE_PORTABLE) is used: that build behaves like the Linux/macOS one even on MinGW.
// Code must test RAPTURE_WIN32, never _WIN32, for anything that is an optional Win32 backend.
#pragma once

#if defined(_WIN32) && !defined(RAPTURE_FORCE_PORTABLE)
#define RAPTURE_WIN32 1
#endif
