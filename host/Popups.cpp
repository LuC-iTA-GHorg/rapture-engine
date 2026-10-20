// G2 - popups of Shoot.exe, part 1: base class, options, new game, audio, play again, confirmations, credits.
// Every function carries its Shoot.exe address.  NOTE: Aaron's Dialog::Close deletes the dialog: nothing may touch `this`
// after a Close() call.
#include "host/Popups.h"

#include "host/HostApp.h"
#include "host/HostConfig.h"

#include <cstdio>

namespace rapture {

using aaron::Button;
using aaron::DibSprite;

// ---------------------------------------------------------------------------------------------
CPopup::CPopup(WidgetParent parent, u32 resId, u32 cmdId, u32 style, uintptr_t arg, int level)
    : Dialog(parent, resId, 0, 0, level, cmdId, style, arg, 3) {}

// CGameBoard::PlaySoundIfSfxOn 0x40a99f
void CPopup::Sfx(u32 res) const {
    if (CGameBase* g = Game()) g->PlaySfxIfEnabled(res, 0, 0, 100, 0, 0, 0);
}

Button* CPopup::AddButton(u32 res, u32 cmd, u32 style, int mode) {
    return Widget::Attach(new Button(WidgetParent(static_cast<Widget*>(this)), res, 0, 0, 0x26fc, cmd, style, mode));
}

// ---------------------------------------------------------------------------------------------
// COptionsMenu
// Shoot.exe 0x403e91.  Button ids 1..9; ids are the ones of the dispatch in OnButton.
int COptionsMenu::OnInit() {
    scene_->AddAccelerator(0x20, CMD_SKIP, 0, 0);            // SPACE
    AddButton(0x8b0029, 1, 8, 0);   // new game
    AddButton(0x8b002b, 2, 8, 0);   // save
    AddButton(0x8b002a, 3, 8, 0);   // restore
    AddButton(0x8b0026, 4, 8, 0);   // quit
    AddButton(0x8b0028, 6, 8, 0);   // high scores
    AddButton(0x8b0025, 7, 8, 0);   // credits
    AddButton(0x8b0027, 8, 8, 0);   // help
    AddButton(0x8b0024, 9, 8, 1);   // cancel (default button)
    AddButton(0x8b0023, 5, 8, 0);   // audio
    return 1;
}

// 0x40426c.  The sub-popups are children of this menu with style 0x220000 (0x200000: Close(.., notify = 0) shows the menu again).
void COptionsMenu::OnButton(u32 id, intptr_t) {
    WidgetParent me(static_cast<Widget*>(this));
    switch (id) {
    case 1: Widget::Attach(new CNewGameDlg(me, POPUP_NEWGAME, 0x220000, 0)); break;
    case 2: Widget::Attach(new CSaveDlg(me, 0, 0x220000)); break;
    case 3: Widget::Attach(new CRestoreDlg(me, 0, 0x220000)); break;
    case 4: Widget::Attach(new CQuitConfirmDlg(me, POPUP_QUIT, 0x220000, 0)); break;
    case 5: Widget::Attach(new CAudioDlg(me, 0, 0x220000)); break;
    case 6: Widget::Attach(new CHighScoreDlg(me, 0, 0x220000)); break;
    case 7:
        if (HostApp* app = dynamic_cast<HostApp*>(aaron::Application::GetApp())) app->CreateCredits(0, this);
        break;
    case 8: // QUIRK: "Help" calls vtable +0x90 of the scene, which in CGameBase is ShowMenu (the WinHelp call of the
            // original product is not there); with the base class it just (re)starts the menu sequence.
        if (CGameBase* g = Game()) g->ShowMenu();
        break;
    case 9: Close(2, 0); break;
    default: break;
    }
}

// 0x4046b0: stops the menu sequence (notify) and asks the scene to resume the interrupted one
void COptionsMenu::OnClose(int) {
    if (CGameBase* g = Game()) {
        if (g->ev0) g->ev0->Stop(1);
    }
    scene_->PostMessage(aaron::WMSG_COMMAND, CMD_RESUME_MENU, 0);
}

// ---------------------------------------------------------------------------------------------
// CNewGameDlg.  b_ order (offset in the original): 0 +0x84 one player, 1 +0x88 two players, 2 +0x8c keyboard P1,
// 3 +0x90 keyboard P2, 4 +0x94 mouse P1, 5 +0x98 mouse P2, 6 +0x9c joystick P1, 7 +0xa0 joystick P2,
// 8 +0xa4 easy P1, 9 +0xa8 easy P2, 10 +0xac medium P1, 11 +0xb0 medium P2, 12 +0xb4 hard P1, 13 +0xb8 hard P2.
// The creation ORDER matters: Button mode 9 groups are rings of siblings delimited by style 0x200.
int CNewGameDlg::OnInit() { // 0x402652
    CGameBase* g = Game();
    if (!g) return 0;
    scene_->LoadResource(kResSoundGroup1);
    scene_->LoadResource(kResSoundGroup2);
    deviceMask_ = g->GetInputDeviceMask();       // vtbl +0x128
    joystickCount_ = aaron::JoystickMgr::CountDevices();
    const u32 nP = g->numPlayers;                // scene+0x6b4 (byte)
    const bool joy = (deviceMask_ & 1) != 0, mouse = (deviceMask_ & 2) != 0, kb = (deviceMask_ & 4) != 0;
    const bool joyOk = joy && joystickCount_ != 0;

    AddButton(0x8b001d, 0x2b, 8, 1);                                           // OK
    AddButton(0x8b001e, 0x2c, 8, 0);                                           // cancel
    b_[0] = AddButton(0x8b001f, 0x1c, 0x208, 9);                               // one player
    b_[1] = AddButton(0x8b0020, 0x1d, 8, 9);                                   // two players
    b_[2] = AddButton(0x8b0017, 0x1e, (kb ? 0u : 2u) | 0x208, 9);              // keyboard P1
    b_[4] = AddButton(0x8b001b, 0x20, (mouse ? 0u : 2u) | 8, 9);               // mouse P1
    b_[6] = AddButton(0x8b0015, 0x22, (joyOk ? 0u : 2u) | 8, 9);               // joystick P1
    b_[8] = AddButton(0x8b0011, 0x24, 0x208, 9);                               // easy P1
    b_[10] = AddButton(0x8b0019, 0x26, 8, 9);                                  // medium P1
    b_[12] = AddButton(0x8b0013, 0x28, 8, 9);                                  // hard P1
    b_[9] = AddButton(0x8b0012, 0x25, (nP > 1 ? 0u : 2u) | 0x208, 9);         // easy P2
    b_[11] = AddButton(0x8b001a, 0x27, (nP > 1 ? 0u : 2u) | 8, 9);             // medium P2
    b_[13] = AddButton(0x8b0014, 0x29, (nP > 1 ? 0u : 2u) | 8, 9);             // hard P2
    b_[3] = AddButton(0x8b0018, 0x1f, ((nP < 2 || !kb) ? 2u : 0u) | 0x208, 9); // keyboard P2
    b_[5] = AddButton(0x8b001c, 0x21, ((nP < 2 || !mouse) ? 2u : 0u) | 8, 9);  // mouse P2
    b_[7] = AddButton(0x8b0016, 0x23, ((nP < 2 || !joy || joystickCount_ == 0) ? 2u : 0u) | 8, 9); // joystick P2
    for (Button* b : b_) if (!b) return 0; // (the original does not check: it would crash on the first use)

    b_[0]->SetToggled(nP == 1);
    b_[1]->SetToggled(nP == 2);
    auto player = [&](int i) -> CPlayer* {
        if (i >= g->players.Count()) return nullptr;
        return static_cast<CPlayer*>(static_cast<CPlayerBase*>(g->players.At(i)));
    };
    CPlayer* p0 = player(0);
    CPlayer* p1 = player(1);
    int d0 = p0 ? p0->GetDifficulty() : 0;         // vtbl +0x74
    b_[8]->SetToggled(d0 == 0);
    b_[10]->SetToggled(d0 == 1);
    b_[12]->SetToggled(d0 == 2);
    int d1 = p1 ? p1->GetDifficulty() : 0;
    b_[9]->SetToggled(d1 == 0);
    b_[11]->SetToggled(d1 == 1);
    b_[13]->SetToggled(d1 == 2);
    int dev0 = p0 ? p0->deviceType : 0;            // *(player + 4)
    if (dev0 == 3 && joystickCount_ == 0) {        // no joystick plugged: fall back
        if (kb) dev0 = 1;
        else if (mouse) dev0 = 2;
    }
    b_[2]->SetToggled(dev0 == 1);
    b_[6]->SetToggled(dev0 == 3);
    b_[4]->SetToggled(dev0 == 2);
    int dev1 = p1 ? p1->deviceType : 0;
    if (dev1 == 1) {
        if (!b_[2]->IsToggled() || g->simultaneous == 0) b_[3]->SetToggled(true);
        else if (!joyOk) { if (mouse) b_[5]->SetToggled(true); }
        else b_[7]->SetToggled(true);
    } else if (dev1 == 2) {
        if (!b_[4]->IsToggled() || g->simultaneous == 0) b_[5]->SetToggled(true);
        else if (!kb) { if (joy) b_[7]->SetToggled(true); }
        else b_[3]->SetToggled(true);
    } else if (dev1 == 3) {
        if ((!b_[6]->IsToggled() || g->simultaneous == 0) && joystickCount_ != 0) b_[7]->SetToggled(true);
        else if (!kb || b_[2]->IsToggled()) { if (mouse && !b_[4]->IsToggled()) b_[5]->SetToggled(true); }
        else b_[3]->SetToggled(true);
    }
    return 1;
}

// 0x4034df: copies the choices into the scene (number of players, difficulty and device of each player)
void CNewGameDlg::ApplyChoices() {
    CGameBase* g = Game();
    if (!g) return;
    g->SetNumPlayers((b_[1]->IsToggled() ? 1 : 0) + 1);          // vtbl +0x58
    struct Sel { int hard, mid, kb, mouse; };
    const Sel sel[2] = {{12, 10, 2, 4}, {13, 11, 3, 5}};
    for (int i = 0; i < 2; ++i) {
        if (i >= g->players.Count()) break;
        CPlayer* p = static_cast<CPlayer*>(static_cast<CPlayerBase*>(g->players.At(i)));
        if (!p) continue;
        p->SetDifficulty(b_[sel[i].hard]->IsToggled() ? 2 : (b_[sel[i].mid]->IsToggled() ? 1 : 0)); // vtbl +0x84
        p->SetDevice(b_[sel[i].kb]->IsToggled() ? 1 : (b_[sel[i].mouse]->IsToggled() ? 2 : 3));      // vtbl +0x6c
    }
}

// 0x4036ba
void CNewGameDlg::OnButton(u32 id, intptr_t) {
    CGameBase* g = Game();
    if (!g) return;
    const bool joyOk = (deviceMask_ & 1) != 0 && joystickCount_ != 0;
    const bool simultaneous = g->simultaneous != 0;
    switch (id) {
    case 0x1c: // one player: disable all the second player's controls
        if (b_[9]->DisableCount() == 0) {
            Sfx(kSndOneP);
            for (int i : {3, 5, 7, 9, 11, 13}) b_[i]->EnableCount(0);
        }
        break;
    case 0x1d: // two players
        if (b_[9]->DisableCount() != 0) {
            Sfx(kSndTwoP);
            b_[3]->EnableCount(int(deviceMask_ & 4));
            b_[5]->EnableCount(int(deviceMask_ & 2));
            b_[7]->EnableCount(joyOk ? 1 : 0);
            b_[9]->EnableCount(1);
            b_[11]->EnableCount(1);
            b_[13]->EnableCount(1);
        }
        break;
    // A device can be used by one player only when both play at the same time: choosing it for one player moves the
    // other player (if on the same device) to another available device.
    case 0x1e: // keyboard P1
        Sfx(kSndSelect);
        if (simultaneous && b_[3]->IsToggled()) {
            b_[3]->SetToggled(false);
            if (!joyOk) { if (deviceMask_ & 2) b_[5]->SetToggled(true); }
            else b_[7]->SetToggled(true);
        }
        break;
    case 0x1f: // keyboard P2
        Sfx(kSndSelect);
        if (simultaneous && b_[2]->IsToggled()) {
            b_[2]->SetToggled(false);
            if (!joyOk) { if (deviceMask_ & 2) b_[4]->SetToggled(true); }
            else b_[6]->SetToggled(true);
        }
        break;
    case 0x20: // mouse P1
        Sfx(kSndSelect);
        if (simultaneous && b_[5]->IsToggled()) {
            b_[5]->SetToggled(false);
            if (!(deviceMask_ & 4)) { if (joyOk) b_[7]->SetToggled(true); }
            else b_[3]->SetToggled(true);
        }
        break;
    case 0x21: // mouse P2
        Sfx(kSndSelect);
        if (simultaneous && b_[4]->IsToggled()) {
            b_[4]->SetToggled(false);
            if (!(deviceMask_ & 4)) { if (joyOk) b_[6]->SetToggled(true); }
            else b_[2]->SetToggled(true);
        }
        break;
    case 0x22: // joystick P1 (only when exactly one joystick exists)
        Sfx(kSndSelect);
        if (simultaneous && b_[7]->IsToggled() && joystickCount_ == 1) {
            b_[7]->SetToggled(false);
            if (!(deviceMask_ & 4)) { if (deviceMask_ & 2) b_[5]->SetToggled(true); }
            else b_[3]->SetToggled(true);
        }
        break;
    case 0x23: // joystick P2
        Sfx(kSndSelect);
        if (simultaneous && b_[6]->IsToggled() && joystickCount_ == 1) {
            b_[6]->SetToggled(false);
            if (!(deviceMask_ & 4)) { if (deviceMask_ & 2) b_[4]->SetToggled(true); }
            else b_[2]->SetToggled(true);
        }
        break;
    case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: // difficulty (radio buttons do the rest)
        Sfx(kSndDifficulty);
        break;
    case 0x2b: // OK
        Sfx(kSndOk);
        OnApply();      // vtbl +0x70
        Close(1, 1);
        break;
    case 0x2c: // cancel
        Sfx(kSndCancel);
        OnReset();      // vtbl +0x74 (nop)
        Close(2, 0);
        break;
    default: break;
    }
}

void CNewGameDlg::OnClose(int result) { // 0x403d9f
    scene_->FreeResource(kResSoundGroup1);
    scene_->FreeResource(kResSoundGroup2);
    if (result == 1) scene_->PostMessage(aaron::WMSG_COMMAND, CMD_NEWGAME, 0);
}

// 0x403e00.  QUIRK: closing with 0 (ESC) also starts the new game; result 2 (cancel) calls vtbl +0x60 = PostCommand.
void CNewGameDlg2::OnClose(int result) {
    scene_->FreeResource(kResSoundGroup1);
    scene_->FreeResource(kResSoundGroup2);
    if (result >= 0) {
        if (result < 2) scene_->PostMessage(aaron::WMSG_COMMAND, CMD_NEWGAME, 0);
        else if (result == 2) {
            if (CGameBase* g = Game()) g->PostCommand();
        }
    }
}

// ---------------------------------------------------------------------------------------------
// CAudioDlg
int CAudioDlg::OnInit() { // 0x407b29
    CGameBase* g = Game();
    if (!g) return 0;
    scene_->LoadResource(kResSoundGroup1);
    AddButton(0x8b0007, 0x2f, 8, 1);           // OK
    AddButton(0x8b0005, 0x30, 8, 0);           // cancel
    sfx_ = AddButton(0x8b0008, 0x2e, 8, 3);    // check box
    music_ = AddButton(0x8b0006, 0x2d, 8, 3);  // check box
    if (!sfx_ || !music_) return 0;
    sfx_->SetToggled(g->sfxMute == 0);
    music_->SetToggled(g->musicMute == 0);
    return 1;
}

void CAudioDlg::OnButton(u32 id, intptr_t) { // 0x407d6c
    if (id == 0x2f) {
        Sfx(kSndOk);
        OnApply();      // nop
        Close(1, 0);
    } else if (id == 0x30) {
        Sfx(kSndCancel);
        Close(2, 0);
    }
}

// 0x407df1: SetMute(sfx muted = effects box off, music muted = music box off)
void CAudioDlg::OnClose(int result) {
    if (result == 1) {
        if (CGameBase* g = Game()) g->SetMute(sfx_->IsToggled() ? 0 : 1, music_->IsToggled() ? 0 : 1);
    }
    scene_->FreeResource(kResSoundGroup1);
}

// ---------------------------------------------------------------------------------------------
// Confirmations
int CConfirmDlg::OnInit() { // 0x407e4f
    AddButton(0x8b0003, 0x31, 8, 1);   // yes (default)
    AddButton(0x8b0002, 0x32, 8, 0);   // no
    return 1;
}
void CConfirmDlg::OnButton(u32 id, intptr_t) { // 0x407f4a
    if (id == 0x31) OnApply();         // vtbl +0x70
    else if (id == 0x32) Close(0, 0);
}
// 0x407f87 (QUIRK: frees a resource it never loaded; harmless, the engine's refcount stops at 0)
void CConfirmDlg::OnClose(int) { scene_->FreeResource(kResSoundGroup1); }

void CQuitConfirmDlg::OnApply() { // 0x407fad
    scene_->PostMessage(aaron::WMSG_COMMAND, CMD_GAMEOVER_NAMES, 0);
    Close(0, 1);
}
void CQuitConfirmDlg::PostCmd1012() { // 0x407fe5 (no caller in the original)
    scene_->PostMessage(aaron::WMSG_COMMAND, CMD_NEWGAME, 0);
    Close(0, 1);
}

void CResetScoresConfirmDlg::OnApply() { // 0x40801d
    if (CArcadeGame* a = Arcade()) a->hsTable->Clear();
    Close(0, 0);
}
void CResetScoresConfirmDlg::OnClose(int) { // 0x408054
    if (CArcadeGame* a = Arcade()) a->hsTable->Show(1);
    scene_->FreeResource(kResSoundGroup1);
}

// 0x408090: overwrite the slot Save\<userArg>
void CSaveConfirmDlg::OnApply() {
    CGameBase* g = Game();
    if (g) {
        aaron::RegKey saves("Save", scene_->GetMachineKey());
        char name[16];
        std::snprintf(name, sizeof name, "%d", int(userArg_));
        aaron::RegKey slot(name, &saves);
        slot.DeleteKey();
        g->SerializeGlobals(&slot, 1);          // vtbl +0x114
    }
    scene_->PostMessage(aaron::WMSG_COMMAND, CMD_RESUME_MENU, 0);
    Close(1, 1);
}

// 0x4082d7: load the game stored in Save\<userArg>
void CRestoreConfirmDlg::OnApply() {
    CGameBase* g = Game();
    if (g) {
        if (g->ev0) g->ev0->Stop(0);
        if (g->running != 0) g->Pause();        // vtbl +0x8c
        g->ResetGame();                         // vtbl +0x80
        aaron::RegKey saves("Save", scene_->GetMachineKey());
        char name[16];
        std::snprintf(name, sizeof name, "%d", int(userArg_));
        aaron::RegKey slot(name, &saves);
        g->SerializeGlobals(&slot, 0);
    }
    scene_->PostMessage(aaron::WMSG_COMMAND, CMD_RESTORED, 0);
    Close(1, 1);
}

// ---------------------------------------------------------------------------------------------
// CPlayAgainDlg
int CPlayAgainDlg::OnInit() { // 0x408574
    scene_->LoadResource(kResSoundGroup1);
    AddButton(0x8b002e, 0x33, 8, 1);   // yes
    AddButton(0x8b002d, 0x34, 8, 0);   // no
    return 1;
}
void CPlayAgainDlg::OnButton(u32 id, intptr_t) { // 0x408686
    if (id == 0x33) {
        Sfx(kSndOk);
        Close(1, 1);
    } else if (id == 0x34) {
        Sfx(kSndCancel);
        Close(0, 1);
    }
}
void CPlayAgainDlg::OnClose(int result) { // 0x408700: the result goes to CArcadeGame::OnCommand(CMD_PLAYAGAIN_ANSWER)
    scene_->FreeResource(kResSoundGroup1);
    scene_->PostMessage(aaron::WMSG_COMMAND, CMD_PLAYAGAIN_ANSWER, result);
}

// ---------------------------------------------------------------------------------------------
// CCreditsScreen
// 0x4023b0 (the callback of the last queue message)
void CCreditsScreen::OnScrollDone(moses::CAnimObj*, uintptr_t popup, uintptr_t) {
    static_cast<CCreditsScreen*>(reinterpret_cast<Widget*>(popup))->Close(0, 0);
}

// 0x402444.  The queue (tables 0x4184f8..0x418548): Delay 500; Move(0, 240 - bounds.top, absolute); SetSpeed(20, 20); Show;
// LineTo(0, -(240 - bounds.top), 1 px per step, absolute); Delay 1000; Call(OnScrollDone).
int CCreditsScreen::OnInit() {
    if (!scene_->OpenArchive(kSlotCommonStrings, Config().commonStringsArchive, nullptr)) return 0;
    const char* script = static_cast<const char*>(scene_->LoadResource(0x65830015));
    text_ = Widget::Attach(new aaron::AnimTextSprite(WidgetParent(static_cast<Widget*>(this)), 0, 0, 0x7fffffff, 0, 1,
                                                     nullptr, nullptr, 0, 0));
    if (!script || !text_) {
        OnClose(0);         // vtbl +0x68 called directly (not Close: the popup stays alive, Create fails)
        return 0;
    }
    text_->moses::CDrawText::SetString(script, 1);
    const aaron::RECT* b = text_->moses::CDrawObj::GetBounds();
    int y0 = 0xf0 - b->top;
    msgs_[0] = moses::qm::Delay(500);
    msgs_[1] = moses::qm::Move(0, y0, 0);
    msgs_[2] = moses::qm::SetSpeed(20, 20);
    msgs_[3] = moses::qm::Show(1);
    msgs_[4] = moses::qm::LineTo(0, -y0, 1, 0, 1);
    msgs_[5] = moses::qm::Delay(1000);
    msgs_[6] = moses::qm::Call(&CCreditsScreen::OnScrollDone, reinterpret_cast<uintptr_t>(static_cast<Widget*>(this)), 0);
    for (int i = 0; i < 7; ++i) list_[i] = &msgs_[i];
    scene_->QStart(static_cast<moses::CAnimObj*>(text_), list_, 7);
    scene_->Cursor().ShowCursor(0);
    prevMinLevel_ = scene_->SetMinComposeLevel(moses::CDrawObj::GetLevel());
    return 1;
}

// 0x4023c4
void CCreditsScreen::OnClose(int) {
    scene_->OpenArchive(100, nullptr, nullptr);
    scene_->OpenArchive(0x65, nullptr, nullptr);
    scene_->SetMinComposeLevel(prevMinLevel_);
    scene_->Cursor().ShowCursor(1);
    if (userArg_ != 0) {
        if (aaron::Application* app = aaron::Application::GetApp()) app->Quit();
    }
}

// 0x402614.  QUIRK: the original calls Dialog::OnKey on the (just deleted) popup after Close; SPACE is not handled by it.
int CCreditsScreen::OnKey(u32 msg, u32 vk, u32 lParam) {
    if (msg == aaron::WMSG_KEYDOWN && vk == aaron::AVK_SPACE) {
        Close(0, 0);
        return 0;
    }
    return Dialog::OnKey(msg, vk, lParam);
}

} // namespace rapture
