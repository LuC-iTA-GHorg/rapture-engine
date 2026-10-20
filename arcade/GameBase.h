// G1 - CGameBase: the multi-player game scene framework.  Shoot.dll 0x100144a8-0x10016150 (+ trampolines 0x10012d30-0x10013170);
// Shoot.exe twin: "CGameBoard" 0x409818 (dead code there).  Name "CGameBase" is hypothetical (no RTTI).
//
// Class chain of the original:  Aaron Scene -> CGameBase (0x710 bytes) -> CArcadeGame (+0xa4) -> CShootScene (Shoot.dll, G3).
//
// WHAT IT DOES: owns the players (CPlayerBase*, one per slot), the two event managers (ev0: menu / intro sequences,
// ev1: in-game sequences), the iris transition (an AnimRectSprite over the whole screen), the global property list
// (GB* registry values), the mute flags and the `Settings` / `GBxxx` persistence; and runs the state machine
// `state` (commands CMD_STATE_1..8) whose steps are virtual HOOKS overridden by the game.
//
// VIRTUAL SLOTS: the original vtable offsets are written next to each virtual.  Slots 0x00-0x40 are Aaron's Scene
// slots (OnPreCreate = Init, OnClose = DestroyMembers, OnCommand, the sound/QM_NOTIFY slot 0x3c = OnSoundMessage ...).
//
// NOTE: in the ORIGINAL `PostMessage(0x111, cmd, ...)` / QM_NOTIFY "commands" are 0xF0001001 ..; see ArcadeCmd.
#pragma once

#include "aaron/Scene.h"
#include "aaron/Sprites.h"
#include "arcade/ArcadeTypes.h"
#include "arcade/Events.h"
#include "arcade/Player.h"
#include "arcade/Property.h"

#include <cstdint>

namespace rapture {

class CGameBase : public aaron::Scene {
public:
    // 0x100144a8.  slots = player slots (+0x6d4), propCapacity = size of the global CPropertyList, def = GameDef (+0x6c0).
    CGameBase(aaron::Window* host, const char* name, int slots, int propCapacity, const GameDef* def);
    ~CGameBase() override;

    // DAT_100325a8: the one game scene (set by the constructor)
    static CGameBase* Get() { return s_instance; }

    // ---- Aaron Scene slots ----------------------------------------------------------------------------
    bool OnPreCreate() override { return Init(); }                        // slot 4 (+0x10): CGameBase::Init
    void OnClose() override { DestroyMembers(); }                         // slot 9 (+0x24)
    int OnCommand(u32 id, uintptr_t param) override;                      // slot 10 (+0x28): 0x10014e0f
    // slot 3 -> 15 (+0x3c): the QM_NOTIFY (a, b, c) of the animation objects lands in OnSoundMessage(widget, a, b, c).
    // The original does NOT route it to Widget::OnNotify (Aaron's default slot 15 is a nop).
    void OnObjectEvent(moses::CAnimObj* obj, intptr_t a, intptr_t b, intptr_t c) override;

    // ---- CGameBase virtuals (vtbl offset, Shoot.dll address of the base implementation) ---------------------
    virtual bool Init();                                                   // +0x10 0x10015078 (OnPreCreate)
    virtual void DestroyMembers();                                         // +0x24 0x10014e8e
    virtual void OnSoundMessage(aaron::Widget* w, u32 cmd, u32 b, intptr_t c);  // +0x3c 0x100153af
    virtual void OnMuteChanged(bool sfxChanged, bool musicChanged) {}      // +0x44 0x10013040 nop
    virtual int GetSettingsSize() { return slots * 4 + 0x10; }             // +0x48 0x10013050
    virtual void SerializeSettings(int save, uint8_t** pp);                // +0x4c 0x10015df5
    virtual CPlayerBase* CreatePlayer(int index) = 0;                      // +0x50 (_purecall; Shoot.dll override 0x100114df)
    virtual void SetCurrentPlayer(int index);                              // +0x54 0x100157da
    virtual void SetNumPlayers(int n) { numPlayers = uint8_t(n); }         // +0x58 0x10013070
    virtual void NextStateOrQuit();                                        // +0x5c 0x100145c3
    virtual void PostCommand();                                            // +0x60 0x10014817: PostMessage(0x111, CMD_LEAVE)
    virtual void StartPlayers(int index);                                  // +0x64 0x10014969
    virtual void EndGame(int completed);                                   // +0x68 0x10014912
    virtual void ShowNewGameDialog() { StateCC(); }                        // +0x6c base 0x10013090 (-> StateCC), arcade 0x10018597 (popup 2)
    virtual void RestartFromFirstPlayer();                                 // +0x70 0x100149f9
    virtual void SaveSettingsAndReturn();                                  // +0x74 0x10014ca5
    virtual void AdvanceLevel() {}                                         // +0x78 nop (arcade 0x1001887a)
    virtual void NextAlivePlayer();                                        // +0x7c 0x10014ce8
    virtual void ResetGame() { ResetPlayers(); }                           // +0x80 base 0x1001567e (arcade 0x100196e5)
    virtual void DispatchState();                                          // +0x84 0x10015703
    virtual void Resume();                                                 // +0x88 0x10015c7f   (hides CAnimView::Pause(int))
    virtual void Pause();                                                  // +0x8c 0x10015d40
    virtual void ShowMenu();                                               // +0x90 0x10014837
    // +0x94..+0xb0: "event" entry points, thin forwarders to the state hooks (base 0x10012d30 .. 0x10013170).  The Shoot.dll game
    // overrides Ev98, Ev9C, EvA4, EvA8, EvB0 (0x100115d7, 0x10011553, 0x100116ab, 0x100117a8, 0x10012a87).
    virtual void Ev94() { StateB8(); }                                     // +0x94 0x10012d30
    virtual void Ev98() { StateC4(); }                                     // +0x98 0x100130b0
    virtual void Ev9C() { StateC8(); }                                     // +0x9c 0x100130d0
    virtual void EvShowGameOver() { StateD0(); }                           // +0xa0 base 0x100130f0 (arcade: ShowGameOver 0x100185be)
    virtual void EvA4() { StateBC(); }                                     // +0xa4 0x10013110
    virtual void EvA8(int arg) { (void)arg; StateC0(); }                  // +0xa8 0x10013130  (EndGame passes the "completed" flag, OnSoundMessage a rank/arg: ignored by the base)
    virtual void EvAC() { StateD4(0); }                                    // +0xac 0x10013150  (arcade 0x100196be: popup 0xe)
    virtual void EvB0() { StateD8(); }                                     // +0xb0 0x10013170
    // +0xb4..+0xd8: state hooks reached by DispatchState (CMD_STATE_x) and by the events above
    virtual void StateB4();                                                // +0xb4 "GotoState0" 0x10014fa0   (CMD_STATE_8)
    virtual void StateB8();                                                // +0xb8 "GotoState3" 0x10014fde   (CMD_STATE_1)
    virtual void StateBC() { Resume(); }                                   // +0xbc base 0x10015368 (arcade 0x10019553)  (CMD_STATE_5)
    virtual void StateC0() { NextStateOrQuit(); }                          // +0xc0 base 0x10013190 (arcade 0x10019583)  (CMD_STATE_6)
    virtual void StateC4();                                                // +0xc4 "GotoState8" 0x10015019   (CMD_STATE_2)
    virtual void StateC8() { EvShowGameOver(); }                           // +0xc8 0x10012d50                (CMD_STATE_3)
    virtual void StateCC() { SaveSettingsAndReturn(); }                    // +0xcc base 0x100131b0 (arcade 0x10018f6a)
    virtual void StateD0();                                                // +0xd0 0x10015046                (CMD_STATE_4)
    virtual void StateD4(int arg);                                         // +0xd4 base 0x10015381 (arcade 0x100195d1)
    virtual void StateD8() { ShowNewGameDialog(); }                        // +0xd8 0x10012d70                (CMD_STATE_7)
    // +0xdc..+0x120: forwarders to the global property list (props)
    int SetPropAt(int i, CProperty* p) { return props->SetAt(i, p); }      // +0xdc 0x10015eba
    int AddProp(CProperty* p) { return props->Add(p); }                    // +0xe0 0x10015e92
    void RemoveAllProps() { props->RemoveAll(); }                          // +0xe4 0x10015ee6
    int PropGetDefault(int i) { return props->GetDefault(i); }             // +0xe8 0x10015f08
    int PropGetMax(int i) { return props->GetMax(i); }                     // +0xec 0x10015f30
    int PropGetMin(int i) { return props->GetMin(i); }                     // +0xf0 0x10015f58
    int PropGetSnapshot(int i) { return props->GetSnapshot(i); }           // +0xfc 0x10015f80  (0xf4 = GetAt, 0xf8 = IndexOf)
    int PropGet(int i) { return props->Get(i); }                           // +0x100 0x10015fa8
    CProperty* PropGetAt(int i) { return props->GetAt(i); }                // +0xf4 0x10015fd0
    int PropIndexOf(CProperty* p) { return props->IndexOf(p); }            // +0xf8 0x10015ff8
    int PropAddValue(int i, int d) { return props->AddValue(i, d); }       // +0x104 0x10016020
    bool RemoveProp(CProperty* p) { return props->Remove(p); }             // +0x108 0x10016074
    CProperty* RemovePropAt(int i) { return props->RemoveAt(i); }          // +0x10c 0x1001604c
    void PropReset(int i) { props->Reset(i); }                             // +0x110 0x1001609c
    virtual void SerializeGlobals(aaron::RegKey* key, int save);           // +0x114 0x100158d5 (GBNumPlayers, GBCurPlayer, GBP%iProp%d)
    void PropConfigure(int i, int mn, int mx, int def, int v) { props->Configure(i, mn, mx, def, v); } // +0x118 0x100160f0
    int PropSetValue(int i, int v) { return props->SetValue(i, v); }       // +0x11c 0x100160c4
    int PropSnapshot(int i) { return props->Snapshot(i); }                 // +0x120 0x10016128
    virtual aaron::RECT* GetPlayRect(aaron::RECT* out);                    // +0x124 0x10014a6d
    virtual u32 GetInputDeviceMask() { return def->deviceMask; }           // +0x128 0x10012d90 ("GetGameKind" in the notes)

    // ---- non-virtual helpers (Shoot.dll) --------------------------------------------------------------------
    // 0x10014633. kind 0..5 (0/2/4 close the iris over XY / Y / X, 1/3/5 open it; the flag irisClosed (+0x6ec) must be in
    // the opposite state or the call returns 0).  action/arg are carried by the QM_NOTIFY (CMD_TRANSITION_DONE, action,
    // arg) that ends the queue: OnSoundMessage then acts (1 = owner->OnChildDone of the event `arg`, 2 = EvB0, 3 =
    // StartPlayers(arg), 4 = EvA4, 5 = Resume, 6 = EvA8, 7 = Ev98, 8 = PostCommand).
    // For action 1 `arg` is an IEvent* carried pointer-sized in the QM_NOTIFY `c`, like the original.
    int StartTransition(int kind, u32 action, uintptr_t arg, int level);
    // CEventUserFunc callback (0x10014420 "TransitionMsgCallback"): p1 = kind, p2 = level.
    static void* TransitionMsgCallback(CEventUserFunc* ev, uintptr_t p1, uintptr_t p2, int phase, void* data);
    // 0x10014bd0: set the mute flags, stop sounds, notify, restart the music if needed and save "Settings"
    void SetMute(int sfx, int music);
    // 0x100155ae / 0x100155e0 / 0x1001562f: Scene::PlaySound wrappers.  The IfEnabled variants return 0 when muted.
    int PlaySoundRaw(u32 res, int a, int pan, int vol, int b, int u0, int u1) { return PlaySound(res, a, pan, vol, b, u0, u1); }
    int PlayMusicIfEnabled(u32 res, int a, int pan, int vol, int b, int u0, int u1);
    int PlaySfxIfEnabled(u32 res, int a, int pan, int vol, int b, int u0, int u1);
    void ResetPlayers();                                                   // 0x1001567e
    aaron::Controller* GetCurrentController() { return curPlayer ? curPlayer->controller : nullptr; } // 0x10014b0e ("GetCurrentPlayerAlive" in the notes: it returns +0x14)
    bool AllPlayersDone();                                                 // 0x10014b48
    // 0x10015aba: blob "Settings" (SerializeSettings + every player's Serialize) of the given key; save != 0 writes it
    void LoadSaveSettings(aaron::RegKey* key, int save);
    // FUN_10015c66: the menu sequence run by ShowMenu (owned by the game, deleted by DestroyMembers)
    void SetMenuSequence(CEventList* seq) { menuSeq = seq; }
    // 0x10016150: CEventSequencer callback of the menu sequence (code 1: state = CMD_STATE_8; code 2: DispatchState())
    static void MenuCallback(CEventSequencer* seq, int code, uintptr_t game, uintptr_t state);
    void* CreatePopup(void* parent, int type, uintptr_t arg);              // HostApp::CreateObject via the registered factory
    void GetClientSizeSafe(aaron::POINT* p);                               // Window::GetClientSize (640x480 without host)

    // Observers wired by CPlayer::Init (the original reads game+0x790 / game+0x79c+4*i, which only exist in CArcadeGame)
    virtual aaron::Subscriber* GetHSDisplaySubscriber() { return nullptr; }
    virtual aaron::Subscriber* GetPlayerDisplaySubscriber(int index) { (void)index; return nullptr; }

    // ---- fields (original offsets) -----------------------------------------------------------------------
    u32 settings0 = 0;                    // +0x6b0 serialized; incremented by every Init (probably a run counter)
    uint8_t numPlayers = 0;               // +0x6b4 (>= 1 after loading)
    int sfxMute = 0;                      // +0x6b8
    int musicMute = 0;                    // +0x6bc
    const GameDef* def = nullptr;         // +0x6c0
    CEventSpriteSequencer* ev0 = nullptr; // +0x6c4 menu / intro sequences
    CEventSpriteSequencer* ev1 = nullptr; // +0x6c8 in-game sequences
    CPlayerBase* curPlayer = nullptr;     // +0x6cc
    CEventList* menuSeq = nullptr;        // +0x6d0
    int slots = 2;                        // +0x6d4
    int curIndex = 0;                     // +0x6d8
    u32 state = 0;                        // +0x6dc CMD_STATE_x
    CPropertyList* props = nullptr;       // +0x6e0 global properties (saved as GBP%iProp%d - per player in the original)
    int simultaneous = 0;                 // +0x6e4 all players play at the same time
    int running = 0;                      // +0x6e8
    int irisClosed = 1;                   // +0x6ec (named "input enabled" in the notes; it is the iris state: 1 = closed)
    int useTransitions = 0;               // +0x6f0
    aaron::AnimRectSprite* transition = nullptr; // +0x6f4
    u32 musicId = 0;                      // +0x6f8 MIDI resource id (0 = none)
    aaron::PtrArray players;              // +0x6fc (data at +0x70c): CPlayerBase*, deleted by ~CGameBase

protected:
    void DestroyBase();                   // idempotent part of DestroyMembers (also run by the destructor)
    // transition messages (the original uses static tables 0x10031bf0..0x10031cb4)
    moses::QUEUEMSG tmSync_, tmNotify_, tmHide_;
    moses::QUEUEMSG tmIris_[6];
    moses::QUEUEMSG* tmList_[6][4];
    int tmCount_[6];

private:
    static CGameBase* s_instance;
};

} // namespace rapture
