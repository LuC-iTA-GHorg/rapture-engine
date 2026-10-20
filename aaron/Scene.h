// Scene: one "screen" of the game (Shoot.exe: "Title Screen", Shoot.dll: the game scene). aaron.md sec. 2.4.
//
//   class Scene : CAnimView (-> CDrawView -> CDibSect -> CDibData)  +  RObject (class id 0x20082)
//
// A Scene IS the drawing/animation view: sprites are created with the scene (or a widget) as parent,
// their moses parts live in this view. The Scene owns the widget tree, the resource table, the
// message queue, the timers, the controllers and the accelerators.
//
// Virtual interface: 17 slots of the original vtable (0x1001e060), in the same order and with the
// same names (slots 1,2 are the inherited CDibData::DibBlt / GetPixel). Shoot's scene classes
// (CTitleScene, CGameBoard -> CArcadeGame -> CShootScene) override these.
//
//   slot  0 dtor          slot  9 OnClose        slot 13 OnMessage
//   slot  1 DibBlt (moses) slot 10 OnCommand     slot 14 Paint
//   slot  2 GetPixel(moses) slot 11 OnQuery      slot 15 OnObjectEvent2
//   slot  3 OnObjectEvent slot 12 OnInput        slot 16 Resize
//   slot  4 OnPreCreate   slot  5 OpenResources  slot 6 OnCreate  slot 7 OnStart  slot 8 OnReady
//
// Creation sequence (Scene::Initialize, Aaron 0x1000d363, ord 526): creates the managers and the widget tree
// roots, then calls OpenResources -> Resize(w,h) -> OnPreCreate -> OnCreate -> OnStart -> (one Service) -> OnReady.
#pragma once

#include "aaron/Input.h"
#include "aaron/Managers.h"
#include "aaron/Module.h"
#include "aaron/RObject.h"
#include "aaron/RegKey.h"
#include "aaron/ResArchive.h"
#include "aaron/Widget.h"
#include "moses/AnimView.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aaron {

class Application;
class Window;

class Scene : public moses::CAnimView, public RObject {
    friend class Module;
    friend class Window;
public:
    // Aaron ord 49 0x1000c2db: Scene(host, name). `host` = the Window/Application. Does not create anything yet:
    // Module::CreateScene calls CDrawView::Init and then Initialize().
    Scene(Window* host, const char* name);
    ~Scene() override;                                   // Aaron ord 78 0x1000c3d3, slot 0
    u32 GetObjClassId() const override { return CID_SCENE; } // Aaron ord 386 0x1000e350 (slot 1 of the Scene vtable: 0x20082)
    // (operator new/delete: Aaron ord 102 0x1000c2b9 / ord 121 0x1000c2ca -> global new/delete, see RObject)

    // ---- virtual interface (slots 3..16) ---------------------------------------------------------------
    // slot 3: override of CAnimView::OnNotify; routes QM_NOTIFY (a,b,c) to the widget owning `obj` (obj id = Widget*).
    void OnNotify(moses::CAnimObj* obj, intptr_t a, intptr_t b, intptr_t c) override { OnObjectEvent(obj, a, b, c); }
    // Aaron ord 507 0x1000d212. Default (faithful): widget = owner of `obj` (CDrawObj id); calls slot 15 OnObjectEvent2(widget, a, b, c) (a nop by default).
    // Shoot.dll's CShootScene::OnObjectEvent(obj, 0x1401, kind) is the game one (plays sounds by kind).
    virtual void OnObjectEvent(moses::CAnimObj* obj, intptr_t a, intptr_t b, intptr_t c);
    virtual bool OnPreCreate() { return true; }          // slot 4  Aaron ord 293 0x1000e360 (returns 1; the thunk is shared with ord 489/508/517/518)
    virtual bool OpenResources();                        // slot 5  Aaron ord 520 0x1000d2b7: opens "<module>.rrs" as archive 0
    virtual bool OnCreate() { return true; }             // slot 6  Aaron ord 489 0x1000e360 (returns 1): scenes build their widgets here
    virtual bool OnStart() { return true; }              // slot 7  Aaron ord 508 0x1000e360 (returns 1)
    // slot 8  Aaron ord 459 0x10003510 (nop virtual, same as Widget::NopVirtual2)
    virtual void OnReady() {}
    virtual void OnClose() {}                            // slot 9  Aaron ord 466 0x10003510 (nop)
    // slot 10: WMSG_COMMAND and non-propagated widget notifications. Return nonzero if handled.
    // `param` is uintptr_t (header change, pkg3, requested by pkg4): the original passes a Widget* for widget notifications.
    virtual int OnCommand(u32 id, uintptr_t param) { (void)id; (void)param; return 0; } // Aaron ord 461 0x10003540
    virtual int OnQuery(u32 what) { (void)what; return 0; } // slot 11  Aaron ord 475 0x1000e370 (returns 0). IdleTimer::SetTimeout calls it with the idle count
    // slot 12: Controller output. Default resets the IdleTimer. Aaron ord 492 0x1000e380.
    virtual void OnInput(u32 code, int x, int y, u32 user);
    // slot 13  Aaron ord 501 0x1000d14f: WMSG_COMMAND -> OnCommand(wp, lp); AMSG_NOTIFY -> widget->OnNotify; AMSG_SOUNDSTART/END -> listener.
    virtual int OnMessage(u32 msg, uintptr_t wParam, intptr_t lParam);
    // slot 14  Aaron ord 509 0x1000d24b: WM_PAINT. Port: repaints `rc` (nullptr = all) -> MarkAllDirty.
    virtual void Paint(void* hdc, const RECT* rc);
    virtual void OnObjectEvent2(Widget* w, intptr_t a, intptr_t b, intptr_t c) { (void)w; (void)a; (void)b; (void)c; }                     // slot 15 Aaron ord 519 0x1000e3a0 (nop) - NOTE CGameBoard reuses this vtable offset (0x3c) as OnSoundMessage
    // slot 16  Aaron ord 521 0x1000d317: origin at the centre; origin_ = (w/2, h/2) -> centred coordinates.
    virtual void Resize(int w, int h);

    // ---- resources (ResTable) -------------------------------------------------------------------------
    // Aaron ord 659 0x1000e08c: binds archive `path` to `slot` (ResArchive shared through Application, key = path).
    // path == nullptr closes the slot. baseDir may be null. Returns 1 on success.
    int OpenArchive(int slot, const char* path, const char* baseDir);
    // Aaron ord 138 0x1000ca5f (Scene::AddResource): forwards to FUN_1000b818 on the resource table (+0x68c): registers a
    // dynamic object in the first free entry of the table (refcount 1) and returns its id (index | 0xff800000), or 0xffffffff
    // when the table cannot be created/grown. The original owns the object (virtual dtor); the port keeps the pointer only.
    u32 AddResource(void* obj);
    void* DynamicResource(u32 id) const; // port helper (tests): the object registered with AddResource, nullptr if none
    void* LoadResource(u32 id);                          // Aaron ord 429 0x1000cfdb (refcount++; strings/blobs return the data pointer)
    void FreeResource(u32 id);                           // Aaron ord 207 0x1000cd0d
    int LoadString(u32 id, char* buf, u32 max);          // Aaron ord 430 0x1000d010
    u32 GetIndexedResId(u32 groupId, u32 n);             // Aaron ord 289 0x1000cd33 (n-th member of a group; Shoot: count frames)
    // Aaron ord 606 0x1000df00: background picture (resource id; 0 = none). Shoot: SetBackground(0x820000).
    void SetBackground(u32 resId);
    void SetSurfaceColor(u32 rawColor);                  // Aaron ord 613 0x1000df8d (Shoot: 0); hides CDrawView::SetSurfaceColor
    // Aaron ord 656 0x1000e055: overlay picture shown on top (Shoot passes def[5] = the "Pausa" sprite, mus 0x82005c).
    // TODO(package 3): semantics unverified (probably a picture drawn at the top level while paused).
    void SetOverlayRes(u32 resId);
    // Aaron 0x1000e0d3 (port: public): shows (AnimateDib of the overlay resource on the scene's top sprite) / hides the overlay.
    void ShowOverlay(int show);
    // port addition (pkg3): listener registered by PlaySoundSimple for resource `sndId` (the u32 user1 of PLAYSOUNDINFO cannot hold a
    // 64-bit pointer). remove=true erases the entry. Used by the application's sound callback to post AMSG_SOUNDSTART/END.
    SceneListener* SoundListenerFor(u32 sndId, bool remove);

    // ---- WP-4: the rest of the Scene API -----------------------------------------------------------------------------------
    // Aaron ord 179 0x1000cbf2: PickMgr::PickEnabled(pt->x, pt->y): the enabled widget under the point (scene coordinates), or null.
    Widget* PickWidget(const POINT* pt);
    // Aaron ord 184 0x1000cc1a: EnableCount on the root widget (0 disables the whole scene tree, 1 enables it again).
    void EnableCount(int enable);
    // Aaron ord 532 0x1000dc96: HideCount on the root widget (FUN_100041ad).
    int HideCount(int hide);
    // Aaron ord 199 0x1000ccd6: the object of a loaded resource WITHOUT touching its reference count (0 for -1 / not loaded). Dynamic ids
    // (AddResource) are looked up in the dynamic table.
    void* GetResObject(u32 id);
    // Aaron ord 198 0x1000cc88: GetResObject(GetIndexedResId(groupId, n)); 0 for groupId == -1 or an unresolved member.
    void* GetIndexedResObject(u32 groupId, u32 n);
    // Aaron ord 338 0x1000cd6d: the resource type (RESTYPE, aliases followed) of an id; 0 for -1 or an unbound slot.
    u32 GetResType(u32 id);
    // Aaron ord 413 0x1000cf9c: is `w` in the scene's widget tree? (walks the root widget with a "node != w" callback)
    bool ContainsWidget(const TreeNode* w);
    // Aaron ord 440 0x1000d04d / ord 441 0x1000d09d: scene (centred) coordinates <-> window ("screen") coordinates for `n` points.
    void ToScreen(POINT* pts, int n) const;
    void FromScreen(POINT* pts, int n) const;
    // Aaron ord 446 0x1000d0ed: LoadString(strId) then Application::MessageBox(text, flags); 0 if the string cannot be loaded.
    int MessageBox(u32 strId, u32 flags);
    // Aaron ord 536 0x1000dcb5 (FUN_1000dcb5): LoadResource(resId) + AudioMgr::SoundPlay(snd, info); the resource is released again on failure.
    // PlaySound / PlaySoundSimple build their PLAYSOUNDINFO and end here.
    int PlaySoundInfo(u32 resId, moses::PLAYSOUNDINFO* info);
    // Aaron ord 556 0x1000de5a / ord 537 0x1000dde7: push a new (empty) accelerator table on top of the current one (`passThrough`: keys it
    // does not know fall through to the table below) / pop it.
    void PushAccelerators(bool passThrough = false);
    void PopAccelerators();
    // Aaron ord 622 0x1000dfb8: lstrcpy(+0x564, path) - the key path used by the registry keys created LATER (existing ones keep theirs).
    void SetRegistryPath(const char* path);
    // Aaron ord 653 0x1000dfd9: if resource `resId` is a palette (type 4): free the previous palette resource, load this one, CDrawView::SetPalette
    // + RealizePalette (8-bit only: no-ops here), remember the id (+0x544); returns 1. Any other type: returns 0.
    int SetPaletteRes(u32 resId);
    u32 PaletteRes() const { return paletteRes_; }
    // Aaron 0x1000ca7d (FUN_1000ca7d): a widget is being destroyed: PickMgr (ord 152), CursorMgr (ord 154) and FocusMgr (ord 153) drop it.
    void ForgetWidget(Widget* w);
    // Aaron ord 202 0x1000b797 (the dynamic archive's Free callback): releases dynamic resource `id` (the entry becomes free). If the object
    // was registered with a deleter (AddResource overload) it is deleted - the original always runs the object's virtual destructor.
    u32 AddResource(void* obj, void (*deleter)(void*));
    void FreeDynamicResource(u32 id);
    void* LoadDynamicResource(u32 id);    // port: slot 0xff ids: refcount + 1 (the original goes through the slot's CResMgr, FUN_1000b6d3)
    void ReleaseDynamicResource(u32 id);  // ResTable::Release 0x1000bcdd for slot 0xff
    // ---- audio ---------------------------------------------------------------------------------------------
    // Aaron ord 534 0x1000dd15 Scene::PlaySound(resId, a, pan, volume, b, user0, user1) -> sound handle (0 = failed)
    // Compiles a PLAYSOUNDINFO{size=0x18,...}. Call sites: PlaySound(0x1850000,1,0,100,0,0,0) (music) and
    // PlaySound(snd,0,0,100,isLoop,0,0) (sfx; isLoop for CGameBase commands 0x0F/0x10).
    // TODO: a = ? (1 for the MIDI; maybe "mix"/"loop"), b = loop. Pan is the x position (GetBoundsMid) of the emitter.
    int PlaySound(u32 resId, int a, int pan, int volume, int b, int user0, int user1);
    // Aaron ord 535 0x1000dd9a PlaySoundSimple(resId, flags, listener): AVI / sound with end notification.
    // listener->OnEnd() is called when it ends or is stopped (Title: dilogo.avi / ddlogo.avi). Returns nonzero on success.
    int PlaySoundSimple(u32 resId, int flags, SceneListener* listener);
    void StopSounds(int waves, int midi, int cd);        // Aaron ord 693 0x1000e131 (Shoot: StopSounds(sfxMute, musicMute, 0))
    void StopSound(u32 resId);                           // Aaron ord 694 0x1000e162

    // ---- messages / input ------------------------------------------------------------------------------------
    // Aaron ord 539 0x1000de09: queue a message for the NEXT dispatch (never re-entrant).
    // Shoot: PostMessage(0x111, 0xf0001012, 0) = scene command.
    void PostMessage(u32 msg, uintptr_t wParam, intptr_t lParam);
    // Aaron ord 601 0x1000ded6 AddAccelerator(vk, cmd, mod, param): WM_KEYDOWN(vk with modifiers mod) -> PostMessage(0x111, cmd, param).
    void AddAccelerator(u32 vk, u32 cmd, u32 mod, u32 param);
    void RemoveAccelerator(u32 vk, u32 mod);             // Aaron ord 173 0x1000cbab
    // Aaron ord 167 0x1000cb33 CreateController(mode, user) -> Controller* (owned by the scene)
    Controller* CreateController(u32 mode, u32 user);
    // Aaron ord 186 0x1000cc39 (Dialog::Create/Close): suspend (true) / resume (false) every controller of the scene (Controller::Suspend(0)/(1)).
    void SuspendControllers(bool suspend);
    // Platform events, already translated by aaron::Window (coordinates: scene-centred for the mouse).
    bool HandleMouse(u32 msg, int x, int y, u32 keys);   // CursorMgr::OnMouse (+ Controllers)
    bool HandleKey(u32 msg, u32 vk, u32 mods);           // AccelTable, FocusMgr, AudioMgr keys, Controllers

    // ---- registry --------------------------------------------------------------------------------------------
    RegKey* GetUserKey();                                // Aaron ord 392 (HKCU, scene key +0x564)
    RegKey* GetMachineKey();                             // Aaron ord 356 0x1000cda2 (HKLM)

    // ---- lifecycle (called by Window / Module, not by game code) ---------------------------------------------
    int Initialize();                                    // Aaron ord 526 0x1000d363
    void Destroy();                                      // Aaron ord 160 0x1000cac3
    // One main-loop iteration for this scene (port): dispatch queued messages, TimerMgr tick, IdleTimer,
    // CAnimView::Service(), audio Service. Called by Application::RunFrame.
    void RunFrame(u32 nowMs);

    // ---- accessors ---------------------------------------------------------------------------------------------
    Window* Host() const { return host_; }
    Application* GetApp() const;
    const std::string& Name() const { return name_; }
    Module* GetModule() const { return module_; }
    uintptr_t Arg() const { return arg_; }
    Node* TreeRootNode() const { return treeRoot_; }
    Widget* RootWidget() const { return rootWidget_; }   // +0x53c: parent of the widgets given a Scene* parent
    const POINT& Origin() const { return origin_; }      // (w/2, h/2)
    CursorMgr& Cursor() { return cursor_; }
    FocusMgr& Focus() { return focus_; }
    PickMgr& Picker() { return pick_; }
    IdleTimer& Idle() { return idle_; }
    TimerMgr& Timers() { return timers_; }
    AccelTable& Accelerators() { return accel_; }
    MsgQueue& Messages() { return msgq_; }
    ResTable& Resources() { return res_; }
    bool Initialized() const { return initialized_; }

    // moses ResProc for this view (Aaron FUN_1000b470): mode 1 load / 0 release; ids without kResDirect are
    // resolved with GetIndexedResId(obj->GetResId(), id & 0xffff). Installed with SetResProc in Initialize().
    static void* ResProcThunk(void* data, moses::CDrawObj* obj, int mode, u32* id);
    void* OnResProc(moses::CDrawObj* obj, int mode, u32* id);

protected:
    Window* host_ = nullptr;                // +0x534
    std::string name_;                      // +0x4a4
    Module* module_ = nullptr;              // +0x524
    uintptr_t arg_ = 0;                     // +0x530
    Node* treeRoot_ = nullptr;              // +0x538
    Widget* rootWidget_ = nullptr;          // +0x53c
    u32 surfaceColor_ = 0;                  // +0x540
    u32 bgResId_ = 0;                       // +0x548
    moses::CDrawDib* bgSprite_ = nullptr;   // +0x550
    u32 overlayRes_ = 0;                    // +0x54c (TODO)
    POINT origin_;                          // +0x554/558
    bool initialized_ = false;              // +0x55c
    u32 paletteRes_ = kNoRes;               // +0x544 (palette resource id)
    std::string regKeyPath_;                // +0x564
    std::unique_ptr<RegKey> userKey_, machineKey_; // +0x664 / +0x668
    FocusMgr focus_;                        // +0x66c
    CursorMgr cursor_;                      // +0x670
    PickMgr pick_;                          // +0x674
    IdleTimer idle_;                        // +0x678
    TimerMgr timers_;                       // +0x67c
    AccelTable accel_;                      // +0x680
    MsgQueue msgq_;                         // +0x684 (+0x688 second buffer folded into MsgQueue)
    ResTable res_;                          // +0x68c
    struct DynEntry { void* obj = nullptr; u32 refcount = 0; void (*deleter)(void*) = nullptr; };
    std::vector<DynEntry> dyn_;             // original: table at +0xbf8 (8-byte entries), created on first AddResource
    std::vector<std::unique_ptr<Controller>> controllers_; // +0x69c
    std::vector<u32> playing_;              // resource ids of sounds started by PlaySound (for StopSound)
    std::vector<std::pair<u32, SceneListener*>> soundListeners_; // port: PlaySoundSimple listeners by resource id
};

} // namespace aaron
