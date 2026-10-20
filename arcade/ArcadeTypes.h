// G1 - Arcade framework: shared constants and plain-data structures.
//
// The "Arcade / GameBoard" library of Digital Doorway exists twice in the original (statically linked in
// Shoot.exe at 0x40xxxx and in Shoot.dll at 0x1001xxxx, identical code).  The port has ONE copy, here, in
// `namespace rapture`.  Addresses in comments are the Shoot.dll ones; the Shoot.exe twin is listed when it exists.
//
// NOTE ON COMMAND IDS: the analysis notes write "0xF001xxxx"; the real values (decompiled case labels such as
// -0xfffefff) are 0xF0001001 .. 0xF000101F.
#pragma once

#include "aaron/Types.h"

#include <cstdint>

namespace rapture {

using aaron::RECT;
using aaron::u32;

constexpr u32 kNoRes = 0xffffffffu; // "no resource" (the original compares resource ids with -1)

// ---------------------------------------------------------------------------------------------
// Scene commands (Scene::PostMessage(0x111, cmd, param) -> OnCommand) and QM_NOTIFY ids.
enum ArcadeCmd : u32 {
    // game states: CGameBase::state (+0x6dc), consumed by CGameBase::DispatchState 0x10015703
    CMD_STATE_1 = 0xF0001001, // -> hook slot 0xb8  (GotoState3)
    CMD_STATE_2 = 0xF0001002, // -> hook slot 0xc4  (GotoState8)
    CMD_STATE_3 = 0xF0001003, // -> hook slot 0xc8  (-> ShowGameOver)
    CMD_STATE_4 = 0xF0001004, // -> hook slot 0xd0
    CMD_STATE_5 = 0xF0001005, // -> hook slot 0xbc
    CMD_STATE_6 = 0xF0001006, // -> hook slot 0xc0
    CMD_STATE_7 = 0xF0001007, // -> hook slot 0xd8
    CMD_STATE_8 = 0xF0001008, // -> hook slot 0xb4  (GotoState0, the menu sequence end state: see MenuCallback)
    CMD_SKIP = 0xF0001009,    // SPACE accelerator (CGameBase::Init): stop both event managers (skip intros/sequences)
    CMD_TRANSITION_DONE = 0xF000100A, // QM_NOTIFY (a) at the end of an iris transition; b = action 1..8, c = argument
    CMD_LEAVE = 0xF000100B,   // CGameBase::OnCommand: LoadScene(previous module) - back to the Title scene
    CMD_SFX_POS = 0xF000100C,      // OnSoundMessage: play sfx (b = sound id), pan from the sprite position when c != 0
    CMD_SFX_POS_LOOP = 0xF000100D, // same, looped
    CMD_SFX = 0xF000100E,          // play sfx unconditionally through PlaySoundRaw
    CMD_SND = 0xF000100F,          // Scene::PlaySound(b, 0, 0, 100, loop=0)
    CMD_SND_LOOP = 0xF0001010,     // Scene::PlaySound(b, 0, 0, 100, loop=1)
    CMD_SND_STOP = 0xF0001011,     // Scene::StopSound(b)
    // CArcadeGame::OnCommand 0x10018ab9
    CMD_NEWGAME = 0xF0001012,      // "new game" confirmed by the popup (shoot_exe: CNewGameDlg::OnClose)
    CMD_GAMEOVER_NAMES = 0xF0001013, // game over: insert scores, popups 0x10/0x11 for the name entry
    CMD_STATE4_NOW = 0xF0001014,   // -> vtbl+0xd0
    CMD_SHOW_HISCORES = 0xF0001015, // popup 8
    CMD_PLAYAGAIN_ANSWER = 0xF0001016, // CPlayAgainDlg result (param 1 = yes)
    CMD_RESET_AND_MENU = 0xF0001017,   // name entry finished
    CMD_RESTORED = 0xF0001019,     // game restored from a save slot (CRestoreDlg)
    CMD_RESUME_MENU = 0xF000101A,  // popup closed / game saved: resume the stopped sequence
    CMD_PAUSE = 0xF000101B,        // ESC accelerator: stop the running sequence, open the options popup (type 0)
    // ids of CEventUserFunc::p1 handled by CArcadeGame::HudEventFunc 0x10018226
    EVT_HUD_COUNTER1 = 0xF000101E, // big counter 1 (+0x740/+0x744), value = current level
    EVT_HUD_COUNTER2 = 0xF000101F, // big counter 2 (+0x74c/+0x750), value = GetBonus()
};

// Popup types of HostApp::CreateObject (Shoot.exe 0x40127a; shoot_exe.md sec. 5)
enum PopupType : int {
    POPUP_OPTIONS = 0, POPUP_NEWGAME = 1, POPUP_NEWGAME_END = 2, POPUP_SAVE = 3, POPUP_RESTORE = 4, POPUP_AUDIO = 5,
    POPUP_NAME_P1 = 6, POPUP_NAME_P2 = 7, POPUP_HIGHSCORES = 8, POPUP_QUIT = 9, POPUP_PLAYAGAIN = 0xe, POPUP_CREDITS = 0xf,
    POPUP_NAME2_P1 = 0x10, POPUP_NAME2_P2 = 0x11,
};

// ---------------------------------------------------------------------------------------------
// CDisplay style: 11 dwords (DAT_10031e60 / e90 / ec0 / ef0 / f20), copied by value to the CDisplay ctor.
struct DisplayStyle {
    int dx;        // width of one digit cell
    int dy;        // height of one digit cell
    int x, y;      // anchor
    int z;         // sprite level
    int p8;        // unused by the constructors
    int hidden;    // bit 0: start hidden (do not show "0")
    u32 flags;     // see CDisplay (1 counting, 2 timer, 8 grouped, 0x10 right, 0x20 centre, ...)
    int maxDigits;
    int step;      // counting step (flag 1) - Shoot.dll: InitCounter param
    int p13;       // delay in ms between counting steps (flag 1)
};

// High score table definition: 19 dwords copied at +0x108 of the table (default DAT_10031f50).
struct HSTableDef {
    int rows;                                     // +0x108 (max 20)
    int z;                                        // +0x10c sprite level
    int rowPitch;                                 // +0x110
    u32 columns;                                  // +0x114 bit 1 score, 2 initials, 4 level, 8 icon (difficulty)
    int scoreX, scoreY0, scoreDx, scoreDigits;   // +0x118..+0x124
    u32 scoreFlags;                               // +0x128
    int levelX, levelY0, levelDx, levelDigits;   // +0x12c..+0x138
    u32 levelFlags;                               // +0x13c
    int iconX, iconY0;                            // +0x140 +0x144
    int initX0, initY0, initPitch;                // +0x148 +0x14c +0x150
};

// What CGameBase keeps at +0x6c0 (= &ArcadeGameDef[1]).
struct GameDef {
    u32 deviceMask;       // [1] GetInputDeviceMask(): bit0 joystick, bit1 mouse, bit2 keyboard (Shoot.dll: 2 = mouse only).
                          //     (notes call it "GetGameKind" - it is the mask used by CNewGameDlg, shoot_exe.md sec. 5)
    u32 defaultDevice;    // [2] InputSourceType used when a player's saved device is 0 (Shoot.dll: 2 = mouse)
    u32 controllerMode;   // [3] first argument of Scene::CreateController (Shoot.dll: 4)
    const RECT* playRect; // [4] play area, nullptr -> {-w/2, -(h/2-40), w/2, h/2-40}
};

// ArcadeGameDef: 31 dwords of DAT_10031ae0 copied by the CArcadeGame ctor to +0x710 (index = dword index).
// Pointers are real pointers in the port (the original has 32-bit addresses).
struct ArcadeGameDef {
    int propCapacity;                // [0]  capacity of the global CPropertyList (GB*)
    GameDef game;                    // [1..4]
    u32 pauseRes;                    // [5]  +0x724  "Pausa" picture (overlay while paused)
    u32 hsDigitsRes;                 // [6]  +0x728  digits of the high score display
    const DisplayStyle* hsStyle;     // [7]  +0x72c  null -> kStyleHighScore
    u32 p1DigitsRes;                 // [8]  +0x730
    const DisplayStyle* p1Style;     // [9]  +0x734  null -> kStyleP1
    u32 p2DigitsRes;                 // [10] +0x738
    const DisplayStyle* p2Style;     // [11] +0x73c  null -> kStyleP2
    u32 big1Res;                     // [12] +0x740  big counter 1
    const DisplayStyle* big1Style;   // [13] +0x744  null -> kStyleBig1
    int big1YOffset;                 // [14] +0x748  added to the counter's y when there is a single player
    u32 big2Res;                     // [15] +0x74c
    const DisplayStyle* big2Style;   // [16] +0x750  null -> kStyleBig2
    u32 lettersRes;                  // [17] +0x754  initials font (high score table)
    u32 hsScoreDigitsRes;            // [18] +0x758
    u32 hsLevelDigitsRes;            // [19] +0x75c
    u32 hsIconRes;                   // [20] +0x760  difficulty icons (3 frames)
    const HSTableDef* hsTable;       // [21] +0x764  null -> kDefaultHSTable
    u32 spare22to25[4];              // [22..25] +0x768..+0x774 (Shoot.dll: 0x8b011a, 0x8b011a, 0x8b000d, 0x8b011a) - used by the dialogs (G2)
    const HSTableDef* listTable;     // [26] +0x778  layout of the Save/Restore lists (CSaveDlg "scene+0x778", shoot_exe.md)
    u32 sfxBankRes;                  // [27] +0x77c  sound bank loaded while sfx are on
    u32 midiRes;                     // [28] +0x780  background music
    u32 pauseAnimRes;                // [29] +0x784
    int transitions;                 // [30] +0x788 -> CGameBase::useTransitions (+0x6f0)
};

// Default tables of Shoot.dll .data
extern const DisplayStyle kStyleHighScore; // DAT_10031e60
extern const DisplayStyle kStyleP1;        // DAT_10031e90
extern const DisplayStyle kStyleP2;        // DAT_10031ec0
extern const DisplayStyle kStyleBig1;      // DAT_10031ef0
extern const DisplayStyle kStyleBig2;      // DAT_10031f20
extern const HSTableDef kDefaultHSTable;   // DAT_10031f50

// ---------------------------------------------------------------------------------------------
// The popup factory of Shoot.exe (HostApp::CreateObject 0x40127a, vtbl slot 0x38).  Shoot.dll reached it with
// Application::GetApp() + a virtual call; here G2 registers it once at start-up (SetPopupFactory).
// `parent` = previous popup of a chain (the original passes the object returned by the previous call, or 0).
class IPopupFactory {
public:
    virtual ~IPopupFactory() = default;
    virtual void* CreateObject(void* parent, int type, uintptr_t arg) = 0;
};
void SetPopupFactory(IPopupFactory* f);
IPopupFactory* GetPopupFactory();

} // namespace rapture
