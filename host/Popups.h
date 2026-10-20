// G2 - the popups (dialogs) of Shoot.exe.  Shoot.exe 0x401d30-0x408e40 + the factory HostApp::CreateObject (0x40127a).
//
// All of them derive from Aaron's Dialog (the original "ord 15" panel: one bitmap of mus.rrs + buttons).  The original
// classes have no RTTI; the names below come from the vtable owners in names_shoot_exe.tsv.  vtable slots of the originals:
//   [4]  0x10  OnButton(id, state)  = Widget::OnNotify(id, src)  (buttons have style 8: notify the parent)
//   [26] 0x68  OnClose(result)      [27] 0x6c OnInit()           [28] 0x70 OnApply / "OnYes" / "DoSave" ...  [29] 0x74 OnReset
//
// All the popups talk to the game scene (CGameBase / CArcadeGame, G1) through dynamic_cast of Widget::GetScene().
#pragma once

#include "aaron/Scene.h"
#include "aaron/Sprites.h"
#include "arcade/ArcadeGame.h"
#include "arcade/Display.h"
#include "moses/QueueMsg.h"

namespace rapture {

using aaron::Widget;
using aaron::WidgetParent;

// ---------------------------------------------------------------------------------------------
// Resource ids used by the popups (mus.rrs slot 0, shoot_exe.md sec. 3)
constexpr u32 kResSoundGroup1 = 0x860043;   // button sound groups, Load/Free with the life of the popup
constexpr u32 kResSoundGroup2 = 0x860044;
constexpr u32 kSndOk = 0x85003d, kSndCancel = 0x85003e, kSndOneP = 0x85003f, kSndTwoP = 0x850040, kSndSelect = 0x850041,
              kSndDifficulty = 0x850042;
constexpr u32 kResLetters = 0x8b003a;       // alphabet group of the name entry (+ cursor frames)
constexpr u32 kResDifficultyIcons = 0x8b000d;

// ---------------------------------------------------------------------------------------------
// Common base ("CPopup" in the tsv: CloneDib / GetClassId 0x20109 / GetPickFlags 0x80 live in Dialog already).
class CPopup : public aaron::Dialog {
public:
    // The factory arguments of every popup: Dialog(parent, resId, 0, 0, 0x26fc, type, 0x20000, arg, 3).
    CPopup(WidgetParent parent, u32 resId, u32 cmdId, u32 style, uintptr_t arg, int level = 0x26fc);

    // Shoot.exe vtable slot [4]: OnButton(id, state).  Buttons deliver Widget::OnNotify(id, src); `arg` is the second
    // argument of the original (the widget pointer for buttons, 1/2 = hover/click for CListHit).
    void OnNotify(u32 id, Widget* src) override { OnButton(id, reinterpret_cast<intptr_t>(src)); }
    virtual void OnButton(u32 id, intptr_t arg) { (void)id; (void)arg; }
    virtual void OnReset() {}   // slot [29] 0x74 (CNewGameDlg: nop, CNameEntryDlg: ResetSelection)

    CGameBase* Game() const { return dynamic_cast<CGameBase*>(scene_); }
    CArcadeGame* Arcade() const { return dynamic_cast<CArcadeGame*>(scene_); }
    // CGameBoard::PlaySoundIfSfxOn(res, 0, 0, 100, 0, 0, 0)  (Shoot.exe 0x40a99f; Shoot.dll 0x1001562f)
    void Sfx(u32 res) const;
    aaron::Button* AddButton(u32 res, u32 cmd, u32 style, int mode);
    int PopupType() const { return int(cmdId_); } // the factory gives the popup its type as command id

protected:
    // Dialog::Close wrappers must be the last thing a handler does: the popup deletes itself (Aaron Dialog::Close).
};

// ---------------------------------------------------------------------------------------------
// type 0.  Shoot.exe 0x403e91 (OnInit), 0x40426c (OnButton), 0x4046b0 (OnClose)
class COptionsMenu : public CPopup {
public:
    COptionsMenu(WidgetParent parent, uintptr_t arg, u32 style = 0x20000) : CPopup(parent, 0x8200e4, POPUP_OPTIONS, style, arg) {}
    int OnInit() override;
    void OnButton(u32 id, intptr_t arg) override;
    void OnClose(int result) override;
};

// types 1 and 2.  Shoot.exe 0x402652 (OnInit), 0x4034df (ApplyChoices), 0x4036ba (OnButton), 0x403d9f / 0x403e00 (OnClose)
class CNewGameDlg : public CPopup {
public:
    CNewGameDlg(WidgetParent parent, int type, u32 style, uintptr_t arg) : CPopup(parent, 0x8200b2, u32(type), style, arg) {}
    int OnInit() override;
    void OnApply() override { ApplyChoices(); }          // slot 0x70
    void OnReset() override {}                           // slot 0x74 "Nop" 0x4036af
    void OnButton(u32 id, intptr_t arg) override;
    void OnClose(int result) override;

protected:
    void ApplyChoices();
    aaron::Button* b_[14] = {};   // +0x84..+0xb8 (see the .cpp for the order)
    u32 deviceMask_ = 0;          // +0xbc  scene->GetInputDeviceMask()
    int joystickCount_ = 0;       // +0xc0
};

// type 2: "CNewGameDlg2" (0x401f80, vtable 0x415994): same dialog, OnClose differs (0x403e00)
class CNewGameDlg2 : public CNewGameDlg {
public:
    CNewGameDlg2(WidgetParent parent, uintptr_t arg, u32 style = 0x20000) : CNewGameDlg(parent, POPUP_NEWGAME_END, style, arg) {}
    void OnClose(int result) override;
};

// ---------------------------------------------------------------------------------------------
// Save / Restore: a list of 6 slots (registry sub keys Save\<n>).  Shoot.exe 0x4046fc ... 0x4066be.
struct SlotListLayout {            // the int table scene+0x778 (ArcadeGameDef::listTable, Shoot.dll 0x10031a80)
    int rows, z, pitch;
    int scoreX, scoreY0, scoreDx, scoreDigits, scoreFlags;
    int levelX, levelY0, levelDx, levelDigits, levelFlags;
    int iconX, iconY0;
    int rectLeft, rectTop, rectRight, highlightColor;
    int numX, numY0, numDx, numDigits, numFlags;
};
class CListHit;

class CSlotDlg : public CPopup {
public:
    using CPopup::CPopup;
    ~CSlotDlg() override;
    int Selected() const { return sel_; }   // selected slot (+0x84 of the Save dialog, +0x88 of the Restore dialog)

protected:
    // the loop shared by CSaveDlg::OnInit and CRestoreDlg::OnInit; returns the first slot satisfying `wantOccupied`
    // (-1 = none) or -2 when the layout table is missing (OnInit must then fail).
    int BuildList(bool wantOccupied, int hitCmd);
    bool SlotOccupied(int i) const { return i >= 0 && i < 6 && icons_[i] && icons_[i]->IsVisible() != 0; }
    void MoveHighlight(int slot);
    const SlotListLayout* lay_ = nullptr;
    CDisplay* num_[6] = {};
    CDisplay* level_[6] = {};
    CDisplay* score_[6] = {};
    aaron::DibSprite* icons_[6] = {};
    CListHit* hit_ = nullptr;
    aaron::RectSprite* hilite_ = nullptr;
    int sel_ = 0;
};

class CSaveDlg : public CSlotDlg {   // type 3
public:
    CSaveDlg(WidgetParent parent, uintptr_t arg, u32 style = 0x20000) : CSlotDlg(parent, 0x82010e, POPUP_SAVE, style, arg) {}
    int OnInit() override;                 // 0x4046fc
    void OnApply() override { DoSave(); }  // 0x405193
    void OnButton(u32 id, intptr_t arg) override;   // 0x4054d8
    void OnClose(int result) override;     // 0x4055af
    int OnKey(u32 msg, u32 vk, u32 lParam) override; // 0x4055d5
private:
    void DoSave();
    aaron::Button* ok_ = nullptr;          // +0x88
};

class CRestoreDlg : public CSlotDlg {   // type 4
public:
    CRestoreDlg(WidgetParent parent, uintptr_t arg, u32 style = 0x20000) : CSlotDlg(parent, 0x820107, POPUP_RESTORE, style, arg) {}
    int OnInit() override;                 // 0x4056e1
    void OnApply() override { DoRestore(); } // 0x40618c
    void OnButton(u32 id, intptr_t arg) override;   // 0x406527
    void OnClose(int result) override;     // 0x406698
    int OnKey(u32 msg, u32 vk, u32 lParam) override; // 0x4066be
private:
    void DoRestore();
    void UpdateOk();
    aaron::Button* ok_ = nullptr;          // +0x84
};

// Hit-test grid.  Shoot.exe 0x408746 (ctor), 0x4088b0 (OnMouse).  Derived from RectSprite (Aaron ord 20), invisible.
class CListHit : public aaron::RectSprite {
public:
    CListHit(CPopup* owner, WidgetParent parent, u32 cmd, const aaron::RECT* rc, int rows, int cols);
    int OnMouse(u32 msg, u32 wParam, intptr_t lParam) override;
    int Cell() const { return cell_; }       // +0xd4
private:
    CPopup* owner_;                           // +0xac
    u32 cmd_;                                 // +0xc0
    int left_, top_, right_, bottom_;         // +0xb0..+0xbc
    int rows_, cols_;                         // +0xc4 +0xc8
    int cellW_, cellH_;                       // +0xcc +0xd0
    int cell_ = 0;                            // +0xd4
};

// ---------------------------------------------------------------------------------------------
// type 5.  Shoot.exe 0x407b29 / 0x407d6c / 0x407df1
class CAudioDlg : public CPopup {
public:
    CAudioDlg(WidgetParent parent, uintptr_t arg, u32 style = 0x20000) : CPopup(parent, 0x82009f, POPUP_AUDIO, style, arg) {}
    int OnInit() override;
    void OnApply() override {}           // "Nop" 0x407d61
    void OnButton(u32 id, intptr_t arg) override;
    void OnClose(int result) override;
private:
    aaron::Button* sfx_ = nullptr;       // +0x84  (0x8b0008, cmd 0x2e): effects check box
    aaron::Button* music_ = nullptr;     // +0x88  (0x8b0006, cmd 0x2d): music check box
};

// type 8.  Shoot.exe 0x4068f5 / 0x406a2f / 0x406b64
class CHighScoreDlg : public CPopup {
public:
    CHighScoreDlg(WidgetParent parent, uintptr_t arg, u32 style = 0x20000) : CPopup(parent, 0x8200aa, POPUP_HIGHSCORES, style, arg) {}
    int OnInit() override;
    void OnButton(u32 id, intptr_t arg) override;
    void OnClose(int result) override;
private:
    CHighScoreTable* table_ = nullptr;   // +0x88
};

// types 6, 7, 0x10, 0x11.  Shoot.exe 0x406bc7 ... 0x407adc.   userArg = rank (1-based) of the new record.
class CNameEntryDlg : public CPopup {
public:
    CNameEntryDlg(WidgetParent parent, int type, uintptr_t arg, u32 style = 0x20000) : CPopup(parent, 0x8200ab, u32(type), style, arg) {}
    int OnInit() override;                           // 0x406bc7
    void OnApply() override;                         // "OnShow" 0x407359: store the initials, free the grid
    void OnReset() override;                         // ResetSelection 0x4073dd
    void OnButton(u32 id, intptr_t arg) override;    // 0x40721e
    void OnClose(int result) override;               // 0x407481 (type 0x10/0x11: 0x407adc)
    int OnKey(u32 msg, u32 vk, u32 lParam) override; // 0x4074ce
private:
    void PickLetter();                               // 0x407901
    void PlaceCursor();
    void PlaceSlotCursor();
    int rows_ = 0, cols_ = 0, pitch_ = 0;            // +0x88 +0x8c +0x90
    int n_ = 0;                                      // +0x94 number of grid cells (letters + blank + 2 arrows)
    int slot_ = 0;                                   // +0x98 initial being typed (0..2)
    int cell_ = 0;                                   // +0x9c grid cursor
    int initials_[3] = {-1, -1, -1};                 // +0xa0
    int rank_ = 0;                                   // +0xac
    aaron::DibSprite* cursor_ = nullptr;             // +0xb0
    aaron::DibSprite* slotCursor_ = nullptr;         // +0xb4
    aaron::DibSprite* slotSprite_[3] = {};           // +0xb8
    CListHit* hit_ = nullptr;                        // +0xc4
};

// types 0x10 / 0x11: "CNameEntryDlg2" (vtable 0x415630): game over of CArcadeGame (CMD_GAMEOVER_NAMES): OnClose posts 0xF0001017
class CNameEntryDlg2 : public CNameEntryDlg {
public:
    CNameEntryDlg2(WidgetParent parent, int type, uintptr_t arg, u32 style = 0x20000) : CNameEntryDlg(parent, type, arg, style) {}
    void OnClose(int result) override;               // 0x407adc
};

// ---------------------------------------------------------------------------------------------
// Confirmations: the bitmap 0x820098 ("Vuoi davvero uscire?") is reused by all of them (lazy localisation).
class CConfirmDlg : public CPopup {     // base 0x415514, OnInit 0x407e4f, OnButton 0x407f4a, OnClose 0x407f87
public:
    CConfirmDlg(WidgetParent parent, u32 cmd, u32 style, uintptr_t arg) : CPopup(parent, 0x820098, cmd, style, arg) {}
    int OnInit() override;
    void OnButton(u32 id, intptr_t arg) override;
    void OnClose(int result) override;
    void OnApply() override = 0;         // "Yes" (slot 0x70)
};

class CQuitConfirmDlg : public CConfirmDlg {   // type 9, 0x407fad
public:
    CQuitConfirmDlg(WidgetParent parent, u32 cmd, u32 style, uintptr_t arg) : CConfirmDlg(parent, cmd, style, arg) {}
    void OnApply() override;
    void PostCmd1012();                  // 0x407fe5: dead code in the original (no caller)
};

class CResetScoresConfirmDlg : public CConfirmDlg {   // 0x40801d / 0x408054
public:
    CResetScoresConfirmDlg(WidgetParent parent, uintptr_t arg) : CConfirmDlg(parent, 0xb, 0x220000, arg) {}
    void OnApply() override;
    void OnClose(int result) override;
};

class CSaveConfirmDlg : public CConfirmDlg {          // 0x408090  userArg = slot
public:
    CSaveConfirmDlg(WidgetParent parent, uintptr_t arg) : CConfirmDlg(parent, 0xb, 0x220000, arg) {}
    void OnApply() override;
};

class CRestoreConfirmDlg : public CConfirmDlg {       // 0x4082d7  userArg = slot
public:
    CRestoreConfirmDlg(WidgetParent parent, uintptr_t arg) : CConfirmDlg(parent, 0xb, 0x220000, arg) {}
    void OnApply() override;
};

// type 0xe.  Shoot.exe 0x408574 / 0x408686 / 0x408700
class CPlayAgainDlg : public CPopup {
public:
    CPlayAgainDlg(WidgetParent parent, uintptr_t arg, u32 style = 0x20000) : CPopup(parent, 0x820100, POPUP_PLAYAGAIN, style, arg) {}
    int OnInit() override;
    void OnButton(u32 id, intptr_t arg) override;
    void OnClose(int result) override;
};

// type 0xf.  Shoot.exe 0x40114c (CreateCredits), 0x402444 (OnInit), 0x4023c4 (OnClose), 0x4023b0 (scroll end), 0x402614 (OnKey)
// userArg = "modal" flag: when set the application quits when the credits end.
class CCreditsScreen : public CPopup {
public:
    CCreditsScreen(WidgetParent parent, uintptr_t modal)
        : CPopup(parent, 0x64820002, POPUP_CREDITS, 0x20000, modal, 0x7fffffff) {}
    int OnInit() override;
    void OnClose(int result) override;
    int OnKey(u32 msg, u32 vk, u32 lParam) override;
    static void OnScrollDone(moses::CAnimObj* obj, uintptr_t popup, uintptr_t unused);
private:
    aaron::AnimTextSprite* text_ = nullptr;   // +0x84
    int prevMinLevel_ = 0;                    // +0x88
    moses::QUEUEMSG msgs_[7];                 // the tables 0x4184b0.. (rebuilt at run time: they hold the text height)
    moses::QUEUEMSG* list_[7];
};

} // namespace rapture
