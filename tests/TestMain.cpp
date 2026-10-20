// rapture_tests: the standalone test executable of the engine repository (cmake -S engine -B build-engine).
//
// It is a "game" with an empty configuration: no archives, no scene, no strings.  So only the tests that do not need game data
// do real work (`--engine-test 1|3|4`, `--game-test arcade`, `--game-test host` for the string / app part); the ones that need
// the archives of a game print SKIP.  The games run the same tests, data-driven, through their own main() (HostMain), with their
// HostConfig (archives, string table, ArcadeGameDef).
//
//   rapture_tests --engine-test N | --game-test arcade|host | --selftest
#include "host/HostMain.h"

int main(int argc, char** argv) {
    static const rapture::HostConfig cfg = [] {
        rapture::HostConfig c;
        c.productName = "rapture_tests";
        c.settingsFolder = "RaptureTests";
        c.settingsFile = "rapture_tests.ini";
        return c;
    }();
    return rapture::HostMain(argc, argv, cfg);
}
