// G1 - CArcadeGame.  See ArcadeGame.h.  Shoot.dll addresses in comments.
#include "arcade/ArcadeGame.h"

#include <cstdio>

namespace rapture {

// ---------------------------------------------------------------------------------------------
// Default tables of Shoot.dll .data
const DisplayStyle kStyleHighScore = {13, 12, 0, -235, 0x270b, 0, 0, 0x8020, 9, 0, 0};      // DAT_10031e60
const DisplayStyle kStyleP1 = {13, 12, -250, -235, 0x270b, 0, 0, 0x8020, 9, 0, 0};          // DAT_10031e90
const DisplayStyle kStyleP2 = {13, 12, 250, -235, 0x270b, 0, 0, 0x8020, 9, 0, 0};           // DAT_10031ec0
const DisplayStyle kStyleBig1 = {50, 50, 105, 31, 0x26f2, 0, 1, 0x101, 3, 10000, 0};        // DAT_10031ef0
const DisplayStyle kStyleBig2 = {50, 50, 75, 25, 0x26f2, 0, 1, 0x101, 4, 225, 100};         // DAT_10031f20
const HSTableDef kDefaultHSTable = {10, 9990, 22, 15, -55, -101, 12, 9, 0x80, 65, -101, 12, 3, 0x80, 150, -103, -165, -100, 16}; // DAT_10031f50

// 0x1001850f.  CGameBase(host, name, 2 slots, def[0] = property capacity, &def[1]); then the 31 dwords of the definition are
// copied to +0x710 and `useTransitions` (+0x6f0) = def[30].
CArcadeGame::CArcadeGame(aaron::Window* host, const char* name, const ArcadeGameDef* d)
    : CGameBase(host, name, 2, d->propCapacity, &d->game), adef(*d) {
    useTransitions = adef.transitions;
}

CArcadeGame::~CArcadeGame() { DestroyArcade(); }

// 0x1001895c: levelProp, hsTable, hsDisplay, p1Display, p2Display, big1, big2 (this order).  The original does not chain to
// CGameBase::DestroyMembers (the first half's scene override presumably calls both): the port chains, which is harmless
// because the base part is idempotent.
void CArcadeGame::DestroyArcade() {
    delete levelProp;
    levelProp = nullptr;
    delete hsTable;
    hsTable = nullptr;
    delete hsDisplay;
    hsDisplay = nullptr;
    delete p1Display;
    p1Display = nullptr;
    delete p2Display;
    p2Display = nullptr;
    delete big1;
    big1 = nullptr;
    delete big2;
    big2 = nullptr;
}

void CArcadeGame::DestroyMembers() {
    DestroyArcade();
    CGameBase::DestroyMembers();
}

// ---------------------------------------------------------------------------------------------
// 0x10018fe8.  Creates the level property, the high score table and the HUD displays, then CGameBase::Init (which
// creates the players: their score properties are observed by hsDisplay / p1Display / p2Display, so those must exist first).
bool CArcadeGame::Init() {
    levelProp = new CPropNumeric(1, 999, 1, 1);
    if (adef.hsTable == nullptr) adef.hsTable = &kDefaultHSTable;
    hsTable = new CHighScoreTable(GetMachineKey(), RootWidget(), *adef.hsTable, adef.lettersRes, adef.hsScoreDigitsRes,
                                  adef.hsLevelDigitsRes, adef.hsIconRes);
    aaron::WidgetParent root(RootWidget());
    if (adef.hsDigitsRes != kNoRes) {
        if (adef.hsStyle == nullptr) adef.hsStyle = &kStyleHighScore;
        hsDisplay = new CHSDisplay(root, adef.hsDigitsRes, *adef.hsStyle, true);
        hsDisplay->DisplayNumber(hsTable->GetTopScore(), 0);
    }
    if (adef.p1DigitsRes != kNoRes) {
        if (adef.p1Style == nullptr) adef.p1Style = &kStyleP1;
        p1Display = new CDisplay(root, adef.p1DigitsRes, *adef.p1Style, true);
    }
    if (adef.p2DigitsRes != kNoRes) {
        if (adef.p2Style == nullptr) adef.p2Style = &kStyleP2;
        p2Display = new CDisplay(root, adef.p2DigitsRes, *adef.p2Style, true);
    }
    if (adef.big1Res != kNoRes) {
        if (adef.big1Style == nullptr) adef.big1Style = &kStyleBig1;
        big1 = new CDisplay(root, adef.big1Res, *adef.big1Style, true);
        if (numPlayers < 2) big1->SetPosition(adef.big1Style->x, adef.big1Style->y + adef.big1YOffset, 0); // (numPlayers is still 0 here)
    }
    if (adef.big2Res != kNoRes) {
        if (adef.big2Style == nullptr) adef.big2Style = &kStyleBig2;
        big2 = new CDisplay(root, adef.big2Res, *adef.big2Style, true);
    }
    AddAccelerator(0x1b, CMD_PAUSE, 0, 0);
    return CGameBase::Init();
}

// 0x1001960c (Aaron OnCreate): pause picture and sound resources
bool CArcadeGame::OnCreate() {
    if (adef.pauseRes != 0) SetOverlayRes(adef.pauseRes);
    if (sfxMute == 0 && adef.sfxBankRes != kNoRes) LoadResource(adef.sfxBankRes);
    if (musicMute == 0 && adef.midiRes != kNoRes) LoadResource(adef.midiRes);
    if (adef.pauseAnimRes != kNoRes) LoadResource(adef.pauseAnimRes);
    return true;
}

// 0x100195b6 (Aaron OnStart)
bool CArcadeGame::OnStart() {
    RestartFromFirstPlayer();
    return true;
}

// 0x100188cd: the sound banks are loaded only while the corresponding sound class is not muted
void CArcadeGame::OnMuteChanged(bool sfxChanged, bool musicChanged) {
    if (sfxChanged) {
        if (sfxMute == 0) LoadResource(adef.sfxBankRes);
        else FreeResource(adef.sfxBankRes);
    }
    if (musicChanged) {
        if (musicMute == 0) LoadResource(adef.midiRes);
        else FreeResource(adef.midiRes);
    }
}

// 0x1001982f: snapshot of the four main properties of every player, then the base
void CArcadeGame::SetCurrentPlayer(int index) {
    for (u32 i = 0; i < numPlayers; ++i) {
        CPlayer* p = Player(int(i));
        p->GetDifficultyProp()->Snapshot();
        p->GetLivesProp()->Snapshot();
        p->GetScoreProp()->Snapshot();
        p->GetLevelProp()->Snapshot();
    }
    CGameBase::SetCurrentPlayer(index);
}

// 0x10019c86
void CArcadeGame::SetNumPlayers(int n) {
    numPlayers = uint8_t(n);
    if (p1Display) p1Display->DisplayNumber(0, 0);
    if (n < 2) {
        if (p2Display) p2Display->Show(false);
    } else if (p2Display) {
        p2Display->DisplayNumber(0, 0);
    }
}

// 0x1001887a
void CArcadeGame::AdvanceLevel() {
    if (simultaneous == 0) {
        if (curPlayer) static_cast<CPlayer*>(curPlayer)->AddLevel(1);
    } else {
        NextLevel(1);
    }
}

// 0x100196e5: reset the HUD, the global level and the players
void CArcadeGame::ResetGame() {
    if (p1Display) p1Display->DisplayNumber(0, 0);
    if (numPlayers < 2) {
        if (p2Display) p2Display->Show(false);
    } else if (p2Display) {
        p2Display->DisplayNumber(0, 0);
    }
    if (big2) big2->Show(false);
    if (big1) big1->Show(false);
    levelProp->Reset();
    ResetPlayers();
}

void CArcadeGame::StateBC() { // 0x10019553
    if (big1) big1->Show(false);
    Resume();
}

void CArcadeGame::StateC0() { // 0x10019583
    if (big2) big2->Show(false);
    NextStateOrQuit();
}

void CArcadeGame::PlaceBig1() {
    if (big1) {
        const DisplayStyle* st = adef.big1Style;
        int y = numPlayers < 2 ? st->y + adef.big1YOffset : st->y;
        big1->SetPosition(st->x, y, 0);
    }
}

void CArcadeGame::StateCC() { // 0x10018f6a
    PlaceBig1();
    SaveSettingsAndReturn();
}

void CArcadeGame::StateD4(int arg) { // 0x100195d1
    if (arg == 0) {
        RemoveAccelerator(0x1b, 0);
        Ev98();
    } else {
        SaveSettingsAndReturn();
    }
}

// 0x1001979c
int CArcadeGame::NextLevel(int delta) {
    int v = levelProp->Add(delta);
    for (u32 i = 0; i < numPlayers; ++i) {
        CPlayer* p = Player(int(i));
        if (p->IsAlive()) p->SetLevel(v);
    }
    return v;
}

// 0x100198fa
int CArcadeGame::SetLevelAll(int level) {
    int v = levelProp->Set(level);
    for (u32 i = 0; i < numPlayers; ++i) Player(int(i))->SetLevel(v);
    return v;
}

// ---------------------------------------------------------------------------------------------
// Shared by ShowGameOver and OnCommand(CMD_GAMEOVER_NAMES): insert the players' scores in the table and open the
// name popups (type base + (player != 0)) for the players that entered, last player first; popup chain via `parent`.
// returns true if at least one player entered.
static bool InsertScoresAndPopups(CArcadeGame* g, bool onlyAlive, int popupBase) {
    int ranks[2] = {0, 0};
    int order[2] = {0, 1};
    if (g->numPlayers > 1) {
        if (g->Player(0)->GetScore() < g->Player(1)->GetScore()) {
            order[0] = 1;
            order[1] = 0;
        }
    }
    for (int k = 0; k < int(g->numPlayers) && k < 2; ++k) {
        CPlayer* p = g->Player(order[k]);
        if (onlyAlive && !(p->IsAlive() || g->simultaneous != 0)) continue;
        int r = g->hsTable->InsertScore(p->GetScore(), p->GetLevel(), p->GetDifficulty());
        if (p->index >= 0 && p->index < 2) ranks[p->index] = r;
    }
    bool any = false;
    void* parent = nullptr;
    for (int k = int(g->numPlayers) - 1; k > -1; --k) {
        if (k < 2 && ranks[k] != 0) {
            any = true;
            parent = g->CreatePopup(parent, popupBase + (k != 0 ? 1 : 0), uintptr_t(ranks[k]));
        }
    }
    return any;
}

// 0x100185be
void CArcadeGame::ShowGameOver() {
    if (simultaneous == 0) {
        CPlayer* p = static_cast<CPlayer*>(curPlayer);
        int rank = hsTable->InsertScore(p->GetScore(), p->GetLevel(), p->GetDifficulty());
        int type = rank == 0 ? POPUP_HIGHSCORES : (curPlayer->index != 0 ? 1 : 0) + POPUP_NAME_P1;
        CreatePopup(nullptr, type, uintptr_t(rank));
    } else {
        if (!InsertScoresAndPopups(this, false, POPUP_NAME_P1)) CreatePopup(nullptr, POPUP_HIGHSCORES, 0);
    }
}

// ---------------------------------------------------------------------------------------------
// 0x10018ab9
int CArcadeGame::OnCommand(u32 id, uintptr_t param) {
    switch (id) {
    case CMD_NEWGAME: // popup "new game" confirmed
        if (running != 0) Pause();
        StateCC();
        break;
    case CMD_GAMEOVER_NAMES: // game over with scores: name entry popups 0x10 / 0x11
        RemoveAccelerator(0x1b, 0);
        if (running != 0) Pause();
        if (!InsertScoresAndPopups(this, true, POPUP_NAME2_P1)) {
            ResetGame();
            Ev98();
        }
        break;
    case CMD_STATE4_NOW:
        StateD0();
        break;
    case CMD_SHOW_HISCORES:
        CreatePopup(nullptr, POPUP_HIGHSCORES, 0);
        break;
    case CMD_PLAYAGAIN_ANSWER:
        StateD4(int(param));
        break;
    case CMD_RESET_AND_MENU:
        ResetGame();
        Ev98();
        break;
    case CMD_RESTORED: // game restored from a save slot
        if (simultaneous == 0) {
            CPlayerBase* p = static_cast<CPlayerBase*>(players.At(curIndex));
            if (!p) return 0; // (does NOT chain to CGameBase::OnCommand, as the original)
            StartPlayers(p->index);
        } else {
            StartPlayers(0);
        }
        PlaceBig1();
        break;
    case CMD_RESUME_MENU: // the options popup was closed: restart the sequence that was stopped by CMD_PAUSE
        if (pausedByMenu != 0) {
            pausedByMenu = 0;
            state = savedState;
            DispatchState();
        }
        break;
    case CMD_PAUSE: // ESC: stop the running menu sequence (remember the state) and open the options popup
        pausedByMenu = ev0->Stop(0) ? 1 : 0;
        if (pausedByMenu != 0) savedState = state;
        CreatePopup(nullptr, POPUP_OPTIONS, 0);
        break;
    default:
        break;
    }
    return CGameBase::OnCommand(id, param);
}

// ---------------------------------------------------------------------------------------------
// 0x10018226
void* CArcadeGame::HudEventFunc(CEventUserFunc* ev, uintptr_t p1, uintptr_t, int phase, void* data) {
    CArcadeGame* g = static_cast<CArcadeGame*>(CGameBase::Get());
    if (!g) return nullptr;
    if (phase == 1) {
        if (p1 == EVT_HUD_COUNTER1) {
            if (g->adef.big1Res != kNoRes) {
                if (g->adef.big1Style == nullptr) g->adef.big1Style = &kStyleBig1;
                CDisplay* d = new CDisplay(aaron::WidgetParent(g->RootWidget()), g->adef.big1Res, *g->adef.big1Style, true);
                int y = g->numPlayers < 2 ? g->adef.big1Style->y + g->adef.big1YOffset : g->adef.big1Style->y;
                d->SetPosition(g->adef.big1Style->x, y, 0);
                d->SetCallback(&CEventUserFunc::OnDone, reinterpret_cast<uintptr_t>(static_cast<IEvent*>(ev)), 0);
                u32 value = g->simultaneous == 0 ? u32(static_cast<CPlayer*>(g->curPlayer)->GetLevel()) : u32(g->GetGlobalLevel());
                d->SetValue(value, 0);
                return d;
            }
        } else if (p1 == EVT_HUD_COUNTER2 && g->adef.big2Res != kNoRes) {
            if (g->adef.big2Style == nullptr) g->adef.big2Style = &kStyleBig2;
            CDisplay* d = new CDisplay(aaron::WidgetParent(g->RootWidget()), g->adef.big2Res, *g->adef.big2Style, true);
            d->SetCallback(&CEventUserFunc::OnDone, reinterpret_cast<uintptr_t>(static_cast<IEvent*>(ev)), 0);
            d->SetValue(u32(g->GetBonus()), 0);
            return d;
        }
    } else if (phase > 1 && phase < 4 && data != nullptr) {
        delete static_cast<CDisplay*>(data);
    }
    return nullptr;
}

// 0x1001997e
void CArcadeGame::SerializeGlobals(aaron::RegKey* key, int save) {
    char name[52];
    CGameBase::SerializeGlobals(key, save);
    if (save == 0) {
        levelProp->Set(int(key->GetDword("AGLevel", 0)));
    } else {
        key->SetDword("AGLevel", u32(levelProp->Get())); // current value (the player values below are the snapshots)
    }
    for (u32 i = 0; i < numPlayers && int(i) < players.Count(); ++i) {
        CPlayer* p = Player(int(i));
        if (save == 0) {
            std::snprintf(name, sizeof(name), "AGP%iScore", int(i));
            int v = int(key->GetDword(name, 0));
            p->SetExtraLifeThreshold(v);
            p->SetScore(v);
            std::snprintf(name, sizeof(name), "AGP%iLevel", int(i));
            p->SetLevel(int(key->GetDword(name, 0)));
            std::snprintf(name, sizeof(name), "AGP%iLives", int(i));
            p->SetLives(int(key->GetDword(name, 0)));
            std::snprintf(name, sizeof(name), "AGP%iDifficulty", int(i));
            p->SetDifficulty(int(key->GetDword(name, 0)));
        } else {
            std::snprintf(name, sizeof(name), "AGP%iScore", int(i));
            key->SetDword(name, u32(p->GetScoreSnapshot()));
            std::snprintf(name, sizeof(name), "AGP%iLevel", int(i));
            key->SetDword(name, u32(p->GetLevelSnapshot()));
            std::snprintf(name, sizeof(name), "AGP%iLives", int(i));
            key->SetDword(name, u32(p->GetLivesSnapshot()));
            std::snprintf(name, sizeof(name), "AGP%iDifficulty", int(i));
            key->SetDword(name, u32(p->GetDifficultySnapshot()));
        }
    }
}

} // namespace rapture
