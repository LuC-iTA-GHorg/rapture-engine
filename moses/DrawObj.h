// CDrawObj / CDrawDib: drawable objects living in a CDrawView (moses.md sec. 4).
//
// PORT DIFFERENCES (deliberate, see engine/README.md):
//  * The original CDrawObj has NO virtual destructor (vtable slot 0 is Copy()) and uses per-class
//    operator new/delete over malloc. The port uses a normal virtual destructor + plain new/delete.
//    `Copy()` stays virtual and is slot 0 *by name only*.
//  * The private block DRAWOBJ_PRIV (0x64 bytes, pointed to by CDrawObj+4) is a plain member `p_`.
//  * GetId()/SetId() are uintptr_t: Aaron stores the owning Widget* in it (Widget::GetFromObj).
//  * Hit-testing (IsPicked / PtInObject / IsContacting) and bounds are bit-exact with the original.
#pragma once

#include "moses/DibData.h"
#include "moses/Types.h"

namespace moses {

class CDrawObj;
class CDrawView;
class CAnimObj;
class CAnimDib;
class CAnimRect;

// moses.md sec. 4: DRAWOBJ_PRIV (0x64)
struct DRAWOBJ_PRIV {
    CDrawView* view = nullptr;       // +00 NULL => Init() fails
    // NOTE (pkg1, verified on 0x10006e80/0x10006f60/0x100070f0): the first version of these comments had them swapped.
    CDrawObj* prev = nullptr;        // +04 towards the HIGHEST level (view->head side); Display walks tail -> head via prev
    CDrawObj* next = nullptr;        // +08 towards the LOWEST level (view->tail side); hit tests walk head -> tail via next
    int level = 0;                   // +0c
    POINT pos;                       // +10 position of the hot-spot
    POINT pending;                   // +18 SetUpdatePos
    RECT bounds;                     // +20 current bounds in view coordinates (= get_bounds(flip) + pos)
    RECT oldbounds;                  // +30 bounds at the last draw
    RECT clip;                       // +40 per-object clip (if has_clip)
    u32 resid = 0;                   // +50 GetResId / SetResId (logical resource/group id)
    uintptr_t id = 0;                // +54 GetId / SetId (free caller id; Aaron: Widget*)
    u8 flipx = 0, flipy = 0;         // +58, +59
    u8 pickmode = 1;                 // +5a bit0 = never pickable, bit1 = bounding box is enough, bit2 = pickable even if hidden
    u8 has_pending = 0;              // +5b
    u8 has_clip = 0;                 // +5c
    u8 dirty_batch = 0;              // +5d
    u8 display_hook = 0;             // +5e
    u8 overlap_old = 0;              // +5f old bounds intersect new bounds
    u8 drawn = 0;                    // +60 already drawn at least once
    u8 scrollable = 0;               // +61
    u8 need_index = 0;               // +62 call update_index() before redrawing
    u8 visible = 0;                  // +63 a new CDrawObj is NOT visible until Show()/Animate()
};

class CDrawObj {
public:
    CDrawObj(CDrawView* view, int level);   // moses.dll 0x10007f30 (links into the view)
    CDrawObj(const CDrawObj& other);        // moses.dll 0x10007f90
    virtual ~CDrawObj();                    // moses.dll 0x10007fd0 (Hide(), unlink)

    // ---- virtual interface, same order as the original vtable (0x100165d8) --------------------
    virtual CDrawObj* Copy() const;                           // slot 0  0x10001780 base, CDrawDib 0x100088c0
    virtual void get_bounds(RECT& r, u32 flip);               // slot 1  0x100010b0 (base: empty) CDrawDib 0x100085b0
    virtual int update_index();                               // slot 2  0x10001500 (base returns 1)
    virtual void Compose(CDibSect* view, RECT& r);            // slot 3  0x10008420 (base: empty) CDrawDib 0x10008610
    virtual u32 GetPixel(int x, int y) const;                 // slot 4  0x100017c0 (base 0) CDrawDib 0x10008680
    virtual int InObject(int x, int y) const;                 // slot 5  0x100017d0 (base 0) CDrawDib 0x100086c0
    virtual void display_hook();                              // slot 6  0x100014f0 (empty) - run after Update() when SetDisplayHook(1)

    // ---- exported non-virtual API (moses.dll 0x10008000-0x10008410) -----------------------------
    int Init();                                               // 0x100080f0  = view && ok
    const RECT* GetBounds() const { return &p_.bounds; }      // 0x10008000
    void GetBoundsMid(POINT& pt) const;                       // 0x10008010  ((l+r)/2, (t+b)/2)
    u16 GetFlip() const { return u16(p_.flipx | (p_.flipy << 8)); } // 0x10008040
    uintptr_t GetId() const { return p_.id; }                 // 0x10008050
    void SetId(uintptr_t id) { p_.id = id; }                  // 0x10008330
    u32 GetResId() const { return p_.resid; }                 // 0x10008090
    void SetResId(u32 id) { p_.resid = id; }                  // 0x10008390
    int GetLevel() const { return p_.level; }                 // 0x10008060
    void SetLevel(int level);                                 // 0x10008340 -> CDrawView::SetLevel (re-links)
    u8 GetPickMode() const { return p_.pickmode; }            // 0x10008070
    void SetPickMode(u8 m) { p_.pickmode = m; }               // 0x10008360
    const POINT* GetPosition() const { return &p_.pos; }      // 0x10008080
    unsigned long GetType() const { return DRAWOBJ_TYPE; }    // 0x100080a0
    CDrawView* GetView() const { return p_.view; }            // 0x100080b0
    int IsVisible() const { return p_.visible; }              // 0x10008230
    int IsScrollable() const { return p_.scrollable; }        // 0x10008220
    void SetScrollable(u8 s) { p_.scrollable = s; }           // 0x100083a0
    void SetClipBox(int l, int t, int r, int b);              // 0x10008280
    void SetClipBox(const RECT* r);                           // 0x100082c0 (NULL = no clip)
    void SetDisplayHook(int on) { p_.display_hook = u8(on); } // 0x10008300

    // SetPosition(x, y, relative) -> set_pos_impl(x,y,relative,update=1). Only (x,y,relative) are
    // observable; the game passes 0 for `relative` almost always.
    void SetPosition(int x, int y, int relative = 0);         // 0x10008370
    // Stores the position as pending; applied (without redraw) at the next Update().
    void SetUpdatePos(int x, int y, int relative = 0);        // 0x100083b0
    // fx/fy: FlipArg (0/1 set, 2 toggle, 3 keep). Recomputes bounds and redraws if something changed.
    void SetFlip(u32 fx, u32 fy);                             // 0x10008310 -> set_flip_impl(update=1) 0x10007d40
    void Show();                                              // 0x10008400  visible=1; Update()
    void Hide();                                              // 0x100080c0  erase_old_area, visible=0, drawn=0
    void Update();                                            // 0x10008410 (jump to 0x10007b90): see moses.md sec. 4

    // Hit tests (bit-exact, moses.md sec. 4)
    int IsPicked(int x, int y) const;                         // 0x100081c0 (honours pickmode + visibility)
    int PtInObject(int x, int y) const;                       // 0x10008240 (bounds + InObject, ignores pickmode)
    int IsContacting(const CDrawObj* other) const;            // 0x10008110 pixel-perfect on the bounds intersection

protected:
    // internals, names from moses.md
    void set_pos_impl(int x, int y, int relative, int update);   // 0x10007dd0
    void set_flip_impl(u32 fx, u32 fy, int update);              // 0x10007d40
    void recalc_bounds();                                        // 0x10007af0 (old=bounds; get_bounds(flip)+pos)
    void erase_old_area(int excludeNew);                         // 0x10007cc0 (Display(oldbounds) with visible=0)
    void update_impl();                                          // 0x10007b90

    DRAWOBJ_PRIV p_;

    friend class CDrawView;
    friend class CAnimObj; // queue_stop / next_message clear need_index, SHOW/MOVE/FLIP use the *_impl functions
    friend class CAnimDib;
    friend class CAnimRect;
};

// ---------------------------------------------------------------------------------------------
// CDrawDib (0x24): draws a CDibObj. `Animate` loads bitmaps through the view's ResProc.
class CDrawDib : public CDrawObj {
public:
    CDrawDib(CDrawView* view, int level);   // moses.dll CDrawDib::CDrawDib(CDrawView*,int)
    CDrawDib(const CDrawDib& other);        // moses.dll CDrawDib::CDrawDib(CDrawDib*) - Shoot imports it
    ~CDrawDib() override;                   // moses.dll 0x10008780 (releases ids, deletes the list)

    CDrawObj* Copy() const override;                           // 0x100088c0  new CDrawDib(*this)
    void get_bounds(RECT& r, u32 flip) override;               // 0x100085b0 (empty rect when no dib)
    void Compose(CDibSect* view, RECT& r) override;            // 0x10008610  dib->DibBlt(view, r.left, r.top, r - pos, flip, DROP_TRANSPARENT)
    u32 GetPixel(int x, int y) const override;                 // 0x10008680  dib->GetPixel(x-pos.x, y-pos.y, flip)
    int InObject(int x, int y) const override;                 // 0x100086c0

    // Animate: for every id the view's ResProc is called with mode 1 (load); n == 1 -> that dib, else a CDibList
    // (up to 4). Old ids are released (mode 0). Then visible = 1, need_index = 0, Update().
    // `pos` (optional) is an ABSOLUTE position applied without update.
    void Animate(u32* ids, u32 n, POINT* pos = nullptr);       // 0x100087b0
    void Animate(u32 id, POINT* pos = nullptr);                // 0x10008810 - the one Shoot calls
    void AnimateDib(CDibObj* dib);                             // 0x10008830  direct bitmap, ids released
    u32 GetIds(u32* ids) const;                                // 0x10008880  returns count

protected:
    int load_ids(u32* ids, u32 n);                             // 0x10008470 (max 4)
    void build_list();                                         // CDibList of the loaded ids

    CDibObj* dib_ = nullptr;        // +08 current picture (borrowed from the resource manager)
    CDibList* list_ = nullptr;      // +0c composite (owned)
    u32 nids_ = 0;                  // +10
    u32 ids_[4] = {0, 0, 0, 0};     // +14
};

} // namespace moses
