// G2 - headless tests of the Host (the exe side of a game: HostApp, popups, Title scene): <game>.exe --game-test host
// No window, no audio, never launches the game.  The registry is redirected to a temporary .ini.
// The popup tests need the real game archives (HostConfig::mainArchive, commonArchive, commonStringsArchive) and the game's
// ArcadeGameDef in the CURRENT folder: run from the game folder; otherwise those groups are SKIPPED.
#include "tests/HostTests.h"

#include "aaron/RegKey.h"
#include "aaron/Scene.h"
#include "host/Popups.h"
#include "host/HostApp.h"
#include "host/HostConfig.h"
#include "host/TitleScene.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace rapture;

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

bool FileExists(const char* p) { return p && *p && std::filesystem::exists(p); }
bool CommonArchivesExist() { return FileExists(Config().commonArchive) && FileExists(Config().commonStringsArchive); }

// ---------------------------------------------------------------------------------------------
struct Sink : IKeySequenceSink {
    std::vector<std::tuple<int, char, bool>> got;
    void AddKeySequenceKey(int id, char k, bool first) override { got.emplace_back(id, k, first); }
};

// A small synthetic string table + configuration: the host logic (title, registry key, LoadString quirks) is tested against it,
// so the engine tests do not depend on any game.  The game's real table is tested by the game (see the game's own --game-test).
const StringEntry kTestStrings[] = {
    {0x5007, "Unknown error.\n"},
    {0x5010, "%s needs 256 colours"},
    {0x5014, "Test Game"},
    {0x5017, "Software\\Test Vendor"},
    {0x501a, "Test Game Key"},
    {0x501d, "XX"},
};

void TestStrings() {
    std::printf("[string table / app]\n");
    HostConfig tc;
    tc.strings = kTestStrings;
    tc.stringCount = int(sizeof kTestStrings / sizeof kTestStrings[0]);
    tc.titleStrId = 0x5014;
    tc.registryNameStrId = 0x501a;
    tc.defaultTitle = "Fallback title";
    tc.registryNameFallback = "Fallback Key";
    tc.hostBinary = "Test.exe";
    const HostConfig* saved = &Config();
    SetConfig(&tc);
    CHECK(HostApp::ResourceStringCount() == 6);
    CHECK(HostApp::ResourceString(0x5014) && std::strcmp(HostApp::ResourceString(0x5014), "Test Game") == 0);
    CHECK(HostApp::ResourceString(0x501d) && std::strcmp(HostApp::ResourceString(0x501d), "XX") == 0);
    CHECK(HostApp::ResourceString(0x5006) == nullptr);
    aaron::APPINIT init;
    HostApp::BuildAppInit(init);
    CHECK(std::strcmp(init.name, "Test Game") == 0);
    CHECK(std::strcmp(init.regKey, "Software\\Test Vendor\\Test Game Key\\XX") == 0);
    CHECK(std::strcmp(init.sceneDll, "Test.exe") == 0);
    CHECK(init.createSceneFn != nullptr);
    HostApp app(&init);
    char buf[300];
    CHECK(app.LoadString(0xE, buf, sizeof buf) == 9 && std::strcmp(buf, "Test Game") == 0); // 0x5014
    CHECK(app.LoadString(0x1, buf, sizeof buf) > 0 && std::strstr(buf, "Unknown") != nullptr);  // 0x5007
    CHECK(app.LoadString(0xA, buf, sizeof buf) > 0 && std::strstr(buf, "Test Game") != nullptr && !std::strchr(buf, '%')); // 0x5010: title
    CHECK(app.LoadString(0x100, buf, sizeof buf) == 0 && buf[0] == 0); // missing -> Application::LoadString (empty)
    CHECK(app.LoadString(0xE, buf, 5) == 4 && std::strcmp(buf, "Test") == 0);                      // truncation
    // ids missing from the table fall back to the configuration
    HostConfig empty;
    empty.defaultTitle = "Fallback title";
    empty.registryNameFallback = "Fallback Key";
    SetConfig(&empty);
    HostApp::BuildAppInit(init);
    CHECK(std::strcmp(init.name, "Fallback title") == 0);
    CHECK(std::strcmp(init.regKey, "Software\\Disney Interactive\\Fallback Key\\US") == 0);
    SetConfig(&tc);
    // key sequences
    Sink sink;
    CHECK(!app.FeedKeySequences(nullptr));
    app.SetKeySequence(0x10, "A");
    app.SetKeySequence(0x20, "DB");
    app.SetKeySequence(0x30, "ZZ"); // ignored
    CHECK(app.FeedKeySequences(&sink) && sink.got.size() == 3);
    CHECK(sink.got.size() == 3 && sink.got[0] == std::make_tuple(0x10, 'A', true) && sink.got[1] == std::make_tuple(0x20, 'D', true) &&
          sink.got[2] == std::make_tuple(0x20, 'B', false));
    // unknown popup type / no scene -> nothing
    CHECK(app.CreateObject(nullptr, 0x0c, 0) == nullptr);
    CHECK(app.CreateObject(nullptr, 0, 0) == nullptr); // no scene to hang from
    SetConfig(saved);
}

// ---------------------------------------------------------------------------------------------
// The game's own ArcadeGameDef (HostConfig::arcadeDef, with its Save / Restore list layout table), minus the audio resources
// (headless tests play nothing).
ArcadeGameDef TestDef() {
    ArcadeGameDef d = *Config().arcadeDef;
    d.sfxBankRes = d.midiRes = d.pauseAnimRes = kNoRes;
    d.transitions = 0;
    return d;
}

struct TGame : CArcadeGame {
    std::vector<std::pair<u32, uintptr_t>> cmds;
    explicit TGame(const ArcadeGameDef* d) : CArcadeGame(nullptr, "TestGame", d) {}
    ~TGame() override { Destroy(); }
    bool OpenResources() override { return OpenArchive(0, Config().mainArchive, "."); }
    CPlayerBase* CreatePlayer(int i) override { return new CPlayer(i, this, 11, 3, 5, 100); }
    int OnCommand(u32 id, uintptr_t p) override { cmds.emplace_back(id, p); return 1; } // do not run the game flow
};

aaron::Widget* FindChild(aaron::TreeNode* n, u32 cmd, bool recursive = true) {
    for (aaron::TreeNode* c = n->FirstChild(); c; c = c->NextSibling()) {
        aaron::Widget* w = static_cast<aaron::Widget*>(c);
        if (w->GetCmdId() == cmd && dynamic_cast<aaron::Button*>(w)) return w;
        if (recursive)
            if (aaron::Widget* r = FindChild(c, cmd, true)) return r;
    }
    return nullptr;
}
// press a button the way the mouse does: deferred AMSG_NOTIFY -> Widget::OnNotify -> popup->OnButton
void Press(aaron::Scene& sc, aaron::Widget* b) {
    b->PostNotify(1);
    sc.Messages().Dispatch(&sc);
}
int CountDialogs(aaron::TreeNode* n) {
    int k = 0;
    for (aaron::TreeNode* c = n->FirstChild(); c; c = c->NextSibling()) {
        if (dynamic_cast<aaron::Dialog*>(c)) ++k;
        k += CountDialogs(c);
    }
    return k;
}
int CountDialogs(aaron::Scene& sc) { return CountDialogs(sc.RootWidget()); }

void TestPopups() {
    std::printf("[popups on a headless game scene]\n");
    if (!Config().arcadeDef || !Config().mainArchive[0] || !FileExists(Config().mainArchive)) {
        std::printf("  SKIP: no game definition / main archive (HostConfig::arcadeDef, mainArchive) in the current folder\n");
        return;
    }
    std::string ini = (std::filesystem::temp_directory_path() / ("host_test_" + std::to_string(unsigned(std::rand())) + ".ini")).string();
    aaron::RegKey::SetIniPath(ini);
    aaron::APPINIT init;
    HostApp::BuildAppInit(init);
    HostApp app(&init);
    SetPopupFactory(&app);
    ArcadeGameDef def = TestDef();
    const int* listTable = reinterpret_cast<const int*>(def.listTable);   // the Save / Restore list layout (int[26])
    {
        TGame g(&def);
        g.moses::CDrawView::Init(nullptr, nullptr, 640, 480, 16);
        CHECK(g.Initialize() == 1);
        app.SetFallbackScene(&g);
        CHECK(GetPopupFactory() == &app);
        CHECK(g.hsTable != nullptr && g.players.Count() == 2);

        // ---- every type: create through the factory, then close
        const int types[] = {POPUP_OPTIONS, POPUP_NEWGAME, POPUP_NEWGAME_END, POPUP_SAVE, POPUP_RESTORE, POPUP_AUDIO, POPUP_NAME_P1,
                             POPUP_NAME_P2, POPUP_HIGHSCORES, POPUP_QUIT, POPUP_PLAYAGAIN, POPUP_NAME2_P1, POPUP_NAME2_P2};
        for (int t : types) {
            void* w = g.CreatePopup(nullptr, t, 1);
            if (!w) std::printf("  type %d: creation failed\n", t);
            CHECK(w != nullptr);
            if (!w) continue;
            auto* d = dynamic_cast<CPopup*>(static_cast<aaron::Widget*>(w));
            CHECK(d != nullptr && d->PopupType() == t && d->UserArg() == 1);
            CHECK(CountDialogs(g) == 1);
            d->Close(2, 0);
            CHECK(CountDialogs(g) == 0);
            g.Messages().Clear();
        }
        CHECK(g.CreatePopup(nullptr, 0x0c, 0) == nullptr);

        // ---- options menu -> sub popups (children of the menu), Close(.., 1) closes the whole chain
        {
            auto* opt = static_cast<aaron::Dialog*>(g.CreatePopup(nullptr, POPUP_OPTIONS, 0));
            CHECK(opt != nullptr);
            for (u32 cmd : {1u, 2u, 3u, 4u, 5u, 6u}) {
                aaron::Widget* b = FindChild(opt, cmd, false);
                CHECK(b != nullptr);
                if (!b) continue;
                Press(g, b);
                aaron::Dialog* top = nullptr;
                for (aaron::TreeNode* c = opt->FirstChild(); c; c = c->NextSibling())
                    if (auto* x = dynamic_cast<aaron::Dialog*>(c)) top = x;
                CHECK(top != nullptr);
                if (top) top->Close(2, 0); // cancel: back to the menu
                CHECK(CountDialogs(g) == 1);
                g.Messages().Clear();
            }
            opt->Close(2, 0);
            CHECK(CountDialogs(g) == 0);
            g.Messages().Clear();
        }

        // ---- new game: two players, OK
        {
            g.cmds.clear();
            auto* ng = static_cast<aaron::Dialog*>(g.CreatePopup(nullptr, POPUP_NEWGAME, 0));
            CHECK(ng != nullptr);
            CHECK(g.numPlayers == 1 || g.numPlayers == 0 || g.numPlayers == 2);
            aaron::Widget* two = FindChild(ng, 0x1d, false);
            CHECK(two != nullptr);
            Press(g, two);
            aaron::Widget* hard = FindChild(ng, 0x28, false);
            CHECK(hard != nullptr);
            Press(g, hard);
            Press(g, FindChild(ng, 0x2b, false));   // OK
            CHECK(CountDialogs(g) == 0);
            CHECK(g.numPlayers == 2);
            CHECK(g.Player(0)->GetDifficulty() == 2);
            bool posted = false;
            g.Messages().Dispatch(&g);
            for (auto& c : g.cmds) posted |= (c.first == CMD_NEWGAME);
            CHECK(posted);
            g.Messages().Clear();
        }

        // ---- high score: insertion + name entry (type 6 = player 1, rank from the table)
        {
            g.hsTable->Clear();
            int rank = g.hsTable->InsertScore(5000, 3, 1);
            CHECK(rank == 1);
            auto* ne = static_cast<aaron::Dialog*>(g.CreatePopup(nullptr, POPUP_NAME_P1, uintptr_t(rank)));
            CHECK(ne != nullptr);
            CHECK(ne->UserArg() == 1);
            // type "AB" with SPACE (cell 0, then cell 1) and finish with OK
            CHECK(ne->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_SPACE, 0) == 1);
            CHECK(ne->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_RIGHT, 0) == 1);
            CHECK(ne->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_SPACE, 0) == 1);
            CHECK(ne->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_DOWN, 0) == 1);
            Press(g, FindChild(ne, 0x1a, false));
            CHECK(CountDialogs(g) == 0);
            CHECK(g.hsTable->Initials(0)[0] == 0 && g.hsTable->Initials(0)[1] == 1 && g.hsTable->Initials(0)[2] == -1);
            // persisted in the registry: a second table instance sees it
            aaron::RegKey::FlushAll();
            CHighScoreTable t2(g.GetMachineKey(), nullptr, *def.hsTable, 0x8b003b, 0x8b011a, 0x8b011a, 0x8b000d);
            CHECK(t2.GetTopScore() == 5000 && t2.Initials(0)[0] == 0 && t2.Initials(0)[1] == 1 && t2.Level(0) == 3 && t2.Icon(0) == 1);
            g.cmds.clear();
            g.Messages().Dispatch(&g);
            bool shown = false;
            for (auto& c : g.cmds) shown |= (c.first == CMD_SHOW_HISCORES);
            CHECK(shown); // name entry done -> the table popup follows (CArcadeGame::OnCommand opens type 8)
            g.Messages().Clear();

            // type 0x10: ESC closes (result 0) and still stores the initials; follow-up is CMD_RESET_AND_MENU
            int r2 = g.hsTable->InsertScore(9000, 1, 0);
            auto* n2 = static_cast<aaron::Dialog*>(g.CreatePopup(nullptr, POPUP_NAME2_P1, uintptr_t(r2)));
            CHECK(n2 != nullptr);
            n2->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_SPACE, 0);
            n2->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_ESCAPE, 0);
            CHECK(CountDialogs(g) == 0);
            CHECK(g.hsTable->Initials(0)[0] == 0 && g.hsTable->Score(0) == 9000);
            g.cmds.clear();
            g.Messages().Dispatch(&g);
            bool reset = false;
            for (auto& c : g.cmds) reset |= (c.first == CMD_RESET_AND_MENU);
            CHECK(reset);
            g.Messages().Clear();

            // table popup + reset scores (confirm) clears the table
            auto* hs = static_cast<aaron::Dialog*>(g.CreatePopup(nullptr, POPUP_HIGHSCORES, 0));
            CHECK(hs != nullptr);
            Press(g, FindChild(hs, 0x1b, false));
            CHECK(CountDialogs(g) == 2);
            aaron::Dialog* conf = nullptr;
            for (aaron::TreeNode* c = hs->FirstChild(); c; c = c->NextSibling())
                if (auto* x = dynamic_cast<aaron::Dialog*>(c)) conf = x;
            CHECK(conf != nullptr);
            if (conf) Press(g, FindChild(conf, 0x31, false));  // yes
            CHECK(CountDialogs(g) == 1);
            CHECK(g.hsTable->GetTopScore() == 0);
            Press(g, FindChild(hs, 0x1a, false));               // OK
            CHECK(CountDialogs(g) == 0);
            g.cmds.clear();
            g.Messages().Dispatch(&g);
            bool st4 = false;
            for (auto& c : g.cmds) st4 |= (c.first == CMD_STATE4_NOW);
            CHECK(st4);
            g.Messages().Clear();
        }

        // ---- save / restore through the registry
        {
            g.Player(0)->SetLevel(4);
            g.Player(0)->SetScore(1234);
            g.Player(0)->GetLevelProp()->Snapshot();
            g.Player(0)->GetScoreProp()->Snapshot();
            auto* sd = dynamic_cast<CSaveDlg*>(static_cast<aaron::Widget*>(g.CreatePopup(nullptr, POPUP_SAVE, 0)));
            CHECK(sd != nullptr);
            CHECK(sd->Selected() == 0);                       // first empty slot
            Press(g, FindChild(sd, 0x10, false));             // OK: empty slot -> saved at once
            CHECK(CountDialogs(g) == 0);
            aaron::RegKey saves("Save", g.GetMachineKey());
            aaron::RegKey slot0("0", &saves);
            CHECK(slot0.GetDword("AGP0Level", 0) == 4 && slot0.GetDword("AGP0Score", 0) == 1234);
            g.Messages().Clear();

            // the Save dialog now selects slot 1 and the Restore dialog slot 0
            auto* sd2 = dynamic_cast<CSaveDlg*>(static_cast<aaron::Widget*>(g.CreatePopup(nullptr, POPUP_SAVE, 0)));
            CHECK(sd2 && sd2->Selected() == 1);
            sd2->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_UP, 0);
            CHECK(sd2->Selected() == 0);
            sd2->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_UP, 0);
            CHECK(sd2->Selected() == 5);                      // wraps
            sd2->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_DOWN, 0);
            CHECK(sd2->Selected() == 0);
            // occupied slot 0: OK asks for confirmation (a child dialog), "No" returns to the list
            Press(g, FindChild(sd2, 0x10, false));
            CHECK(CountDialogs(g) == 2);
            aaron::Dialog* conf = nullptr;
            for (aaron::TreeNode* c = sd2->FirstChild(); c; c = c->NextSibling())
                if (auto* x = dynamic_cast<aaron::Dialog*>(c)) conf = x;
            CHECK(conf != nullptr);
            if (conf) Press(g, FindChild(conf, 0x32, false));
            CHECK(CountDialogs(g) == 1);
            sd2->Close(2, 0);
            g.Messages().Clear();

            g.Player(0)->SetLevel(9);
            auto* rd = dynamic_cast<CRestoreDlg*>(static_cast<aaron::Widget*>(g.CreatePopup(nullptr, POPUP_RESTORE, 0)));
            CHECK(rd && rd->Selected() == 0);
            aaron::Button* okb = dynamic_cast<aaron::Button*>(FindChild(rd, 0x18, false));
            CHECK(okb && okb->DisableCount() == 0);           // enabled: slot 0 is occupied
            rd->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_DOWN, 0); // slot 1 is empty -> OK disabled
            CHECK(rd->Selected() == 1 && okb && okb->DisableCount() == 1);
            rd->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_UP, 0);
            CHECK(rd->Selected() == 0 && okb->DisableCount() == 0);
            Press(g, okb);                                     // asks for confirmation
            CHECK(CountDialogs(g) == 2);
            conf = nullptr;
            for (aaron::TreeNode* c = rd->FirstChild(); c; c = c->NextSibling())
                if (auto* x = dynamic_cast<aaron::Dialog*>(c)) conf = x;
            CHECK(conf != nullptr);
            g.cmds.clear();
            if (conf) Press(g, FindChild(conf, 0x31, false));
            CHECK(CountDialogs(g) == 0);
            CHECK(g.Player(0)->GetLevel() == 4 && g.Player(0)->GetScore() == 1234); // restored
            g.Messages().Dispatch(&g);
            bool restored = false;
            for (auto& c : g.cmds) restored |= (c.first == CMD_RESTORED);
            CHECK(restored);
            g.Messages().Clear();
        }

        // ---- mouse on the list (CListHit): click on row 3
        {
            auto* sd = dynamic_cast<CSaveDlg*>(static_cast<aaron::Widget*>(g.CreatePopup(nullptr, POPUP_SAVE, 0)));
            CHECK(sd != nullptr);
            int x = -100, y = listTable[16] + 3 * listTable[2] + listTable[2] / 2;
            g.HandleMouse(aaron::WMSG_MOUSEMOVE, x, y, 0);
            g.HandleMouse(aaron::WMSG_LBUTTONDOWN, x, y, 1);
            g.HandleMouse(aaron::WMSG_LBUTTONUP, x, y, 0);
            CHECK(sd && sd->Selected() == 3);
            if (sd) sd->Close(2, 0);
            g.Messages().Clear();
        }

        // ---- audio dialog: OK applies the check boxes
        {
            auto* ad = static_cast<aaron::Dialog*>(g.CreatePopup(nullptr, POPUP_AUDIO, 0));
            CHECK(ad != nullptr);
            Press(g, FindChild(ad, 0x2e, false));   // effects box: toggled off
            Press(g, FindChild(ad, 0x2f, false));   // OK
            CHECK(CountDialogs(g) == 0);
            CHECK(g.sfxMute == 1 && g.musicMute == 0);
            g.SetMute(0, 0);
            g.Messages().Clear();
        }

        // ---- play again: Yes answers 1
        {
            auto* pa = static_cast<aaron::Dialog*>(g.CreatePopup(nullptr, POPUP_PLAYAGAIN, 0));
            CHECK(pa != nullptr);
            g.cmds.clear();
            Press(g, FindChild(pa, 0x33, false));
            CHECK(CountDialogs(g) == 0);
            g.Messages().Dispatch(&g);
            bool yes = false;
            for (auto& c : g.cmds) yes |= (c.first == CMD_PLAYAGAIN_ANSWER && c.second == 1);
            CHECK(yes);
            g.Messages().Clear();
        }

        // ---- credits (need the common archives)
        if (CommonArchivesExist()) {
            aaron::Widget* c = static_cast<aaron::Widget*>(g.CreatePopup(nullptr, POPUP_CREDITS, 0));
            CHECK(c != nullptr);
            if (c) {
                CHECK(dynamic_cast<CCreditsScreen*>(c) != nullptr);
                static_cast<aaron::Dialog*>(c)->OnKey(aaron::WMSG_KEYDOWN, aaron::AVK_SPACE, 0); // SPACE closes
                CHECK(CountDialogs(g) == 0);
            }
            g.Messages().Clear();
        } else {
            std::printf("  SKIP credits: the common archives are not in the current folder\n");
        }
    }
    SetPopupFactory(nullptr);
    aaron::RegKey::FlushAll();
    std::error_code ec;
    std::filesystem::remove(ini, ec);
}

// ---------------------------------------------------------------------------------------------
moses::u32 g_clock = 0;
moses::u32 FakeNow() { return g_clock; }

struct TTitle : CTitleScene {
    std::vector<u32> cmds;
    using CTitleScene::CTitleScene;
    int OnCommand(u32 id, uintptr_t p) override { cmds.push_back(id); (void)p; return 0; }
};

void TestTitle() {
    std::printf("[title scene]\n");
    if (!CommonArchivesExist()) {
        std::printf("  SKIP: the common archives are not in the current folder\n");
        return;
    }
    // first visit: the intro runs through the steps and ends with the load-game command (0x2400)
    CTitleScene::g_firstVisit = true;
    CTitleScene::g_introStep = 0;
    {
        TTitle t(nullptr);
        t.Init(nullptr, nullptr, 640, 480, 16);
        CHECK(t.Initialize() == 1);
        CHECK(!CTitleScene::g_firstVisit);
        // no audio engine here: PlaySoundSimple fails for both AVIs and the title animation (step 2) is running
        CHECK(CTitleScene::g_introStep == 2);
        CHECK(t.Focus().KeyListeners().size() == 1 && t.Focus().SoundListeners().size() == 1);
        // let the animation run with a fake clock: 2 s + 3 frames + notify
        g_clock = 1000;
        moses::CAnimView::SetTimeSource(&FakeNow);
        for (int i = 0; i < 60; ++i) {
            g_clock += 100;
            t.Service();
            t.Messages().Dispatch(&t);
        }
        moses::CAnimView::SetTimeSource(nullptr);
        CHECK(CTitleScene::g_introStep == 3);
        bool load = false;
        for (u32 c : t.cmds) load |= (c == 0x2400);
        CHECK(load);
        CHECK(t.Focus().KeyListeners().empty() && t.Focus().SoundListeners().empty());
        // the skip keys: SPACE / ESC key up and a mouse button release; other keys do nothing
        CTitleScene::g_introStep = 2;
        t.Keys().OnInput(aaron::WMSG_KEYUP, 'A');
        CHECK(CTitleScene::g_introStep == 2);
        t.Keys().OnInput(aaron::WMSG_KEYUP, aaron::AVK_SPACE);
        CHECK(CTitleScene::g_introStep == 3);
        CTitleScene::g_introStep = 2;
        t.Keys().OnInput(aaron::WMSG_RBUTTONUP, 0);
        CHECK(CTitleScene::g_introStep == 3);
        // QUIRK: a skip during the first logo sets step 1 by hand
        CTitleScene::g_introStep = 0;
        t.Keys().OnInput(aaron::WMSG_KEYUP, aaron::AVK_ESCAPE);
        CHECK(CTitleScene::g_introStep == 1);
        t.Sounds().OnEnd();
        CHECK(CTitleScene::g_introStep == 2);
    }
    // second visit: clip box + credits command
    CTitleScene::g_introStep = 0;
    {
        TTitle t(nullptr);
        t.Init(nullptr, nullptr, 640, 480, 16);
        CHECK(t.Initialize() == 1);
        t.Messages().Dispatch(&t);
        bool credits = false;
        for (u32 c : t.cmds) credits |= (c == 0x1401);
        CHECK(credits);
        CHECK(CTitleScene::g_introStep == 0);
    }
    CTitleScene::g_firstVisit = true; // restore the initial value of DAT_00418574
    CTitleScene::g_introStep = 0;
}

} // namespace

int RunHostTests(int, char**) {
    std::printf("== Shoot.exe package tests (G2) ==\n");
    std::string ini0 = (std::filesystem::temp_directory_path() / ("host_test0_" + std::to_string(unsigned(std::rand())) + ".ini")).string();
    aaron::RegKey::SetIniPath(ini0);
    TestStrings();
    TestPopups();
    TestTitle();
    std::printf("== %d checks, %d failed ==\n", g_checks, g_fail);
    aaron::RegKey::SetIniPath(ini0);
    std::error_code ec;
    std::filesystem::remove(ini0, ec);
    return g_fail == 0 ? 0 : 1;
}
