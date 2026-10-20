// WP-4 headless tests, group "widgets": Poly / Text sprites, ControlWin / EditControl model, RegKey additions, ResTable walkers,
// AccelTable::ForEach, HideCount pausing the animated sprites. Called from RunPkg4Tests. No window is ever created.
#include "aaron/Aaron.h"
#include "aaron/Application.h"
#include "aaron/ControlWin.h"
#include "aaron/Scene.h"
#include "aaron/Sprites.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using namespace aaron;

namespace {

int g_checks = 0, g_fail = 0;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        ++g_checks;                                                                      \
        if (!(c)) {                                                                      \
            ++g_fail;                                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                   \
        }                                                                                \
    } while (0)

class WScene : public Scene {
public:
    explicit WScene(aaron::Window* host) : Scene(host, "WScene") {}
    bool OpenResources() override { return true; }
};
Scene* CreateWScene(aaron::Window* host) { return new WScene(host); }

int countSlots(u32, void* a, void*) { ++*static_cast<int*>(a); return 1; }
int countRes(const ResTable::ResInfo*, void* a, void*) { ++*static_cast<int*>(a); return 1; }
int countAcc(const AccelTable::Entry* e, void* a, void*) { *static_cast<int*>(a) += int(e->cmd); return 1; }

void test_regkey() {
    std::puts("[wp4 regkey]");
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "wp4_test_reg";
    std::error_code ec;
    fs::remove_all(dir, ec);
    RegKey::SetIniPath((dir / "reg.ini").string());
    RegKey* root = RegKey::Create("Software\\Wp4", 0); // ord 12 (path, machine)
    CHECK(root->Path() == "HKCU\\Software\\Wp4");
    RegKey* mach = RegKey::Create("Software\\Wp4", 1);
    CHECK(mach->Path() == "HKLM\\Software\\Wp4");
    delete mach;
    root->SetString("Name", "Aaron");                  // ord 624
    char buf[16] = {0};
    CHECK(root->QueryString("Name", buf, sizeof buf, "dflt") == 6 && std::strcmp(buf, "Aaron") == 0); // ord 251: size incl. NUL
    CHECK(root->QueryString("Name", nullptr, 0, nullptr) == 6);                                       // size query
    char small[3] = {0};
    CHECK(root->QueryString("Name", small, sizeof small, "d") == 2 && std::strcmp(small, "d") == 0);  // does not fit: the default
    CHECK(root->QueryString("Missing", buf, sizeof buf, "dflt") == 5 && std::strcmp(buf, "dflt") == 0);
    CHECK(root->QueryString("Missing", buf, sizeof buf, nullptr) == 0);
    RegKey* sub = root->OpenSubKey("Child");           // ord 171
    CHECK(sub->Path() == "HKCU\\Software\\Wp4\\Child");
    sub->SetDword("x", 5);
    RegKey* subsub = sub->OpenSubKey("Leaf");
    subsub->SetDword("y", 1);
    // ord 175: removes the VALUE and the sub key of the same name (unless it has sub keys)
    root->SetDword("Child", 9);
    root->DeleteValue("Child");
    CHECK(root->GetDword("Child", 77) == 77);          // the value is gone
    CHECK(sub->GetDword("x", 0) == 5);                 // the sub key survives: it has a sub key itself
    sub->DeleteValue("Leaf");                          // Leaf has no sub keys: key AND value of that name go
    CHECK(subsub->GetDword("y", 0) == 0);
    root->DeleteValue("Child");                        // now Child has no sub keys left either
    CHECK(sub->GetDword("x", 0) == 0);                 // the key goes with its values (RegDeleteKey)
    delete subsub;
    delete sub;
    delete root;
    RegKey::FlushAll();
}

void test_widgets() {
    std::puts("[wp4 widgets]");
    Application app("Wid", static_cast<const char*>(nullptr), 0);
    RegisterScene("Wid.DLL", "CreateScene", &CreateWScene);
    Module* m = app.GetModule("Wid.DLL", "CreateScene", nullptr);
    CHECK(m && app.LoadSceneNow(m, 0));
    Scene* sc = app.GetScene();

    // ---- sprites
    POINT line[2] = {{0, 0}, {50, 40}};
    PolySprite* ps = static_cast<PolySprite*>(Widget::InitOrDelete(new PolySprite(sc, 10, 20, 0x2000, 7, 0, &line[0], &line[1])));
    CHECK(ps != nullptr);
    if (ps) {
        CHECK(ps->GetClassId() == CID_GDISPRITE && ps->GetDrawObj() != nullptr && ps->GetCmdId() == 7);
        CHECK(ps->npts_ == 2);
    }
    POINT tri[3] = {{0, 0}, {40, 0}, {20, 30}};
    PolySprite* pg = static_cast<PolySprite*>(Widget::InitOrDelete(new PolySprite(sc, 0, 0, 0x2000, 0, 0, tri, 3, 1)));
    CHECK(pg && pg->npts_ == 3 && pg->closed_ == 1);
    TextSprite* ts = static_cast<TextSprite*>(Widget::InitOrDelete(new TextSprite(sc, 0, 0, 0x2000, 0, 0x10000, "Hello", "Arial", 0, 0x00ff00)));
    CHECK(ts && ts->GetClassId() == CID_GDISPRITE && ts->GetDrawObj() != nullptr);
    AnimPolySprite* aps = static_cast<AnimPolySprite*>(Widget::InitOrDelete(new AnimPolySprite(sc, 0, 0, 0x2000, 0, 0, &line[0], &line[1])));
    CHECK(aps && aps->GetClassId() == CID_SPRITE && aps->GetAnimObj() != nullptr && aps->GetDrawObj() != nullptr);

    // HideCount pauses the animated sprites (slot 7 = CAnimObj::Pause) - and shows them again
    if (aps) {
        CHECK(!aps->GetAnimObj()->IsPaused());
        aps->HideCount(1);
        CHECK(aps->GetAnimObj()->IsPaused());
        aps->HideCount(0);
        CHECK(!aps->GetAnimObj()->IsPaused());
    }
    // the scene's HideCount (ord 532) hides everything below the root, then restores
    CHECK(sc->HideCount(1) >= 0);
    if (aps) CHECK(aps->GetAnimObj()->IsPaused());
    sc->HideCount(0);
    if (aps) CHECK(!aps->GetAnimObj()->IsPaused());

    // AnimSprite timing accessors (ord 353 / 664) write the CAnimObj fields directly
    AnimSprite* as = static_cast<AnimSprite*>(Widget::InitOrDelete(new AnimSprite(sc, kNoRes, 0, 0, 0x2000, 0, 0)));
    CHECK(as != nullptr);
    if (as) {
        as->SetTiming(33, 77);
        u32 sp = 0, mo = 0;
        as->GetTiming(&sp, &mo);
        CHECK(sp == 33 && mo == 77);
        CHECK(as->GetAnimObj() != nullptr);
    }

    // Timer: ord 525 posts AMSG_NOTIFY(cmdId, this) unconditionally
    Timer* tm = new Timer(sc, 0, 0, 0, 0); // cmdId 0
    tm->PostNotify(5);
    CHECK(!sc->Messages().Empty());
    sc->Messages().Clear();
    delete tm;

    // Node(TreeRoot*, Scene*): ord 33 / 48
    TreeRoot* holder = new TreeRoot(sc);
    Node* rn = new Node(holder, sc);
    CHECK(holder->Root() == rn && rn->Holder() == holder && holder->Owner() == sc);
    holder->SetRoot(nullptr);
    delete holder; // deletes the root node (it was attached under the scene root widget: the destructor unlinks it)

    // ---- ControlWin / EditControl (state model)
    EditControl* ed = new EditControl(sc, 0x200, "initial", 0x10000 /*ES_CENTER*/, 100, 50, 80, 20, 0x31, nullptr, 0x8000);
    CHECK(ed->GetClassId() == CID_CONTROLWIN && ed->NativeHandle() != nullptr && ed->GetHWnd() == ed->NativeHandle()); // ord 379 / 396
    CHECK(ed->GetCmdId() == 0x31);
    // (100,50) is the CENTRE in scene coordinates: + origin (320,240), minus half the size
    CHECK(ed->ScreenRect().left == 100 + 320 - 40 && ed->ScreenRect().top == 50 + 240 - 10 && ed->GetClientWidth() == 80 && ed->GetClientHeight() == 20);
    CHECK((ed->WinStyle() & 0x40000000u) != 0 && (ed->WinStyle() & 0x08000000u) == 0);
    CHECK(ed->HasParentFont() && (ed->GetStyle() & 0x100) != 0); // widgetStyle 0x8000 -> parent font; ES_CENTER -> style bit 0x100
    CHECK(Widget::InitOrDelete(ed) == ed);               // ord 486 Create: sub-classes and shows
    CHECK(ed->IsSubclassed() && ed->ControlVisible());
    char tbuf[16];
    ed->GetText(tbuf, sizeof tbuf);
    CHECK(std::strcmp(tbuf, "initial") == 0);
    ed->SetText("new text");                              // ord 666
    CHECK(ed->Text() == "new text");
    ed->GetText(tbuf, 4);                                 // ord 358: truncated to n - 1
    CHECK(std::strcmp(tbuf, "new") == 0);
    CHECK(ed->SendMessage(0xe, 0, 0) == 8);               // WM_GETTEXTLENGTH
    char gb[8];
    CHECK(ed->SendMessage(0xd, sizeof gb, reinterpret_cast<intptr_t>(gb)) == 7 && std::strcmp(gb, "new tex") == 0);
    ed->SetLimit(5);                                      // ord 669 EM_LIMITTEXT
    CHECK(ed->Limit() == 5);
    ed->SetText("abcdefgh");
    CHECK(ed->Text() == "abcde");
    ed->SetSel(1, 3);                                     // ord 589 EM_SETSEL
    CHECK(ed->SelStart() == 1 && ed->SelEnd() == 3);
    ed->SetSel(2, -1);                                    // to the end
    CHECK(ed->SelStart() == 2 && ed->SelEnd() == 5);
    ed->SetFont("Arial", 14);                             // ord 637
    CHECK(ed->FontFace() == "Arial" && ed->FontHeight() == 14);
    ed->SetTextColor(0x0000ff, 1);                        // ord 668
    ed->SetBackColor(0xff0000);                           // ord 605
    u32 tc = 0, bc = 0;
    int bk = 0;
    CHECK(ed->OnCtlColor(&tc, &bk, &bc) == 1 && tc == 0x0000ff && bk == 1 && bc == 0xff0000);
    ed->SetPos(0, 0, 60, 30);                             // ord 444: top-left (+ origin), not centred
    CHECK(ed->ScreenRect().left == 320 && ed->ScreenRect().top == 240 && ed->GetClientWidth() == 60);
    ed->Resize(70, 35);                                   // ord 690
    CHECK(ed->GetClientWidth() == 70 && ed->GetClientHeight() == 35);
    i32 cs[2];
    ed->GetClientSize(cs);                                // ord 347
    CHECK(cs[0] == 70 && cs[1] == 35);
    RECT cr = ed->GetClientRectScene();                   // ord 219: QUIRK - the origin is subtracted from the size, too
    CHECK(cr.left == -320 && cr.top == -240 && cr.right == 70 - 320 && cr.bottom == 35 - 240);
    CHECK(ed->HitTest(-300, -220) && !ed->HitTest(0, 0)); // ord 554
    ed->Show(false);                                      // ord 683
    CHECK(!ed->ControlVisible());
    ed->Show(true);
    CHECK(ed->ControlVisible());
    ed->PostMessage(0xa, 0, 0);                           // ord 540 WM_ENABLE(false)
    CHECK(!ed->ControlEnabled());
    // QUIRK (ord 467): EnableCount(0) disables the widget but calls slot 5 with 1 -> the control is ENABLED
    ed->EnableControl(false);
    ed->EnableCount(0);
    CHECK(ed->ControlEnabled());
    ed->EnableCount(1);
    CHECK(!ed->ControlEnabled());
    // PreProcess (sub-class proc): focus / enable
    ed->PreProcess(7, 0, 0);
    CHECK(sc->Focus().GetFocus() == ed);
    ed->PreProcess(8, 0, 0);
    CHECK(sc->Focus().GetFocus() == nullptr);
    ed->PreProcess(2, 0, 0); // WM_DESTROY
    CHECK(ed->NativeHandle() == nullptr && !ed->IsSubclassed());
    // a control without a window class fails to create
    ControlWin* bad = new ControlWin(sc, 0, "", "x", 0, 0, 0, 10, 10, 1, nullptr, 0);
    CHECK(bad->NativeHandle() == nullptr && Widget::InitOrDelete(bad) == nullptr);
    // ---- ResTable walkers / AccelTable::ForEach / class ids
    int n = 0;
    CHECK(sc->Resources().ForEachSlot(countSlots, &n, nullptr) == 1 && n == 0); // no archive bound
    n = 0;
    CHECK(sc->Resources().ForEachResource(countRes, 0, 255, 1, &n, nullptr) == 1 && n == 0);
    u32 la = 1, lb = 1;
    ResTable::GetLastLoad(&la, &lb);
    CHECK(la == 0 && lb == 0);
    ResTable::ResetLastLoad();
    sc->AddAccelerator('Q', 5, 0, 0);
    sc->AddAccelerator('W', 7, 0, 0);
    n = 0;
    CHECK(sc->Accelerators().ForEach(countAcc, &n, nullptr) == 1 && n == 12 && sc->Accelerators().Count() == 2);
    CHECK(kCopyright[0] == 'C' && std::strlen(kCopyright) == 50);

    // ---- AnimateList twins (ord 143 / 144): no ids -> nothing happens
    DibSprite* ds = static_cast<DibSprite*>(Widget::InitOrDelete(new DibSprite(sc, kNoRes, 0, 0, 0x2000, 0, 0)));
    if (ds) ds->AnimateList(0);
    if (as) as->AnimateList(0);
    CHECK(ds != nullptr);
}

} // namespace

int RunAaronWidgetTests(int* checks) {
    g_checks = g_fail = 0;
    test_regkey();
    test_widgets();
    std::printf("aaron-wp4 widgets: %d checks, %d failures\n", g_checks, g_fail);
    if (checks) *checks += g_checks;
    return g_fail;
}
