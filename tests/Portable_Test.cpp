// Checks of the platform-dependent choices, valid for both configurations (Windows backends / RAPTURE_FORCE_PORTABLE or Linux/macOS):
// part of `<game>.exe --engine-test 10`.  Headless.
#include "tests/EngineTests.h"

#include "aaron/Path.h"
#include "aaron/RegKey.h"
#include "platform/Config.h"
#include "platform/MidiSeq.h"

#include <cstdlib>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

} // namespace

int RunPortableTests() {
#ifdef RAPTURE_WIN32
    std::printf("[platform: Win32 backends (winmm MIDI, GDI text, %%APPDATA%% settings)]\n");
#else
    std::printf("[platform: portable (no Win32 backends: null MIDI sink, stb_truetype text, XDG settings)]\n");
#endif
    // default MIDI sink: never null; the portable one accepts everything without a device
    {
        std::unique_ptr<MidiSink> s = CreateDefaultMidiSink();
        CHECK(s != nullptr);
#ifndef RAPTURE_WIN32
        CHECK(s->Open());
        s->ShortMsg(0x90 | 60 << 8 | 100 << 16, 0);
        s->Reset();
        s->Close();
#else
        // winmm: may legitimately fail without a MIDI device; opening and closing sends only all-notes-off / reset
        if (s->Open()) {
            s->Reset();
            s->Close();
        }
#endif
    }
    CHECK(!aaron::ExecutablePath().empty());
#ifndef RAPTURE_WIN32
    // settings location without %APPDATA%: $XDG_CONFIG_HOME/<folder>/<file>
    {
        namespace fs = std::filesystem;
        fs::path tmp = fs::temp_directory_path() / "rapture_limits_xdg";
        std::error_code ec;
        fs::remove_all(tmp, ec);
#ifdef _WIN32
        _putenv_s("XDG_CONFIG_HOME", tmp.string().c_str());   // MinGW in the forced-portable build: SDL_setenv would not reach std::getenv
#else
        setenv("XDG_CONFIG_HOME", tmp.string().c_str(), 1);
#endif
        aaron::RegKey::SetDefaultLocation("RaptureLimits", "portable.ini");
        {
            aaron::RegKey k(aaron::RegKey::HKCU, "Software\\Limits\\Test");
            k.SetString("Name", "value");
            aaron::RegKey::FlushAll();
        }
        fs::path f = tmp / "RaptureLimits" / "portable.ini";
        CHECK(fs::exists(f));
        std::ifstream in(f);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.find("Name=") != std::string::npos && text.find("Limits") != std::string::npos);
        fs::remove_all(tmp, ec);
    }
#endif
    std::printf("platform: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail;
}
