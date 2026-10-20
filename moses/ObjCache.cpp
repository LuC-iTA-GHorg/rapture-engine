// CObjCache + GDI object caches (see ObjCache.h). moses.dll 0x100010c0..0x100011e0, 0x10003090..0x100031a0, 0x10008cf0.
#include "moses/ObjCache.h"

#include <cstdlib>
#include <cstring>

namespace moses {

// ---------------------------------------------------------------------------------------------
// software SelectObject
namespace gdi {

GdiObj* StockObject(GdiObj::Kind kind) {
    static GdiObj pen, brush, font;
    static bool init = false;
    if (!init) {
        pen.kind = GdiObj::PEN;     pen.pen.style = 0; pen.pen.width = POINT{0, 0}; pen.pen.color = 0;   // BLACK_PEN
        brush.kind = GdiObj::BRUSH; brush.brush.style = 0; brush.brush.color = Rgb(255, 255, 255);         // WHITE_BRUSH
        font.kind = GdiObj::FONT;                                                                           // default font
        init = true;
    }
    return kind == GdiObj::PEN ? &pen : kind == GdiObj::BRUSH ? &brush : &font;
}

GdiObj* SelectObject(GdiCtx* dc, GdiObj* obj) {
    if (!dc || !obj) return nullptr;
    switch (obj->kind) {
    case GdiObj::PEN: {
        GdiObj* prev = dc->selPen ? dc->selPen : StockObject(GdiObj::PEN);
        dc->selPen = obj;
        dc->penStyle = obj->pen.style;
        dc->penWidth = u32(obj->pen.width.x);
        dc->penColor = obj->pen.color;
        dc->penNull = obj->pen.style == 5; // PS_NULL
        return prev;
    }
    case GdiObj::BRUSH: {
        GdiObj* prev = dc->selBrush ? dc->selBrush : StockObject(GdiObj::BRUSH);
        dc->selBrush = obj;
        dc->brushStyle = obj->brush.style;
        dc->brushColor = obj->brush.color;
        dc->brushHatch = obj->brush.hatch;
        return prev;
    }
    case GdiObj::FONT: {
        GdiObj* prev = dc->selFont ? dc->selFont : StockObject(GdiObj::FONT);
        dc->selFont = obj;
        dc->font = &obj->font;
        return prev;
    }
    }
    return nullptr;
}

} // namespace gdi

// ---------------------------------------------------------------------------------------------
// CObjCache
// moses.dll 0x100030e0 (CMem::Alloc(.., zero) -> calloc)
CObjCache::CObjCache(u32 capacity, u32 keySize) : cap_(capacity), keySize_(keySize) {
    objects_ = static_cast<void**>(std::calloc(capacity ? capacity : 1, sizeof(void*)));
    keys_ = static_cast<void**>(std::calloc(capacity ? capacity : 1, sizeof(void*)));
}

// moses.dll 0x10003130: the base destructor calls the BASE delete_object (does nothing), frees the key blocks and the arrays
CObjCache::~CObjCache() {
    for (u32 i = 0; i < cap_; ++i) {
        if (objects_[i]) CObjCache::delete_object(objects_[i]);
        if (keys_[i]) std::free(keys_[i]);
    }
    std::free(objects_);
    std::free(keys_);
}

// vtable slot 0 of the base class: no object can be made
void* CObjCache::create_object(const void*) { return nullptr; }

// moses.dll 0x10003090 (empty)
void CObjCache::delete_object(void*) {}

bool CObjCache::keys_equal(const void* a, const void* b) const { return std::memcmp(a, b, keySize_) == 0; }

// moses.dll 0x100030a0: newest to oldest
int CObjCache::find(const void* key) const {
    for (int i = int(count_) - 1; i >= 0; --i)
        if (keys_equal(key, keys_[i])) return i;
    return -1;
}

// port: used by the derived destructors (the original leaks, see the header)
void CObjCache::release_all() {
    for (u32 i = 0; i < count_; ++i) {
        if (objects_[i]) delete_object(objects_[i]);
        objects_[i] = nullptr;
    }
    count_ = 0;
}

// moses.dll 0x100031a0
void* CObjCache::Select(const void* key) {
    int idx = find(key);
    if (idx < 0) {
        void* obj = create_object(key);
        if (!obj) return nullptr;
        void* keyBlock;
        if (count_ == cap_) {
            // full: drop the oldest.  (original: delete_object(obj[0]); obj[] shifted down by one; key[] pointers shifted
            // too, with the aliasing bug described in the header.  Port: the key block of the evicted entry is recycled.)
            if (cap_ == 0) { delete_object(obj); return nullptr; }
            delete_object(objects_[0]);
            keyBlock = keys_[0];
            for (u32 i = 0; i + 1 < cap_; ++i) { objects_[i] = objects_[i + 1]; keys_[i] = keys_[i + 1]; }
            count_ = cap_ - 1;
        } else {
            keyBlock = std::malloc(keySize_ ? keySize_ : 1);
        }
        keys_[count_] = keyBlock;
        objects_[count_] = obj;
        std::memcpy(keyBlock, key, keySize_);
        ++count_;
        return obj;
    }
    // hit: move to the newest position
    void* obj = objects_[idx];
    void* k = keys_[idx];
    for (u32 i = u32(idx); i + 1 < count_; ++i) { objects_[i] = objects_[i + 1]; keys_[i] = keys_[i + 1]; }
    objects_[count_ - 1] = obj;
    keys_[count_ - 1] = k;
    return obj;
}

// ---------------------------------------------------------------------------------------------
// CGdiCache
CGdiCache::CGdiCache(u32 capacity, u32 keySize) : CObjCache(capacity, keySize) {} // moses.dll 0x100010c0

// moses.dll 0x10008cf0
void* CGdiCache::Select(GdiCtx* dc, const void* key) {
    void* h = CObjCache::Select(key);
    if (dc) {
        if (h) return gdi::SelectObject(dc, static_cast<GdiObj*>(h));
        h = nullptr;
    }
    return h;
}

// CGdiPenCache: key = LOGPEN
CGdiPenCache::CGdiPenCache(u32 capacity) : CGdiCache(capacity, sizeof(LOGPEN)) {} // moses.dll 0x100011b0
CGdiPenCache::~CGdiPenCache() { release_all(); }
void* CGdiPenCache::create_object(const void* key) { // FUN_100011e0 CreatePenIndirect
    GdiObj* o = new GdiObj();
    o->kind = GdiObj::PEN;
    o->pen = *static_cast<const LOGPEN*>(key);
    return o;
}
void CGdiPenCache::delete_object(void* obj) { delete static_cast<GdiObj*>(obj); } // FUN_100010f0 DeleteObject

// CGdiBrushCache: key = LOGBRUSH
CGdiBrushCache::CGdiBrushCache(u32 capacity) : CGdiCache(capacity, sizeof(LOGBRUSH)) {} // moses.dll 0x10001110
CGdiBrushCache::~CGdiBrushCache() { release_all(); }
void* CGdiBrushCache::create_object(const void* key) { // FUN_10001140 CreateBrushIndirect
    GdiObj* o = new GdiObj();
    o->kind = GdiObj::BRUSH;
    o->brush = *static_cast<const LOGBRUSH*>(key);
    return o;
}
void CGdiBrushCache::delete_object(void* obj) { delete static_cast<GdiObj*>(obj); }

// CGdiFontCache: key = LOGFONT
CGdiFontCache::CGdiFontCache(u32 capacity) : CGdiCache(capacity, sizeof(LOGFONT_LITE)) {} // moses.dll 0x10001160
CGdiFontCache::~CGdiFontCache() { release_all(); }
void* CGdiFontCache::create_object(const void* key) { // FUN_10001190 CreateFontIndirectA
    GdiObj* o = new GdiObj();
    o->kind = GdiObj::FONT;
    o->font = *static_cast<const LOGFONT_LITE*>(key);
    return o;
}
void CGdiFontCache::delete_object(void* obj) { delete static_cast<GdiObj*>(obj); }
bool CGdiFontCache::keys_equal(const void* a, const void* b) const {
    const LOGFONT_LITE& x = *static_cast<const LOGFONT_LITE*>(a);
    const LOGFONT_LITE& y = *static_cast<const LOGFONT_LITE*>(b);
    return x.height == y.height && x.weight == y.weight && x.italic == y.italic && x.underline == y.underline &&
           std::strncmp(x.face, y.face, sizeof x.face) == 0;
}

} // namespace moses
