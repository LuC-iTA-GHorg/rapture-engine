// G1 - CArcadeGame: CGameBase + the "arcade" HUD, high score table, level property and the new-game / game-over /
// pause command flow.  Shoot.dll 0x1001850f-0x10019c86 (ctor 0x1001850f, vtbl 0x10023fe0, size 0x7b4).
// (Shoot.exe does not have this class: its CGameBoard is the base only.)
//
// The game (CShootScene of Shoot.dll, G3) derives from this class, passes its ArcadeGameDef (DAT_10031ae0) and implements CreatePlayer.
#pragma once

#include "arcade/Display.h"
#include "arcade/GameBase.h"
#include "arcade/HighScores.h"

namespace rapture {

class CArcadeGame : public CGameBase {
public:
    // 0x1001850f.  `d` must outlive the game (CGameBase keeps a pointer into d->game, as the original does with the
    // static table); the 31 dwords are copied into `adef` (+0x710) and `transitions` becomes useTransitions (+0x6f0).
    CArcadeGame(aaron::Window* host, const char* name, const ArcadeGameDef* d);
    ~CArcadeGame() override;

    // ---- Aaron slots reused by the arcade layer ---------------------------------------------------------------
    // NOTE: shoot_dll_b.md calls 0x1001960c "OnPause"; it is the Aaron OnCreate slot (+0x18): loads the pause overlay and
    // the sound banks.  0x100195b6 is OnStart (+0x1c): starts the first player.
    bool OnCreate() override;                                  // 0x1001960c
    bool OnStart() override;                                   // 0x100195b6
    int OnCommand(u32 id, uintptr_t param) override;           // 0x10018ab9

    bool Init() override;                                      // +0x10 0x10018fe8
    void DestroyMembers() override;                            // +0x24 0x1001895c (chains to CGameBase::DestroyMembers - see .cpp)
    void OnMuteChanged(bool sfxChanged, bool musicChanged) override;   // +0x44 0x100188cd
    int GetSettingsSize() override { return slots * 4 + 0x18; }        // +0x48 0x100132b0
    void SetCurrentPlayer(int index) override;                 // +0x54 0x1001982f
    void SetNumPlayers(int n) override;                        // +0x58 0x10019c86
    void ShowNewGameDialog() override { CreatePopup(nullptr, POPUP_NEWGAME_END, 0); } // +0x6c 0x10018597
    void AdvanceLevel() override;                              // +0x78 0x1001887a
    void ResetGame() override;                                 // +0x80 0x100196e5
    void EvShowGameOver() override { ShowGameOver(); }         // +0xa0 0x100185be
    void EvAC() override { CreatePopup(nullptr, POPUP_PLAYAGAIN, 0); } // +0xac 0x100196be
    void StateBC() override;                                   // +0xbc 0x10019553
    void StateC0() override;                                   // +0xc0 0x10019583
    void StateCC() override;                                   // +0xcc 0x10018f6a
    void StateD4(int arg) override;                            // +0xd4 0x100195d1
    void SerializeGlobals(aaron::RegKey* key, int save) override; // +0x114 0x1001997e (adds AGLevel, AGP%i*)
    aaron::Subscriber* GetHSDisplaySubscriber() override { return hsDisplay; }
    aaron::Subscriber* GetPlayerDisplaySubscriber(int i) override { return i == 0 ? p1Display : (i == 1 ? p2Display : nullptr); }

    // ---- new virtuals of CArcadeGame (vtbl +0x12c ..) -------------------------------------------------------------
    virtual int GetBonus() { return 0; }                       // +0x12c 0x100132d0 (Shoot.dll game override 0x10011692); HUD counter 2 value
    virtual int GetGlobalLevel() { return levelProp->Get(); }  // +0x130 0x100132e0 ("all together" level)
    virtual int NextLevel(int delta);                          // +0x134 0x1001979c: global level += delta, alive players get it
    virtual int SetLevelAll(int level);                        // +0x138 0x100198fa (named "PrevLevel" in the notes: it Sets)

    // 0x100185be: game over with the table: popups 6/7 (name entry, rank) per player in the table, 8 (table) if nobody
    void ShowGameOver();
    // CEventUserFunc callback 0x10018226: p1 = EVT_HUD_COUNTER1 / EVT_HUD_COUNTER2 creates a big counter (phase 1) and
    // deletes it (phase 2/3).  Uses the game singleton (DAT_100325a8).
    static void* HudEventFunc(CEventUserFunc* ev, uintptr_t p1, uintptr_t p2, int phase, void* data);

    CPlayer* Player(int i) { return static_cast<CPlayer*>(static_cast<CPlayerBase*>(players.At(i))); }

    // ---- fields (original offsets) -----------------------------------------------------------------------
    ArcadeGameDef adef;                       // +0x710 (31 dwords)
    int pausedByMenu = 0;                     // +0x78c a sequence was stopped by the pause command
    CHSDisplay* hsDisplay = nullptr;          // +0x790 best score
    CDisplay* big1 = nullptr;                 // +0x794 big counter 1 (level)
    CDisplay* big2 = nullptr;                 // +0x798 big counter 2
    CDisplay* p1Display = nullptr;            // +0x79c
    CDisplay* p2Display = nullptr;            // +0x7a0
    CHighScoreTable* hsTable = nullptr;       // +0x7a4
    CPropNumeric* levelProp = nullptr;        // +0x7ac global level (1..999)
    u32 savedState = 0;                       // +0x7b0

private:
    void DestroyArcade();                     // idempotent own part of DestroyMembers
    void PlaceBig1();                         // SetPosition of big1 for 1 / 2 players (0x10018f6a, 0x10018ab9 case 0x19)
};

} // namespace rapture
