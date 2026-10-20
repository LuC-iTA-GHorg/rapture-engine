// HostMain: the command-line driver shared by all Rapture-engine games.  See HostMain.h.
#include "host/HostMain.h"

#include "aaron/RegKey.h"
#include "autoplay/Autoplay.h"
#include "host/HostApp.h"
#include "moses/DebugOverlay.h"
#include "platform/Movie.h"
#include "platform/Window.h"

#ifdef RAPTURE_WITH_TESTS
#include "tests/ArcadeTests.h"
#include "tests/EngineTests.h"
#include "tests/HostTests.h"
#include "tests/RrsTest.h"
#endif

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
// The exe is a GUI-subsystem program; give console tests an output stream.
static void AttachConsoleOutput() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            std::freopen("CONOUT$", "w", stdout);
            std::freopen("CONOUT$", "w", stderr);
        }
    }
}
#else
static void AttachConsoleOutput() {}
#endif

namespace rapture {

int HostMain(int argc, char** argv, const HostConfig& cfg) {
    SetConfig(&cfg);
    aaron::RegKey::SetDefaultLocation(cfg.settingsFolder, cfg.settingsFile);
    autoplay::SetExtension(cfg.autoplay);

#ifdef RAPTURE_WITH_TESTS
    for (int i = 1; i + 1 < argc; ++i) { // --game-test NAME (headless package tests)
        if (std::strcmp(argv[i], "--game-test") == 0) {
            AttachConsoleOutput();
            const char* name = argv[i + 1];
            int rc = 2;
            bool known = false;
            if (std::strcmp(name, "arcade") == 0) { rc = RunArcadeTests(argc - i - 1, argv + i + 1); known = true; }
            else if (std::strcmp(name, "host") == 0) { rc = RunHostTests(argc - i - 1, argv + i + 1); known = true; }
            for (int t = 0; !known && t < cfg.gameTestCount; ++t)
                if (std::strcmp(name, cfg.gameTests[t].name) == 0) { rc = cfg.gameTests[t].run(argc - i - 1, argv + i + 1); known = true; }
            if (!known) {
                std::printf("--game-test: unknown package '%s' (arcade, host", name);
                for (int t = 0; t < cfg.gameTestCount; ++t) std::printf(", %s", cfg.gameTests[t].name);
                std::printf(")\n");
            }
            std::fflush(stdout);
            return rc;
        }
    }
    for (int i = 1; i + 1 < argc; ++i) { // --engine-test N (headless engine package tests)
        if (std::strcmp(argv[i], "--engine-test") == 0) {
            AttachConsoleOutput();
            int n = std::atoi(argv[i + 1]), rc = 2;
            if (n == 1) rc = RunPkg1Tests(argc, argv);
            else if (n == 2) rc = RunPkg2Tests(argc - i - 1, argv + i + 1);
            else if (n == 3) rc = RunPkg3Tests(argc, argv);
            else if (n == 4) rc = RunPkg4Tests(argc, argv);
            else if (n == 9) rc = RunPkg9Tests(argc, argv);
            else if (n == 10) rc = RunPkg10Tests(argc, argv);
            else if (n == 11) rc = RunPkg11Tests(argc - i - 1, argv + i + 1);
            else std::printf("--engine-test: unknown package '%s' (1..4, 9, 10, 11)\n", argv[i + 1]);
            std::fflush(stdout);
            return rc;
        }
    }
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--rrs-test") == 0) {
            AttachConsoleOutput();
            int rc = RunRrsTest(argc, argv);
            std::fflush(stdout);
            return rc;
        }
    }
#endif

    for (int i = 1; i < argc; ++i) // --debug-rects: port debug overlay of recomposed/displayed rectangles (WP-9, moses/DebugOverlay.h)
        if (std::strcmp(argv[i], "--debug-rects") == 0) moses::DebugOverlay::Enable(true);

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) {
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
            SDL_Quit();
            return 0;
        }
    }

    for (int i = 1; i + 1 < argc; ++i) { // --movie FILE.avi: full-window AVI player
        if (std::strcmp(argv[i], "--movie") == 0) {
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
            Window window;
            if (!window.Create(cfg.productName[0] ? cfg.productName : "Rapture")) {
                SDL_Quit();
                return 1;
            }
            PlayMovie(window, argv[i + 1]);
            window.Destroy();
            SDL_Quit();
            return 0;
        }
    }

    // --autoplay SCRIPT [--outdir DIR]: the normal game, scripted and invisible (read the script before changing directory).
    const char* autoplayScript = nullptr;
    std::string autoplayOut;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--autoplay") == 0) autoplayScript = argv[i + 1];
        else if (std::strcmp(argv[i], "--outdir") == 0) {
            std::error_code ec;
            std::filesystem::create_directories(argv[i + 1], ec);
            autoplayOut = std::filesystem::absolute(argv[i + 1], ec).string();
        }
    }
    // --settings INI: use this settings file instead of %APPDATA%\<folder>\<file> (record, volumes, saved games).  With
    // --autoplay the default is a FRESH <outdir>/autoplay_settings.ini, so a verification run never touches the user's real data.
    std::string settingsIni;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--settings") == 0) {
            std::error_code ec;
            settingsIni = std::filesystem::absolute(argv[i + 1], ec).string();
        }
    if (settingsIni.empty() && autoplayScript) {
        std::error_code ec;
        settingsIni = (std::filesystem::absolute(autoplayOut.empty() ? "." : autoplayOut, ec) / "autoplay_settings.ini").string();
        std::filesystem::remove(settingsIni, ec);
    }
    if (!settingsIni.empty()) aaron::RegKey::SetIniPath(settingsIni);
    // --fast N|max (only with --autoplay): the virtual game clock runs N times faster (see platform/Window.h).
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--fast") == 0) {
            if (!autoplayScript || i + 1 >= argc) {
                AttachConsoleOutput();
                std::printf("--fast N|max requires --autoplay SCRIPT\n");
                return 2;
            }
            const bool mx = std::strcmp(argv[i + 1], "max") == 0;
            double n = mx ? 20.0 : std::atof(argv[i + 1]);
            if (!(n >= 1.0)) {
                AttachConsoleOutput();
                std::printf("--fast: bad value '%s' (N >= 1 or max)\n", argv[i + 1]);
                return 2;
            }
            ::Window::SetTimeScale(n, mx);
        }
    if (autoplayScript) {
        AttachConsoleOutput();
        std::printf("[autoplay] settings file %s\n", settingsIni.c_str());
        if (!autoplay::Start(autoplayScript, autoplayOut.c_str())) {
            std::fflush(stdout);
            return autoplay::ExitCode();
        }
    }

    // The game: the host exe's WinMain.  It must run with the game folder (host exe, *.rrs, *.avi) as the current directory;
    // when the main archive is not in the current folder but is next to the executable (or up to two levels up: build\<game>.exe
    // inside src\), go there.
    if (cfg.mainArchive[0] && !std::filesystem::exists(cfg.mainArchive)) {
        char* base = SDL_GetBasePath();
        if (base) {
            std::filesystem::path b(base);
            SDL_free(base);
            for (int up = 0; up < 3; ++up) {
                std::error_code ec;
                if (std::filesystem::exists(b / cfg.mainArchive, ec)) {
                    std::filesystem::current_path(b, ec);
                    break;
                }
                if (!b.has_parent_path() || b.parent_path() == b) break;
                b = b.parent_path();
            }
        }
    }
    std::string cmdline; // bare arguments are Aaron scene-dll names, options are "-x value"
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--settings") == 0 || std::strcmp(argv[i], "--fast") == 0 || (autoplayScript && (std::strcmp(argv[i], "--autoplay") == 0 || std::strcmp(argv[i], "--outdir") == 0))) {
            ++i; // option + its value are ours, not Aaron's
            continue;
        }
        if (!cmdline.empty()) cmdline += ' ';
        cmdline += '"';
        cmdline += argv[i];
        cmdline += '"';
    }
    int rc = RunHost(cmdline.c_str());
    SDL_Quit();
    if (autoplayScript) {
        std::fflush(stdout);
        return rc ? rc : autoplay::ExitCode();
    }
    return rc;
}

} // namespace rapture
