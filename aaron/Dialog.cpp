// Dialog (Aaron.dll 0x10003620 .. 0x10003bcb). Package 4.
//
// Style policy bits (resolved from Dialog::Create 0x10003980 / FUN_10003702):
//   0x040000  do NOT hide the widgets under the parent
//   0x080000  do NOT disable (EnableCount) the widgets under the parent
//   0x100000  do NOT suspend the scene's Controllers
//   0x010000  do NOT stop wave sounds on open      0x020000  do NOT stop midi on open
//   0x200000 / 0x400000  on Close(.., notify == 0) re-show the enclosing dialog (0x400000: the outermost one)
//   0x800000  do NOT change the cursor on open     0x1000000 do NOT restore the cursor on close (original asymmetry kept)
#include "aaron/Scene.h"
#include "aaron/Sprites.h"

namespace aaron {

// Aaron 0x10003620 (ord 15): DibSprite_Frame(frame = -1), then zero the dialog fields, mode = +0x7c, userArg = +0x64.
Dialog::Dialog(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style, uintptr_t userArg, int mode)
    : DibSprite(parent, resId, x, y, level, cmdId, style, kNoRes) {
    userArg_ = userArg;
    mode_ = mode;
}

Dialog::~Dialog() {} // Aaron 0x1000368c

// Aaron 0x1000391f (FUN_1000391f)
Dialog* Dialog::FindDialogAncestor(bool outermost) {
    Dialog* found = nullptr;
    for (Widget* w = ParentWidget(); w; w = w->ParentWidget()) {
        if (w->GetPickFlags() == 0x80) {
            found = static_cast<Dialog*>(w);
            if (!outermost) break;
        }
    }
    return found;
}

// Aaron 0x10003980 (ord 477)
bool Dialog::Create() {
    Scene* sc = scene_;
    moses::CDrawView* view = sc;
    u32 st = style_;
    view->SetRedraw(0, 1);
    bool ok = Widget::Create() && moses::CDrawObj::Init() != 0;
    bool result = ok;
    if (ok) {
        Widget* parent = ParentWidget();
        if ((st & 0x40000) == 0) {
            if (parent) parent->HideCount(1);
            sc->Service();                       // CAnimView::Service
            HideCount(0);
        }
        if ((st & 0x80000) == 0) {
            if (parent) parent->EnableCount(0);
            EnableCount(1);
        }
        // FUN_1000cc39(scene, 0): suspend every Controller of the scene (a click on a dialog button must not fire the cannon).
        if ((st & 0x100000) == 0) sc->SuspendControllers(true);
        // FUN_1000de5a(scene, 0): AccelTable push (the dialog gets an empty table: the scene's ESC/SPACE/F-key accelerators are masked).
        sc->Accelerators().Push();
        if (sc->Cursor().Captured()) sc->Cursor().Capture(nullptr); // FUN_100136b2 = ReleaseCapture
        prevFocus_ = sc->Focus().GetFocus();     // FUN_10012f5a(this): becomes the focus owner, old one is returned
        if (disable_ == 0) sc->Focus().SetFocus(this);
        parentDialog_ = FindDialogAncestor(false);
        sc->StopSounds((st & 0x10000) == 0, (st & 0x20000) == 0, 0);
        if (st & 0x600000) {
            Dialog* anc = (st & 0x400000) == 0 ? parentDialog_ : FindDialogAncestor(true);
            if (anc) {
                anc->Show(false);
                Show(true);
            }
        }
        if ((st & 0x800000) == 0) {
            sc->Cursor().SetCursor(mode_);        // TODO(pkg3): returns void, the previous cursor (+0x80) is unknown
            prevCursor_ = 1;
        }
        moses::CDrawDib::Animate(resId_, nullptr);
        int r = OnInit();                         // slot 27
        result = (r != 0 && !closing_);
        created_ = true;
    }
    view->SetRedraw(1, 1);
    return result;
}

// Aaron 0x10003702 (FUN_10003702)
void Dialog::RestoreInput() {
    if (!created_) return;
    Scene* sc = scene_;
    u32 st = style_;
    Widget* parent = ParentWidget();
    if ((st & 0x40000) == 0 && parent) parent->HideCount(0);
    if ((st & 0x80000) == 0 && parent) parent->EnableCount(1);
    if ((st & 0x100000) == 0) sc->SuspendControllers(false); // FUN_1000cc39(scene, 1)
    sc->Accelerators().Pop();                                 // FUN_1000dde7
    sc->Focus().SetFocus(prevFocus_);
    if ((st & 0x1000000) == 0) sc->Cursor().SetCursor(prevCursor_);
}

// Aaron 0x100037d3 (ord 187). Deletes this dialog (and, with notify != 0, every enclosing dialog) synchronously.
void Dialog::Close(int result, int notify) {
    closing_ = true;
    Scene* sc = scene_;
    moses::CDrawView* view = sc;
    bool created = created_;
    if (created) view->SetRedraw(0, 1);
    OnClose(result);                             // slot 26
    Dialog* cur = this;
    Dialog* toShow = nullptr;
    if (notify == 0 && (style_ & 0x600000)) {
        toShow = (style_ & 0x400000) == 0 ? parentDialog_ : FindDialogAncestor(true);
    }
    do {
        cur->RestoreInput();
        Widget* anc = cur->ParentWidget();
        while (anc && anc->GetPickFlags() != 0x80) anc = anc->ParentWidget();
        delete cur;
        cur = static_cast<Dialog*>(anc);
    } while (notify != 0 && cur != nullptr);
    if (toShow) toShow->Show(true);              // vtable +0x24 (1, result, deleted-this)
    if (created) view->SetRedraw(1, 1);
}

// Aaron 0x10003bcb (ord 493). msg 0x100 = WM_KEYDOWN.
int Dialog::OnKey(u32 msg, u32 vk, u32 lParam) {
    (void)lParam;
    if (msg == WMSG_KEYDOWN) {
        if ((vk & 0xffff) == AVK_RETURN) {
            bool valid = false;
            if (defaultButton_) { // FUN_1000cf9c: is the pointer still in the scene tree?
                Widget* root = scene_ ? scene_->RootWidget() : nullptr;
                struct Finder {
                    static bool Find(TreeNode* n, TreeNode* t) {
                        for (TreeNode* c = n ? n->FirstChild() : nullptr; c; c = c->NextSibling())
                            if (c == t || Find(c, t)) return true;
                        return false;
                    }
                };
                valid = root && (root == defaultButton_ || Finder::Find(root, defaultButton_));
            }
            if (!valid) {
                defaultButton_ = nullptr;
            } else if (defaultButton_->DisableCount() == 0) {
                defaultButton_->PostNotify(1);
            }
            return 1;
        }
        if ((vk & 0xffff) == AVK_ESCAPE) {
            Close(0, 0);
            return 1;
        }
    }
    return 0;
}

} // namespace aaron
