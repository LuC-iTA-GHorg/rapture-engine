// G1 - players.  Shoot.dll 0x100167e0-0x10016dd0 (CPlayerBase), 0x100161a0-0x100166d7 (CPlayer).
// (Shoot.exe links neither class.)  CPlayerBase = input + extra property list; CPlayer adds score / lives / level /
// difficulty (observable properties) and the "extra life every N points" rule.
#pragma once

#include "aaron/Input.h"
#include "aaron/RObject.h"
#include "arcade/Property.h"

#include <cstdint>

namespace rapture {

class CGameBase;

// ---------------------------------------------------------------------------------------------
// CPlayerBase (vtbl 0x1002424c; 0x24 bytes: +4 device type, +8 index, +0xc game, +0x10 property list, +0x14 controller,
// +0x18/+0x1c/+0x20 keyboard / joystick / mouse sources).
class CPlayerBase {
public:
    CPlayerBase(int index, CGameBase* game, int propCapacity);    // 0x100167e0
    virtual ~CPlayerBase();                                       // 0x10016886

    // ---- virtual interface (vtbl slot byte offsets in comments) --------------------------------------------
    virtual bool Init() { return InitInput(); }                   // +0x04 base: InitInput (0x100168f1)
    virtual bool IsAlive() { return true; }                       // +0x08 ret 1 (0x10017740): used by NextAlivePlayer
    virtual bool IsFinished() { return false; }                   // +0x0c ret 0 (0x100132d0): used by AllPlayersDone
    virtual void ResetGame() {}                                   // +0x10 nop (0x10016e00); CPlayer: restart values
    virtual void Serialize(int save, uint8_t** pp);               // +0x14 0x10016adc
    virtual void OnActivate(int on) { (void)on; }                 // +0x18 nop (0x10001600): called with 1 by SetCurrentPlayer
    virtual void Resume() { SuspendController(1); }               // +0x1c 0x100167c0
    virtual void Pause() { SuspendController(0); }                // +0x20 0x10007640
    // +0x24..+0x64: forwarders to the property list (+0x10).  Not overridden anywhere in Shoot.dll.
    virtual void SuspendController(int on);                       // +0x68 0x100168c9: Controller::Suspend(on) (0 = suspend, 1 = resume)
    virtual void SetDevice(int type);                             // +0x6c 0x10016ab4: +4 = type, Controller::EnableSource(type, 3)

    // property list forwarders
    int PropSetAt(int i, CProperty* p) { return props->SetAt(i, p); }        // +0x24 0x10016b75
    int PropAdd(CProperty* p) { return props->Add(p); }                      // +0x28 0x10016b53
    bool PropRemove(CProperty* p) { return props->Remove(p); }               // +0x2c 0x10016d09
    CProperty* PropRemoveAt(int i) { return props->RemoveAt(i); }            // +0x30 0x10016ce7
    void PropRemoveAll() { props->RemoveAll(); }                             // +0x34 0x10016b9b
    int PropGetDefault(int i) { return props->GetDefault(i); }               // +0x38 0x10016bb7
    int PropGetMax(int i) { return props->GetMax(i); }                       // +0x3c 0x10016bd9
    int PropGetMin(int i) { return props->GetMin(i); }                       // +0x40 0x10016bfb
    int PropGetSnapshot(int i) { return props->GetSnapshot(i); }             // +0x44 0x10016c39
    int PropGet(int i) { return props->Get(i); }                             // +0x48 0x10016c5b
    CProperty* PropGetAt(int i) { return props->GetAt(i); }                  // +0x4c 0x10016c7d
    int PropIndexOf(CProperty* p) { return props->IndexOf(p); }              // +0x50 0x10016c9f
    void PropReset(int i) { props->Reset(i); }                               // +0x54 0x10016d2b
    int PropAddValue(int i, int d) { return props->AddValue(i, d); }         // +0x58 0x10016cc1
    int PropSetValue(int i, int v) { return props->SetValue(i, v); }         // +0x5c 0x10016d4d
    void PropConfigure(int i, int mn, int mx, int def, int v) { props->Configure(i, mn, mx, def, v); } // +0x60 0x10016d95
    int PropSnapshot(int i) { return props->Snapshot(i); }                   // +0x64 0x10016d73
    int PropCount() { return props->Size(); }                                // 0x10016c1d (not virtual)

    // ---- state ------------------------------------------------------------------------------------------
    int deviceType = 0;                       // +4  InputSourceType (1 keyboard, 2 mouse, 3 joystick), 0 = default
    int index = 0;                            // +8
    CGameBase* game = nullptr;                // +0xc
    CPropertyList* props = nullptr;           // +0x10
    aaron::Controller* controller = nullptr;  // +0x14 (owned by the Scene)
    aaron::KeyboardSource* keyboard = nullptr; // +0x18 (owned by the controller)
    aaron::JoystickSource* joystick = nullptr; // +0x1c
    aaron::MouseSource* mouse = nullptr;       // +0x20

protected:
    bool InitInput();                         // 0x100168f1
};

// ---------------------------------------------------------------------------------------------
// CPlayer (vtbl 0x1002417c, 50 slots; Subscriber sub-object at +0x24, vtbl 0x10024174).
class CPlayer : public CPlayerBase, public aaron::Subscriber {
public:
    // 0x100161a0.  Shoot.dll's CShootGame (0x10007090): CPlayer(idx, game, 11, -1, -1, -1).
    CPlayer(int index, CGameBase* game, int propCapacity, int livesInit, int livesMax, int extraLifeEvery);
    ~CPlayer() override;                                          // 0x10016252

    bool Init() override;                                         // +0x04 0x100163f2
    void ResetGame() override;                                    // +0x10 0x10016622
    void Serialize(int save, uint8_t** pp) override;              // +0x14 0x100166d7
    void OnAdvise(int code, intptr_t value, aaron::Broadcaster* src) override; // Subscriber slot 1: 0x10016380

    // +0x70 nop hook (0x10016e00): called after an extra life was awarded
    virtual void OnExtraLife() {}
    // property accessors (vtbl +0x74..+0xc4; the Shoot.dll class overrides none of them)
    int GetDifficulty() { return difficulty->Get(); }             // +0x74 0x10007660
    CPropNumeric* GetDifficultyProp() { return difficulty; }      // +0x78
    int GetDifficultySnapshot() { return difficulty->GetSnapshot(); } // +0x7c
    int AddDifficulty(int d) { return difficulty->Add(d); }       // +0x80 0x1001666c
    int SetDifficulty(int v) { return difficulty->Set(v); }       // +0x84 0x1001668e
    int GetLevel() { return level->Get(); }                       // +0x88
    CPropNumeric* GetLevelProp() { return level; }                // +0x8c
    int GetLevelSnapshot() { return level->GetSnapshot(); }       // +0x90
    int AddLevel(int d) { return level->Add(d); }                 // +0x94
    int SetLevel(int v) { return level->Set(v); }                 // +0x98
    int GetLives() { return lives->Get(); }                       // +0x9c
    CPropNumeric* GetLivesProp() { return lives; }                // +0xa0
    int GetLivesSnapshot() { return lives->GetSnapshot(); }       // +0xa4
    int AddLives(int d) { return lives->Add(d); }                 // +0xa8
    int SetLives(int v) { return lives->Set(v); }                 // +0xac
    int GetScore() { return score->Get(); }                       // +0xb0
    CPropNumeric* GetScoreProp() { return score; }                // +0xb4
    int GetScoreSnapshot() { return score->GetSnapshot(); }       // +0xb8
    int AddScore(int d) { return score->Add(d); }                 // +0xbc
    int SetScore(int v) { return score->Set(v); }                 // +0xc0
    void SetExtraLifeThreshold(int s);                            // +0xc4 0x100166b0: next = (s / every + 1) * every

    int livesInit = 0;          // +0x3c
    int livesMax = 0;           // +0x40
    int extraLifeEvery = 0;     // +0x44  <= 0: no extra lives
    int nextExtraLife = 0;      // +0x48  0 until ResetGame / SetExtraLifeThreshold
    CPropNumeric* score = nullptr;      // +0x4c  (a CPropScore)
    CPropNumeric* lives = nullptr;      // +0x50  min 0, max livesMax, default/initial livesInit
    CPropNumeric* level = nullptr;      // +0x54  1..999, default 1
    CPropNumeric* difficulty = nullptr; // +0x58  0..2, default 0
};

} // namespace rapture
