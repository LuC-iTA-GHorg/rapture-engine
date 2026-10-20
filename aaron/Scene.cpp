// Scene: lifecycle, loop piece, message routing, input entry points. Package 3.
// Aaron Scene::Scene ord 49 0x1000c2db, Initialize ord 526 0x1000d363, Destroy ord 160 0x1000cac3, OnMessage ord 501 0x1000d14f,
// OnObjectEvent ord 507 0x1000d212, Paint ord 509 0x1000d24b, Resize ord 521 0x1000d317, per-frame service FUN_1000de81.
#include "aaron/Scene.h"

#include "aaron/Application.h"
#include "aaron/Window.h"

namespace aaron {

// Scene -> QM_POST / QM_SEND of the animation objects (port of the PostMessage/SendMessage on the view's HWND).
static void post_thunk(void* data, u32 msg, uintptr_t wParam, intptr_t lParam, bool send) {
    Scene* s = static_cast<Scene*>(data);
    if (send) s->OnMessage(msg, wParam, lParam);
    else s->PostMessage(msg, wParam, lParam);
}

Scene::Scene(Window* host, const char* name) : host_(host), name_(name ? name : "") { // ord 49 0x1000c2db
    origin_.x = origin_.y = 0;
    bgResId_ = kNoRes;
    overlayRes_ = kNoRes;
    SetPostProc(&post_thunk, this);
}

Scene::~Scene() { // ord 78 0x1000c3d3
    cursor_.DeleteCursors(); // the cursor sprites live in this view: they must go before the resources and the view itself
    delete bgSprite_;
    bgSprite_ = nullptr;
    if (treeRoot_) { // the widget tree: a parent owns its children
        treeRoot_->DeleteChildren();
        delete treeRoot_;
        treeRoot_ = nullptr;
        rootWidget_ = nullptr;
    }
    controllers_.clear();
    if (bgResId_ != kNoRes) FreeResource(bgResId_);
    if (overlayRes_ != kNoRes) FreeResource(overlayRes_);
    for (int i = 0; i < ResTable::kSlots; ++i) res_.Bind(u32(i), nullptr); // closes every slot
}

Application* Scene::GetApp() const { return Application::GetApp(); }

Widget* Scene::PickWidget(const POINT* pt) { return pick_.PickEnabled(this, pt->x, pt->y); } // ord 179 0x1000cbf2
void Scene::EnableCount(int enable) { if (rootWidget_) rootWidget_->EnableCount(enable); }   // ord 184 0x1000cc1a
int Scene::HideCount(int hide) { return rootWidget_ ? rootWidget_->HideCount(hide) : 0; }     // ord 532 0x1000dc96

void* Scene::GetResObject(u32 id) { // ord 199 0x1000ccd6 -> FUN_1000bbbb(table, id, 0)
    if (id == kNoRes) return nullptr;
    if (ResSlot(id) == 0xff) return DynamicResource(id);
    ResTable::Slot& s = res_.At(ResSlot(id));
    u32 i = ResIndex(id);
    return i < s.entries.size() ? s.entries[i].obj : nullptr;
}

void* Scene::GetIndexedResObject(u32 groupId, u32 n) { // ord 198 0x1000cc88
    if (groupId == kNoRes) return nullptr;
    u32 id = GetIndexedResId(groupId, n);
    return id == kNoRes ? nullptr : GetResObject(id);
}

u32 Scene::GetResType(u32 id) { // ord 338 0x1000cd6d -> FUN_1000be20
    if (id == kNoRes) return 0;
    ResArchive* a = res_.Archive(id);
    return a ? u32(a->GetResType(id)) : 0u;
}

static int node_is_not(TreeNode* n, void* target, void*) { return n != static_cast<TreeNode*>(target); } // FUN_1000cfc7

bool Scene::ContainsWidget(const TreeNode* w) { // ord 413 0x1000cf9c
    if (!rootWidget_) return false;
    return rootWidget_->ForEachFromHere(&node_is_not, const_cast<TreeNode*>(w), nullptr) == 0;
}

void Scene::ToScreen(POINT* pts, int n) const { // ord 440 0x1000d04d
    for (int i = 0; i < n; ++i) {
        pts[i].x += origin_.x;
        pts[i].y += origin_.y;
    }
}

void Scene::FromScreen(POINT* pts, int n) const { // ord 441 0x1000d09d
    for (int i = 0; i < n; ++i) {
        pts[i].x -= origin_.x;
        pts[i].y -= origin_.y;
    }
}

int Scene::MessageBox(u32 strId, u32 flags) { // ord 446 0x1000d0ed
    char buf[256];
    if (LoadString(strId, buf, 0xff) == 0) return 0;
    Application* app = Application::GetApp();
    return app ? app->MessageBox(buf, flags) : 0;
}

void Scene::PushAccelerators(bool passThrough) { accel_.Push(passThrough); } // ord 556 0x1000de5a
void Scene::PopAccelerators() { accel_.Pop(); }                                // ord 537 0x1000dde7

void Scene::SetRegistryPath(const char* path) { regKeyPath_ = path ? path : ""; } // ord 622 0x1000dfb8

int Scene::SetPaletteRes(u32 resId) { // ord 653 0x1000dfd9
    if (GetResType(resId) != 4) return 0; // RT_PALETTE
    FreeResource(paletteRes_);
    void* pal = LoadResource(resId);
    CDrawView::SetPalette(static_cast<const moses::CDibPal*>(pal)); // RT_PALETTE resources load as CDibPal (WP-5)
    if (CDrawView::RealizePalette()) MarkAllDirty();
    paletteRes_ = resId;
    return 1;
}

void Scene::ForgetWidget(Widget* w) { // 0x1000ca7d
    pick_.OnWidgetDestroyed(w);
    cursor_.OnWidgetDestroyed(w);
    focus_.OnWidgetDestroyed(w, this);
}

RegKey* Scene::GetUserKey() { // ord 392 0x1000ce39
    if (!userKey_) userKey_.reset(new RegKey(RegKey::HKCU, regKeyPath_.c_str()));
    return userKey_.get();
}

RegKey* Scene::GetMachineKey() { // ord 356 0x1000cda2
    if (!machineKey_) machineKey_.reset(new RegKey(RegKey::HKLM, regKeyPath_.c_str()));
    return machineKey_.get();
}

// ---------------------------------------------------------------------------------------------
// Scene::Initialize 0x1000d363. The managers are members (no allocation to fail); the order of the hooks is the original one:
//   OpenResources -> SetRedraw(0,1) -> Resize(w,h) -> OnPreCreate -> OnCreate -> SetCursor -> OnStart -> initialized -> Service
//   -> SetRedraw(1,..) -> OnReady.   Returns 1 on success.
int Scene::Initialize() {
    SetResProc(&Scene::ResProcThunk, this); // original passes the ResTable as data; the port needs the Scene (GetIndexedResId)
    SetPostProc(&post_thunk, this);

    // TreeRoot (bare node) + RootWidget (child of the root). ASSUMPTION for package 4: a Widget constructed with a Scene*
    // parent while Scene::RootWidget() is still null just records scene_ and stays unattached (this is how the root is made).
    treeRoot_ = new Node(WidgetParent(this), 0, 0);
    rootWidget_ = new Node(WidgetParent(static_cast<Widget*>(treeRoot_)), 0, 0);
    bgSprite_ = new moses::CDrawDib(this, 0x4000000); // +0x550: overlay/background sprite, always on top

    Application* app = Application::GetApp();
    regKeyPath_ = app ? std::string(app->Init().regKey) : std::string();
    regKeyPath_ += "\\";
    regKeyPath_ += name_;

    cursor_.SetCursor(1);
    if (!OpenResources()) return 0;
    SetRedraw(0, 1);
    POINT sz;
    if (host_) host_->GetClientSize(&sz);
    else { sz.x = 640; sz.y = 480; }
    Resize(sz.x, sz.y);
    if (!OnPreCreate()) return 0;
    if (!OnCreate()) return 0;
    if (!OnStart()) return 0;
    initialized_ = true;
    Service();
    SetRedraw(1, 1); // original: invalidate + UpdateWindow unless the client already fits the clip box; the port presents later
    OnReady();
    return 1;
}

void Scene::Destroy() { // 0x1000cac3 (the caller deletes)
    if (initialized_) {
        SetRedraw(0, 1);
        OnClose();
        initialized_ = false;
    }
}

// OpenResources 0x1000d2b7: opens "<module>.rrs" as archive 0. Scenes with a different layout override it.
// TODO: lenient (returns true even if the file is missing): Shoot's scenes open their archives in OnCreate.
bool Scene::OpenResources() {
    if (module_ && !module_->DllName().empty()) {
        std::string base = module_->DllName();
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base.resize(dot);
        OpenArchive(0, (base + ".rrs").c_str(), nullptr);
    }
    return true;
}

// slot 16, 0x1000d317: origin at the centre
void Scene::Resize(int w, int h) {
    origin_.x = w / 2;
    origin_.y = h / 2;
    CDrawView::Resize(u32(w), u32(h));
}

void Scene::Paint(void* hdc, const RECT* rc) { // 0x1000d24b: WM_PAINT; the port repaints everything on the next present
    (void)hdc; (void)rc;
    MarkAllDirty();
}

// 0x1000d212: the QM_NOTIFY of an animation object reaches the widget that owns it (CDrawObj id = Widget*).
// Faithful: calls the scene's slot 15 (OnObjectEvent2, a nop by default) with the widget and (a, b, c). It does NOT
// route to Widget::OnNotify; scenes that want it override OnObjectEvent2 (or OnObjectEvent).
void Scene::OnObjectEvent(moses::CAnimObj* obj, intptr_t a, intptr_t b, intptr_t c) {
    Widget* w = obj ? Widget::GetFromObj(obj->Target()) : nullptr;
    OnObjectEvent2(w, a, b, c);
}

void Scene::OnInput(u32 code, int x, int y, u32 user) { // slot 12, ord 492: default resets the idle timer
    (void)code; (void)x; (void)y; (void)user;
    idle_.Reset();
}

// ord 501 0x1000d14f
int Scene::OnMessage(u32 msg, uintptr_t wParam, intptr_t lParam) {
    switch (msg) {
    case AMSG_NOTIFY: { // 0xc06: deferred widget notification -> widget->OnNotify(id, widget)   (lParam = Widget*)
        Widget* w = reinterpret_cast<Widget*>(lParam);
        if (w) w->OnNotify(u32(wParam), w);
        return 1;
    }
    case WMSG_COMMAND: // 0x111
        return OnCommand(u32(wParam), uintptr_t(lParam));
    case AMSG_RESFREED: // 0xc01: a sound finished -> release the resource it holds (twice when lParam != 0, as the original)
        FreeResource(u32(wParam));
        if (lParam != 0) FreeResource(u32(wParam));
        return 1;
    case AMSG_SOUNDSTART: { // 0xc08 (lParam = listener)
        SceneListener* l = reinterpret_cast<SceneListener*>(lParam);
        if (l) l->OnInput(msg, wParam);
        return 1;
    }
    case AMSG_SOUNDEND: { // 0xc09
        SceneListener* l = reinterpret_cast<SceneListener*>(lParam);
        if (l) l->OnEnd();
        return 1;
    }
    default:
        return 0; // >= 0x1400: handled by the derived scenes
    }
}

void Scene::PostMessage(u32 msg, uintptr_t wParam, intptr_t lParam) { // ord 539 0x1000de09
    msgq_.Post(msg, wParam, lParam);
}

// FUN_1000de81 (+ IdleTimer, + CAudioLib::Service which the original ran on its own thread)
void Scene::RunFrame(u32 nowMs) {
    timers_.Tick(nowMs); // original: only when the TimerMgr has listeners (Tick handles it)
    Service();           // CAnimView::Service
    msgq_.Dispatch(this);
    idle_.Tick(this, nowMs);
    Application* app = Application::GetApp();
    if (app) app->Audio().Lib().Service();
}

void Scene::AddAccelerator(u32 vk, u32 cmd, u32 mod, u32 param) { accel_.Add(vk, cmd, mod, param); } // ord 601
void Scene::RemoveAccelerator(u32 vk, u32 mod) { accel_.Remove(vk, mod); }                            // ord 173

void Scene::SuspendControllers(bool suspend) {
    for (auto& c : controllers_) c->Suspend(suspend ? 0 : 1);
}

Controller* Scene::CreateController(u32 mode, u32 user) { // ord 167 0x1000cb33
    Controller* c = new Controller(this, mode, user);
    controllers_.emplace_back(c);
    return c;
}

// Mouse: coordinates are already scene-centred (Window subtracted the origin).
bool Scene::HandleMouse(u32 msg, int x, int y, u32 keys) {
    idle_.Reset();
    bool r = cursor_.OnMouse(this, msg, x, y, keys);
    for (auto& c : controllers_) c->OnMouse(msg, x, y, keys);
    if (msg == WMSG_LBUTTONUP || msg == WMSG_RBUTTONUP || msg == WMSG_MBUTTONUP) {
        std::vector<SceneListener*> snap = focus_.KeyListeners(); // Title scene: "click or key to skip" listeners
        for (SceneListener* l : snap) l->OnInput(msg, keys);
    }
    return r;
}

bool Scene::HandleKey(u32 msg, u32 vk, u32 mods) {
    idle_.Reset();
    bool consumed = false;
    if (msg == WMSG_KEYDOWN || msg == WMSG_KEYUP) {
        if (msg == WMSG_KEYDOWN && accel_.Translate(this, vk, mods)) consumed = true;
        if (msg == WMSG_KEYUP) { // AudioMgr::OnMessage 0x10011c33 acts on WM_KEYUP and does not consume the key
            Application* app = Application::GetApp();
            if (app) app->Audio().OnKey(vk, mods);
        }
        if (!consumed) consumed = focus_.OnKey(this, msg, vk, mods);
        if (!focus_.Locked()) {
            for (auto& c : controllers_) c->OnKey(vk, msg == WMSG_KEYDOWN);
        }
    } else {
        if (!focus_.Locked() && focus_.GetFocus()) consumed = focus_.GetFocus()->OnKey(msg, vk, mods) != 0;
    }
    return consumed;
}

// ---------------------------------------------------------------------------------------------
// moses ResProc (Aaron FUN_1000b470). mode 1 = load, 0 = release.
void* Scene::ResProcThunk(void* data, moses::CDrawObj* obj, int mode, u32* id) {
    return static_cast<Scene*>(data)->OnResProc(obj, mode, id);
}

void* Scene::OnResProc(moses::CDrawObj* obj, int mode, u32* id) {
    u32 v = *id;
    if (mode == 0) {
        if (v & kResDirect) FreeResource(v);   // (also the dynamic resources: slot 0xff)
        return nullptr;
    }
    if ((v & kResDirect) == 0) { // "group + index": member `id & 0xffff` of the group stored in the object's resid
        v = res_.GetIndexedResId(obj->GetResId(), v & 0xffff);
        if (v == kNoRes) return nullptr;
        *id = v;
    }
    return LoadResource(v);
}

} // namespace aaron
