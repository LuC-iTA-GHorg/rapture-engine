// G2 - popups of Shoot.exe, part 2: Save / Restore lists, CListHit, high score table, name entry.
#include "host/Popups.h"

#include <cstdio>

namespace rapture {

using aaron::Button;
using aaron::DibSprite;

// ---------------------------------------------------------------------------------------------
// CListHit (0x408746).  Derived from the rectangle sprite; invisible (pen NULL, hollow brush), only used to hit-test a grid.
CListHit::CListHit(CPopup* owner, WidgetParent parent, u32 cmd, const aaron::RECT* rc, int rows, int cols)
    : RectSprite(parent, 0, 0, 0x26fc, cmd, 0x71, rc, 0), owner_(owner), cmd_(cmd), left_(rc->left), top_(rc->top),
      right_(rc->right), bottom_(rc->bottom), rows_(rows), cols_(cols) {
    cellW_ = (right_ - left_) / cols;
    cellH_ = (bottom_ - top_) / rows;
    // QUIRK: the width is rounded up when the quotient is NONZERO (the height test uses the remainder): kept.
    if ((right_ - left_) / cols != 0) ++cellW_;
    if ((bottom_ - top_) % rows != 0) ++cellH_;
    moses::CDrawRect::pen_.style = 5;      // +0x6c = PS_NULL
    moses::CDrawRect::brush_.style = 1;    // +0x80 = BS_NULL
}

// 0x4088b0.  lParam = x | y << 16 (signed words, scene-centred).  Notifies the owner popup with (cmd, 1 = hover / 2 = click).
int CListHit::OnMouse(u32 msg, u32 wParam, intptr_t lParam) {
    if (msg == aaron::WMSG_LBUTTONDOWN || msg == aaron::WMSG_MOUSEMOVE) {
        int x = int(int16_t(lParam & 0xffff)), y = int(int16_t((lParam >> 16) & 0xffff));
        int cw = cellW_ ? cellW_ : 1, ch = cellH_ ? cellH_ : 1; // (the original would divide by zero)
        int idx = ((y - top_) / ch) * cols_ + (x - left_) / cw;
        if (msg != aaron::WMSG_MOUSEMOVE || idx != cell_) {
            cell_ = idx;
            owner_->OnButton(cmd_, msg != aaron::WMSG_MOUSEMOVE ? 2 : 1);
        }
    }
    return Widget::OnMouse(msg, wParam, lParam);
}

// ---------------------------------------------------------------------------------------------
// Save / Restore common part
CSlotDlg::~CSlotDlg() {}

void CSlotDlg::MoveHighlight(int slot) {
    if (hilite_ && lay_) hilite_->moses::CDrawObj::SetPosition(0, slot * lay_->pitch, 0);
}

// The OnInit loop of 0x4046fc / 0x4056e1.  The registry sub key Save\<i> holds GBCurPlayer and AGP<p>Level/Score/Difficulty
// of the game stored in slot i; level 0 = empty slot.
int CSlotDlg::BuildList(bool wantOccupied, int hitCmd) {
    CArcadeGame* a = Arcade();
    if (!a || !a->adef.listTable) return -2;
    lay_ = reinterpret_cast<const SlotListLayout*>(a->adef.listTable);   // an int table, NOT an HSTableDef (see README)
    const SlotListLayout& t = *lay_;
    int first = -1;
    WidgetParent me(static_cast<Widget*>(this));
    for (int i = 0; i < t.rows && i < 6; ++i) {
        bool occupied = true;
        aaron::RegKey saves("Save", scene_->GetMachineKey());
        char name[32];
        std::snprintf(name, sizeof name, "%d", i);
        aaron::RegKey slot(name, &saves);
        u32 cur = slot.GetDword("GBCurPlayer", 0);
        if (int(a->adef.spare22to25[3]) != -1) { // +0x774: digits of the slot number
            DisplayStyle st = {t.numDx, t.pitch, t.numX, t.numY0 + i * t.pitch, t.z, 0, 0, u32(t.numFlags), t.numDigits, 0, 0};
            num_[i] = new CDisplay(me, a->adef.spare22to25[3], st, false);
            num_[i]->DisplayNumber(i + 1, 0);
        }
        DisplayStyle ls = {t.levelDx, t.pitch, t.levelX, t.levelY0 + i * t.pitch, t.z, 0, 1, u32(t.levelFlags), t.levelDigits, 0, 0};
        level_[i] = new CDisplay(me, a->adef.spare22to25[1], ls, false);      // +0x76c
        std::snprintf(name, sizeof name, "AGP%iLevel", int(cur));
        u32 lvl = slot.GetDword(name, 0);
        if (lvl == 0) {
            occupied = false;
            if (!wantOccupied && first < 0) first = i;
        } else {
            level_[i]->DisplayNumber(int(lvl), 0);
            if (wantOccupied && first < 0) first = i;
        }
        DisplayStyle ss = {t.scoreDx, t.pitch, t.scoreX, t.scoreY0 + i * t.pitch, t.z, 0, 1, u32(t.scoreFlags), t.scoreDigits, 0, 0};
        score_[i] = new CDisplay(me, a->adef.spare22to25[0], ss, false);      // +0x768
        std::snprintf(name, sizeof name, "AGP%iScore", int(cur));
        u32 score = slot.GetDword(name, 0);
        if (occupied) score_[i]->DisplayNumber(int(score), 0);
        std::snprintf(name, sizeof name, "AGP%iDifficulty", int(cur));
        u32 diff = slot.GetDword(name, 0);
        icons_[i] = Widget::Attach(new DibSprite(me, kResDifficultyIcons, t.iconX, t.iconY0 + i * t.pitch, t.z, 0, 1));
        if (occupied && icons_[i]) icons_[i]->moses::CDrawDib::Animate(diff, nullptr);
    }
    aaron::RECT r1 = {t.rectLeft, t.rectTop, t.rectRight, t.rectTop + t.rows * t.pitch};
    hit_ = Widget::Attach(new CListHit(this, me, u32(hitCmd), &r1, 6, 1));
    aaron::RECT r2 = {t.rectLeft, t.rectTop, t.rectRight, t.rectTop + t.pitch};
    hilite_ = Widget::Attach(new aaron::RectSprite(me, 0, 0, 0x2706, 0, 0, &r2, 0));
    if (hilite_) {
        hilite_->moses::CDrawRect::pen_.style = 0;                         // PS_SOLID
        hilite_->moses::CDrawRect::pen_.color = u32(t.highlightColor);
        hilite_->moses::CDrawRect::SetEdgeWidth(5);
        hilite_->moses::CDrawRect::brush_.style = 1;                       // hollow
    }
    return first;
}

// ---------------------------------------------------------------------------------------------
// CSaveDlg
int CSaveDlg::OnInit() { // 0x4046fc
    if (!Arcade()) return 0;
    const SlotListLayout* l = nullptr;
    l = reinterpret_cast<const SlotListLayout*>(Arcade()->adef.listTable);
    if (!l) return 0;
    scene_->LoadResource(kResSoundGroup1);
    ok_ = AddButton(0x8b0034, 0x10, 0x208, 1);
    AddButton(0x8b0033, 0x11, 8, 0);
    Widget::Attach(new DibSprite(WidgetParent(static_cast<Widget*>(this)), 0x8b0035, 0, 0, 0x26fc, 0, 0, 0)); // "Save" label
    Widget::Attach(new DibSprite(WidgetParent(static_cast<Widget*>(this)), 0x8b0036, 0, 0, 0x26fc, 0, 0, 0));
    int first = BuildList(false, 0x1000);
    if (first < 0) return 1;      // (no empty slot: the selection stays on slot 0, the highlight at the top)
    sel_ = first;
    MoveHighlight(first);
    return 1;
}

// 0x405193
void CSaveDlg::DoSave() {
    if (!SlotOccupied(sel_)) {   // empty slot: save at once
        if (CGameBase* g = Game()) {
            aaron::RegKey saves("Save", scene_->GetMachineKey());
            char name[16];
            std::snprintf(name, sizeof name, "%d", sel_);
            aaron::RegKey slot(name, &saves);
            slot.DeleteKey();
            g->SerializeGlobals(&slot, 1);
        }
        scene_->PostMessage(aaron::WMSG_COMMAND, CMD_RESUME_MENU, 0);
        Close(1, 1);
    } else {                      // occupied: ask for confirmation
        Widget::Attach(new CSaveConfirmDlg(WidgetParent(static_cast<Widget*>(this)), uintptr_t(sel_)));
    }
}

// 0x4054d8
void CSaveDlg::OnButton(u32 id, intptr_t arg) {
    if (id == 0x10) {
        Sfx(kSndOk);
        OnApply();
    } else if (id == 0x11) {
        Sfx(kSndCancel);
        Close(2, 0);
    } else if (id == 0x1000 && arg == 2) {
        int cell = hit_ ? hit_->Cell() : 0;
        MoveHighlight(cell);
        sel_ = cell;
    }
}

void CSaveDlg::OnClose(int) { scene_->FreeResource(kResSoundGroup1); } // 0x4055af

// 0x4055d5: UP / DOWN move the selection (wrap on 6 rows)
int CSaveDlg::OnKey(u32 msg, u32 vk, u32 lParam) {
    if (msg == aaron::WMSG_KEYDOWN) {
        if (vk == aaron::AVK_UP) {
            sel_ = sel_ == 0 ? 5 : sel_ - 1;
            MoveHighlight(sel_);
            return 1;
        }
        if (vk == aaron::AVK_DOWN) {
            ++sel_;
            if (u32(sel_) > 5) sel_ = 0;
            MoveHighlight(sel_);
            return 1;
        }
    }
    return Dialog::OnKey(msg, vk, lParam);
}

// ---------------------------------------------------------------------------------------------
// CRestoreDlg
int CRestoreDlg::OnInit() { // 0x4056e1
    if (!Arcade() || !Arcade()->adef.listTable) return 0;
    scene_->LoadResource(kResSoundGroup1);
    ok_ = AddButton(0x8b0031, 0x18, 0x20a, 1);        // starts disabled (style 2)
    AddButton(0x8b0030, 0x19, 8, 0);
    Widget::Attach(new DibSprite(WidgetParent(static_cast<Widget*>(this)), 0x8b0035, 0, 0, 0x26fc, 0, 0, 0));
    Widget::Attach(new DibSprite(WidgetParent(static_cast<Widget*>(this)), 0x8b0036, 0, 0, 0x26fc, 0, 0, 0));
    int first = BuildList(true, 0x1000);
    if (first >= 0) {
        sel_ = first;
        if (ok_) ok_->EnableCount(1);
        MoveHighlight(first);
    }
    return 1;
}

// OK enabled only on an occupied slot (the repeated tests of 0x406527 / 0x4066be)
void CRestoreDlg::UpdateOk() {
    if (!ok_) return;
    bool occ = SlotOccupied(sel_);
    if (ok_->DisableCount() == 0 || !occ) {
        if (ok_->DisableCount() == 0 && !occ) ok_->EnableCount(0);
    } else {
        ok_->EnableCount(1);
    }
}

// 0x40618c.  QUIRK: the direct path is taken for an EMPTY slot (unreachable in practice: OK is disabled there); the occupied
// slot asks for confirmation.
void CRestoreDlg::DoRestore() {
    if (!SlotOccupied(sel_)) {
        if (CGameBase* g = Game()) {
            if (g->ev0) g->ev0->Stop(0);
            if (g->running != 0) g->Pause();
            g->ResetGame();
            aaron::RegKey saves("Save", scene_->GetMachineKey());
            char name[16];
            std::snprintf(name, sizeof name, "%d", sel_);
            aaron::RegKey slot(name, &saves);
            g->SerializeGlobals(&slot, 0);
        }
        scene_->PostMessage(aaron::WMSG_COMMAND, CMD_RESTORED, 0);
        Close(1, 1);
    } else {
        Widget::Attach(new CRestoreConfirmDlg(WidgetParent(static_cast<Widget*>(this)), uintptr_t(sel_)));
    }
}

void CRestoreDlg::OnButton(u32 id, intptr_t arg) { // 0x406527
    if (id == 0x18) {
        Sfx(kSndOk);
        OnApply();
    } else if (id == 0x19) {
        Sfx(kSndCancel);
        Close(2, 0);
    } else if (id == 0x1000 && arg == 2) {
        int cell = hit_ ? hit_->Cell() : 0;
        MoveHighlight(cell);
        sel_ = cell;
        UpdateOk();
    }
}

void CRestoreDlg::OnClose(int) { scene_->FreeResource(kResSoundGroup1); } // 0x406698

int CRestoreDlg::OnKey(u32 msg, u32 vk, u32 lParam) { // 0x4066be
    if (msg == aaron::WMSG_KEYDOWN) {
        if (vk == aaron::AVK_UP) {
            sel_ = sel_ == 0 ? 5 : sel_ - 1;
            MoveHighlight(sel_);
            UpdateOk();
            return 1;
        }
        if (vk == aaron::AVK_DOWN) {
            ++sel_;
            if (u32(sel_) > 5) sel_ = 0;
            MoveHighlight(sel_);
            UpdateOk();
            return 1;
        }
    }
    return Dialog::OnKey(msg, vk, lParam);
}

// ---------------------------------------------------------------------------------------------
// CHighScoreDlg
int CHighScoreDlg::OnInit() { // 0x4068f5
    CArcadeGame* a = Arcade();
    if (!a || !a->hsTable) return 0;
    table_ = a->hsTable;
    scene_->LoadResource(kResSoundGroup1);
    AddButton(0x8b000b, 0x1a, 8, 1);   // OK
    AddButton(0x8b000c, 0x1b, 8, 0);   // reset
    table_->Show(1);
    return 1;
}

void CHighScoreDlg::OnButton(u32 id, intptr_t) { // 0x406a2f
    if (id == 0x1a) {
        Sfx(kSndOk);
        Close(1, 0);
    } else if (id == 0x1b) {
        Sfx(kSndCancel);
        table_->Show(0);
        Widget::Attach(new CResetScoresConfirmDlg(WidgetParent(static_cast<Widget*>(this)), 0));
    }
}

// 0x406b64: only the outermost table popup tells the scene it is done (CMD_STATE4_NOW)
void CHighScoreDlg::OnClose(int) {
    scene_->FreeResource(kResSoundGroup1);
    if (table_) table_->Show(0);
    if (parentDialog_ == nullptr) scene_->PostMessage(aaron::WMSG_COMMAND, CMD_STATE4_NOW, 0);
}

// ---------------------------------------------------------------------------------------------
// CNameEntryDlg: a grid of letter frames (group 0x8b003a) + three initials + the cursors.
//   frames 0 .. n-4 letters, n-3 blank, n-2 / n-1 "move left / right in the 3 initials", n = the cursor frame.
int CNameEntryDlg::OnInit() { // 0x406bc7
    CArcadeGame* a = Arcade();
    rank_ = int(userArg_);
    WidgetParent me(static_cast<Widget*>(this));
    AddButton(0x8b000b, 0x1a, 8, 1);                                  // OK
    AddButton(0x8b000c, 0x1b, 8, 0);                                  // reset
    int t = PopupType();
    Widget::Attach(new Button(me, (t == POPUP_NAME_P1 || t == POPUP_NAME2_P1) ? 0x8b000e : 0x8b000f, 0, 0, 0x26fc, 0, 0, 0)); // Mushu icon P1 / P2
    scene_->LoadResource(kResSoundGroup1);
    initials_[0] = initials_[1] = initials_[2] = -1;
    slot_ = 0;
    n_ = 0;
    u32 idx;
    do {
        idx = u32(uint16_t(n_));
        ++n_;
    } while (scene_->GetIndexedResId(kResLetters, idx) != aaron::kNoRes);
    n_ -= 2;
    if (n_ < 0x21) { cols_ = 8; pitch_ = 0x32; }
    else { cols_ = 10; pitch_ = 0x28; }
    rows_ = (n_ - 1 + cols_) / cols_;
    aaron::RECT r;
    r.left = -0xaf - pitch_ / 2;
    r.top = -0x41 - pitch_ / 2;
    r.right = r.left + cols_ * pitch_;
    r.bottom = r.top + rows_ * pitch_;
    hit_ = Widget::Attach(new CListHit(this, me, 0x35, &r, rows_, cols_));
    for (int i = 0; i < n_; ++i)
        Widget::Attach(new DibSprite(me, kResLetters, (i % cols_) * pitch_ - 0xaf, (i / cols_) * pitch_ - 0x41, 0x26fc, 0, 0, u32(i)));
    for (int i = 0; i < 3; ++i)
        slotSprite_[i] = Widget::Attach(new DibSprite(me, kResLetters, i * pitch_ - (pitch_ * 2) / 2, -0x78, 0x26fc, 0, 1, 0u));
    cursor_ = Widget::Attach(new DibSprite(me, kResLetters, -0xaf, -0x41, 0x26fc, 0, 0, u32(n_)));
    slotCursor_ = Widget::Attach(new DibSprite(me, kResLetters, -((pitch_ * 2) / 2), -0x78, 0x26fc, 0, 0, u32(n_)));
    (void)a;
    return 1;
}

void CNameEntryDlg::PlaceCursor() {
    if (cursor_) cursor_->moses::CDrawObj::SetPosition((cell_ % cols_) * pitch_ - 0xaf, (cell_ / cols_) * pitch_ - 0x41, 0);
}
void CNameEntryDlg::PlaceSlotCursor() {
    if (slotCursor_) slotCursor_->moses::CDrawObj::SetPosition(slot_ * pitch_ - (pitch_ * 2) / 2, -0x78, 0);
}

// 0x40721e
void CNameEntryDlg::OnButton(u32 id, intptr_t arg) {
    if (id == 0x1a) {
        Sfx(kSndOk);
        OnApply();
        Close(1, 0);
    } else if (id == 0x1b) {
        Sfx(kSndCancel);
        OnReset();
    } else if (id == 0x35) {
        if (arg == 1) {
            int c = hit_ ? hit_->Cell() : -1;
            if (c > -1 && c < n_) {
                cell_ = c;
                PlaceCursor();
            }
        } else if (arg == 2) {
            PickLetter();
        }
    }
}

// 0x407359 "OnShow": the initials go to the table, the hit grid is released
void CNameEntryDlg::OnApply() {
    if (CArcadeGame* a = Arcade()) a->hsTable->SetInitials(initials_, rank_);
    scene_->FreeResource(kResSoundGroup1);
    delete hit_;
    hit_ = nullptr;
}

// 0x4073dd
void CNameEntryDlg::OnReset() {
    slot_ = 0;
    for (int i = 0; i < 3; ++i) {
        if (slotSprite_[i]) slotSprite_[i]->Show(false);
        initials_[i] = -1;
    }
    PlaceSlotCursor();
}

// 0x407481: any close but result 1 (ESC...) still stores what was typed.  Only the outermost popup posts the follow-up.
void CNameEntryDlg::OnClose(int result) {
    if (result != 1) OnApply();
    if (parentDialog_ == nullptr) scene_->PostMessage(aaron::WMSG_COMMAND, CMD_SHOW_HISCORES, 0);
}
// 0x407adc
void CNameEntryDlg2::OnClose(int result) {
    if (result != 1) OnApply();
    if (parentDialog_ == nullptr) scene_->PostMessage(aaron::WMSG_COMMAND, CMD_RESET_AND_MENU, 0);
}

// 0x407901
void CNameEntryDlg::PickLetter() {
    if (cell_ < n_ - 3 && slot_ < 3) {
        initials_[slot_] = cell_;
        DibSprite* s = slotSprite_[slot_];
        ++slot_;
        if (s) s->moses::CDrawDib::Animate(u32(cell_), nullptr);
    } else if (cell_ == n_ - 3 && slot_ < 3) {
        initials_[slot_] = -1;
        DibSprite* s = slotSprite_[slot_];
        ++slot_;
        if (s) s->Show(false);
    } else if (cell_ == n_ - 2 || cell_ == n_ - 1) {
        slot_ += ((cell_ != n_ - 2 ? 2 : 0) - 1);
    }
    if (slot_ < 0) slot_ = 2;
    if (slot_ > 2) slot_ = 0;
    PlaceSlotCursor();
}

// 0x4074ce.  Arrow keys move the grid cursor with wrap-around inside the row / column (the odd arithmetic is the original's).
int CNameEntryDlg::OnKey(u32 msg, u32 vk, u32 lParam) {
    if (msg != aaron::WMSG_KEYDOWN) return Dialog::OnKey(msg, vk, lParam);
    switch (vk) {
    case aaron::AVK_SPACE:
        PickLetter();
        return 1;
    case aaron::AVK_LEFT:
        if (cell_ % cols_ == 0) {
            int m = (cols_ - 1 < (n_ - cell_) - 1) ? cols_ : n_ - cell_;
            cell_ += m - 1;
        } else {
            --cell_;
        }
        PlaceCursor();
        return 1;
    case aaron::AVK_UP:
        if (cell_ < cols_) {
            cell_ += (rows_ - 1) * cols_;
            if (n_ <= cell_) cell_ -= cols_;
        } else {
            cell_ -= cols_;
        }
        PlaceCursor();
        return 1;
    case aaron::AVK_RIGHT:
        if (cell_ % cols_ == cols_ - 1 || cell_ == n_ - 1) {
            int m = (cols_ - 1 < cell_ % cols_) ? cols_ - 1 : cell_ % cols_;
            cell_ -= m;
        } else {
            ++cell_;
        }
        PlaceCursor();
        return 1;
    case aaron::AVK_DOWN:
        if (cell_ + cols_ < n_) cell_ += cols_;
        else cell_ = cell_ % cols_;
        PlaceCursor();
        return 1;
    default:
        return Dialog::OnKey(msg, vk, lParam);
    }
}

} // namespace rapture
