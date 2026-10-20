// DibSprite, AnimSprite, RectSprite, AnimRectSprite, AnimTextSprite (Aaron.dll 0x10004c7c .. 0x10005ade). Package 4.
#include "aaron/Scene.h"
#include "aaron/Sprites.h"
#include <cstdarg>
#include <vector>

namespace aaron {

static moses::CDrawView* ViewOf(Scene* s) { return s; }
static moses::CAnimView* AnimViewOf(Scene* s) { return s; }

// ---------------------------------------------------------------------------------------------
// DibSprite. Aaron 0x100055a4 (ord 16)
DibSprite::DibSprite(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style)
    : Widget(parent, resId, x, y, level, cmdId, style), moses::CDrawDib(ViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawDib*>(this), style);
}

// Aaron 0x10005647 (ord 17): + Animate(frame) when frame != -1.
// (the frame is a member index of the group `resId`, resolved by Scene::ResProc through GetIndexedResId.)
DibSprite::DibSprite(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style, u32 frame)
    : Widget(parent, resId, x, y, level, cmdId, style), moses::CDrawDib(ViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawDib*>(this), style);
    frame_ = frame;
    if (frame != kNoRes) moses::CDrawDib::Animate(frame, nullptr);
}

// Aaron ord 143 0x10005702
void DibSprite::AnimateList(u32 count, ...) {
    std::vector<u32> ids(count);
    va_list ap;
    va_start(ap, count);
    for (u32 i = 0; i < count; ++i) ids[i] = va_arg(ap, u32);
    va_end(ap);
    if (count) moses::CDrawDib::Animate(ids.data(), count, nullptr);
}

// Aaron ord 144 0x10004ed2
void AnimSprite::AnimateList(u32 count, ...) {
    std::vector<u32> ids(count);
    va_list ap;
    va_start(ap, count);
    for (u32 i = 0; i < count; ++i) ids[i] = va_arg(ap, u32);
    va_end(ap);
    if (count) moses::CDrawDib::Animate(ids.data(), count, nullptr);
}

DibSprite::~DibSprite() {} // ~CDrawDib then ~Widget

// TODO(pkg4): the original DibSprite vtable (0x1001da34) is not decodable from the decompilation. The picture of a
// frame-less sprite is shown by Create (Animate(resId), as Dialog::Create 0x10003980 does explicitly).
bool DibSprite::Create() {
    if (!Widget::Create() || !moses::CDrawObj::Init()) return false;
    if (frame_ == kNoRes && resId_ != kNoRes) {
        moses::CDrawDib::Animate(resId_, nullptr);
        if (style_ & 1) moses::CDrawObj::Hide();
    }
    return true;
}

void DibSprite::Show(bool show) { Widget::Show(show); }
void DibSprite::ReleaseRes() { Widget::ReleaseRes(); }
bool DibSprite::LoadRes() { return Widget::LoadRes(); }
bool DibSprite::HitTest(int x, int y) { return moses::CDrawObj::IsPicked(x, y) != 0; } // 0x10003cd0

void DibSprite::SetFrame(u32 frame) {
    frame_ = frame;
    if (frame != kNoRes) moses::CDrawDib::Animate(frame, nullptr);
}

// ---------------------------------------------------------------------------------------------
// AnimSprite. Aaron 0x10004c7c (ord 22)
AnimSprite::AnimSprite(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style)
    : Widget(parent, resId, x, y, level, cmdId, style), moses::CAnimDib(AnimViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawDib*>(this), style);
}

// Aaron 0x10004e07 (ord 23)
AnimSprite::AnimSprite(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style, u32 frame)
    : Widget(parent, resId, x, y, level, cmdId, style), moses::CAnimDib(AnimViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawDib*>(this), style);
    if (frame != kNoRes) moses::CDrawDib::Animate(frame, nullptr);
}

// Aaron 0x10004d2c (ord 24)
AnimSprite::AnimSprite(WidgetParent parent, u32 resId, int x, int y, int level, u32 cmdId, u32 style,
                       moses::QUEUEMSG** msgs, u32 count)
    : Widget(parent, resId, x, y, level, cmdId, style), moses::CAnimDib(AnimViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawDib*>(this), style);
    if ((style & 1) == 0) AnimViewOf(scene_)->QStart(static_cast<moses::CAnimObj*>(this), msgs, count);
}

AnimSprite::~AnimSprite() {}

bool AnimSprite::Create() { return Widget::Create() && moses::CDrawObj::Init() != 0; }

// Aaron 0x100045c3 (ord 142)
void AnimSprite::Show(bool show) {
    if (!show) {
        moses::CAnimObj* a = GetAnimObj();
        a->GetAnimView()->QStop(a);
    }
    Widget::Show(show);
}

void AnimSprite::ReleaseRes() { Widget::ReleaseRes(); }
bool AnimSprite::LoadRes() { return Widget::LoadRes(); }
bool AnimSprite::HitTest(int x, int y) { return moses::CDrawObj::IsPicked(x, y) != 0; }

// Aaron 0x10004fb3 (ord 696). NOTE (original quirk kept): the resource is only released when stopFirst != 0, but it is
// replaced in either case.
void AnimSprite::Play(u32 resId, moses::QUEUEMSG** msgs, u32 count, int stopFirst) {
    if (resId_ != resId) {
        if (stopFirst != 0) ReleaseRes();
        resId_ = resId;
        if (!LoadRes()) return;
        moses::CDrawObj::SetResId(resId);
    }
    GetAnimObj()->GetAnimView()->QStart(GetAnimObj(), msgs, count);
}

// ---------------------------------------------------------------------------------------------
// RectSprite. Aaron 0x100059e5 (ord 20)
RectSprite::RectSprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, const RECT* rc, int ellipse)
    : Widget(parent, kNoRes, x, y, level, cmdId, style), moses::CDrawRect(ViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawRect*>(this), style);
    // +0x7c / +0x68 = scene+0x694 / +0x698 (default colours of the scene): TODO(pkg3) Scene has no such fields.
    moses::CDrawRect::Init(rc, ellipse);
}
RectSprite::~RectSprite() {}
bool RectSprite::Create() { return Widget::Create() && moses::CDrawObj::Init() != 0; }
void RectSprite::Show(bool show) { Widget::Show(show); }
bool RectSprite::HitTest(int x, int y) { return moses::CDrawObj::IsPicked(x, y) != 0; }

// AnimRectSprite. Aaron 0x100052ac (ord 27)
AnimRectSprite::AnimRectSprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, const RECT* rc, int ellipse)
    : Widget(parent, kNoRes, x, y, level, cmdId, style), moses::CAnimRect(AnimViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawRect*>(this), style);
    moses::CDrawRect::Init(rc, ellipse);
}
AnimRectSprite::~AnimRectSprite() {}
bool AnimRectSprite::Create() { return Widget::Create() && moses::CDrawObj::Init() != 0; }
void AnimRectSprite::Show(bool show) { Widget::Show(show); }

// AnimTextSprite. Aaron 0x100053ed (ord 28)
AnimTextSprite::AnimTextSprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, const char* text,
                               const char* fontFace, int fontHeight, moses::COLORREF color)
    : Widget(parent, kNoRes, x, y, level, cmdId, style), moses::CAnimText(AnimViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawText*>(this), style);
    if (fontFace) moses::CDrawText::SetFontInfo(fontFace, fontHeight == 0 ? 12 : fontHeight);
    moses::CDrawText::SetTextColor(color);
    if ((style_ & 0x10000) == 0) moses::CDrawText::SetTextAlign((style_ & 0x20000) == 0 ? 1 : 2);
    else moses::CDrawText::SetTextAlign(0);
    moses::CDrawText::Init(text ? text : "");
}
AnimTextSprite::~AnimTextSprite() {}
bool AnimTextSprite::Create() { return Widget::Create() && moses::CDrawObj::Init() != 0; }
void AnimTextSprite::Show(bool show) { Widget::Show(show); }

// ---------------------------------------------------------------------------------------------
// WP-4: PolySprite, AnimPolySprite, TextSprite
// PolySprite. Aaron 0x100057c0 (ord 18) / 0x100058b9 (ord 19). (The original also copies the scene's GDI caches into +0x68 / +0x7c: not ported.)
PolySprite::PolySprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, POINT* p0, POINT* p1)
    : Widget(parent, kNoRes, x, y, level, cmdId, style), moses::CDrawPoly(ViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawPoly*>(this), style);
    moses::CDrawPoly::Init(p0, p1);
}
PolySprite::PolySprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, POINT* pts, u32 n, int closed)
    : Widget(parent, kNoRes, x, y, level, cmdId, style), moses::CDrawPoly(ViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawPoly*>(this), style);
    moses::CDrawPoly::Init(pts, n, closed);
}
PolySprite::~PolySprite() {}
bool PolySprite::Create() { return Widget::Create() && moses::CDrawObj::Init() != 0; }          // ord 478-481 0x10006a10
void PolySprite::Show(bool show) { Widget::Show(show); }                                         // ord 675-678 0x10006a50
bool PolySprite::HitTest(int x, int y) { return moses::CDrawObj::IsPicked(x, y) != 0; }          // ord 547-549 0x100069d0

// AnimPolySprite. Aaron 0x10005055 (ord 25) / 0x10005167 (ord 26)
AnimPolySprite::AnimPolySprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, POINT* p0, POINT* p1)
    : Widget(parent, kNoRes, x, y, level, cmdId, style), moses::CAnimPoly(AnimViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawPoly*>(this), style);
    moses::CDrawPoly::Init(p0, p1);
}
AnimPolySprite::AnimPolySprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, POINT* pts, u32 n, int closed)
    : Widget(parent, kNoRes, x, y, level, cmdId, style), moses::CAnimPoly(AnimViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawPoly*>(this), style);
    moses::CDrawPoly::Init(pts, n, closed);
}
AnimPolySprite::~AnimPolySprite() {}
bool AnimPolySprite::Create() { return Widget::Create() && moses::CDrawObj::Init() != 0; }      // ord 483-485 0x10006980
void AnimPolySprite::Show(bool show) {                                                           // ord 679-682 0x100068b0 (-> AnimSprite::Show)
    if (!show) GetAnimObj()->GetAnimView()->QStop(GetAnimObj());
    Widget::Show(show);
}
bool AnimPolySprite::HitTest(int x, int y) { return moses::CDrawObj::IsPicked(x, y) != 0; }     // ord 551-553 0x10006950

// TextSprite. Aaron 0x10005b0d (ord 21)
TextSprite::TextSprite(WidgetParent parent, int x, int y, int level, u32 cmdId, u32 style, const char* text, const char* fontFace,
                       int fontHeight, moses::COLORREF color)
    : Widget(parent, kNoRes, x, y, level, cmdId, style), moses::CDrawText(ViewOf(scene_), level) {
    InitDrawObj(static_cast<moses::CDrawText*>(this), style);
    if (fontFace) moses::CDrawText::SetFontInfo(fontFace, fontHeight == 0 ? 12 : fontHeight);
    moses::CDrawText::SetTextColor(color);
    if ((style_ & 0x10000) == 0) moses::CDrawText::SetTextAlign((style_ & 0x20000) == 0 ? 1 : 2);
    else moses::CDrawText::SetTextAlign(0);
    moses::CDrawText::Init(text ? text : "");
}
TextSprite::~TextSprite() {}
bool TextSprite::Create() { return Widget::Create() && moses::CDrawObj::Init() != 0; }
void TextSprite::Show(bool show) { Widget::Show(show); }
bool TextSprite::HitTest(int x, int y) { return moses::CDrawObj::IsPicked(x, y) != 0; }

} // namespace aaron
