// G1 - players.  See Player.h.  Shoot.dll addresses in comments.
#include "arcade/Player.h"

#include "aaron/Scene.h"
#include "arcade/GameBase.h"

#include <cstring>

namespace rapture {

// ---------------------------------------------------------------------------------------------
// CPlayerBase
CPlayerBase::CPlayerBase(int idx, CGameBase* g, int propCapacity) : index(idx), game(g) { // 0x100167e0
    props = new CPropertyList(propCapacity);
}

CPlayerBase::~CPlayerBase() { delete props; } // 0x10016886

// 0x100168f1.  The controller is created by the scene (mode = GameDef::controllerMode, user = player index), suspended
// right away (the game resumes it in CGameBase::SetCurrentPlayer/Resume) and gets the play rectangle as bounds.  The
// three sources register themselves with the controller, which owns them.
bool CPlayerBase::InitInput() {
    controller = game->CreateController(game->def->controllerMode, u32(index));
    if (!props || !controller) return false;
    controller->Suspend(0);
    aaron::RECT r;
    game->GetPlayRect(&r);
    controller->SetBounds(&r);
    keyboard = new aaron::KeyboardSource(controller, 1);
    joystick = new aaron::JoystickSource(controller, u32(index) | 0x8000u);
    mouse = new aaron::MouseSource(controller, 0);
    return keyboard && joystick && mouse;
}

// 0x100168c9
void CPlayerBase::SuspendController(int on) {
    if (controller) controller->Suspend(on);
}

// 0x10016ab4
void CPlayerBase::SetDevice(int type) {
    deviceType = type;
    if (controller) controller->EnableSource(type, 3);
}

// 0x10016adc.  The device type is one dword of the Settings blob; 0 = use the game's default device.  The selected
// source is enabled with flags 3 (directions + buttons) and the others are disabled by Controller::EnableSource.
void CPlayerBase::Serialize(int save, uint8_t** pp) {
    if (save == 0) {
        std::memcpy(&deviceType, *pp, 4);
    } else {
        std::memcpy(*pp, &deviceType, 4);
    }
    *pp += 4;
    if (deviceType == 0) deviceType = int(game->def->defaultDevice);
    if (controller) controller->EnableSource(deviceType, 3);
}

// ---------------------------------------------------------------------------------------------
// CPlayer
CPlayer::CPlayer(int idx, CGameBase* g, int propCapacity, int lInit, int lMax, int extra)
    : CPlayerBase(idx, g, propCapacity), livesInit(lInit), livesMax(lMax), extraLifeEvery(extra) {} // 0x100161a0

// 0x10016252: the four properties are deleted explicitly (difficulty, level, lives, score), then ~Subscriber and
// ~CPlayerBase (property list).
CPlayer::~CPlayer() {
    delete difficulty;
    delete level;
    delete lives;
    delete score;
}

// 0x100163f2.  Creates the four properties (note the argument order of CPropNumeric: min, max, default, value), then
// registers the observers of the score: the player itself (if it awards extra lives), the high-score display and
// the player's score display of the (arcade) game.
bool CPlayer::Init() {
    difficulty = new CPropNumeric(0, 2, 0, 0);
    level = new CPropNumeric(1, 999, 1, 1);
    lives = new CPropNumeric(0, livesMax, livesInit, livesInit);
    score = new CPropScore();
    if (extraLifeEvery > 0) score->AddListener(static_cast<aaron::Subscriber*>(this));
    score->AddListener(game->GetHSDisplaySubscriber());
    score->AddListener(game->GetPlayerDisplaySubscriber(index));
    return InitInput();
}

// 0x10016622.  NOTE: the difficulty is NOT reset.  The extra-life threshold restarts at `extraLifeEvery`.
void CPlayer::ResetGame() {
    level->Reset();
    lives->Reset();
    score->Reset();
    nextExtraLife = extraLifeEvery;
}

// 0x100166b0
void CPlayer::SetExtraLifeThreshold(int s) {
    if (extraLifeEvery == 0) return; // DEVIATION: the original divides by zero (the Shoot.dll player passes -1: fine)
    nextExtraLife = (s / extraLifeEvery + 1) * extraLifeEvery;
}

// 0x10016380.  Score changed: if it reached the next threshold award ONE life (even if several thresholds were passed),
// move the threshold by `extraLifeEvery` and call the OnExtraLife hook.
void CPlayer::OnAdvise(int code, intptr_t value, aaron::Broadcaster* src) {
    if (code == -1 && src == static_cast<aaron::Broadcaster*>(score) && nextExtraLife != 0 && nextExtraLife <= int(value)) {
        AddLives(1);
        nextExtraLife += extraLifeEvery;
        OnExtraLife();
    }
}

// 0x100166d7: base (device type), then the difficulty: loaded from the LOW BYTE of the dword, saved as a full dword.
void CPlayer::Serialize(int save, uint8_t** pp) {
    CPlayerBase::Serialize(save, pp);
    int diff = difficulty->Get();
    if (save == 0) {
        difficulty->Set(int(**pp));
    } else {
        std::memcpy(*pp, &diff, 4);
    }
    *pp += 4;
}

} // namespace rapture
