// CObjCache / CGdiCache / CGdi{Pen,Brush,Font}Cache (moses.dll 0x100030e0..0x100031a0, 0x100010c0..0x100011b0, 0x10008cf0).
//
// The original caches Win32 GDI handles (HPEN/HBRUSH/HFONT) created from the LOGPEN / LOGBRUSH / LOGFONT that a
// CDrawRect / CDrawPoly / CDrawText keeps in its own fields: `Select(key)` returns the object for the key (creating it
// on a miss) and moves it to the "most recently used" end; when the cache is full the oldest entry is evicted.
// Aaron creates two caches per scene (pen+brush) and hands them to the drawables (pointer at +0x28 / +0x3c / +0x24).
//
// PORT: there are no GDI handles.  The "handle" is a `GdiObj*` (a heap copy of the LOG* structure, see below) and
// "SelectObject(hdc, obj)" is `gdi::SelectObject(GdiCtx*, GdiObj*)`, which loads the pen/brush/font into the software
// device context and returns the previously selected object (a stock object when none was selected: BLACK_PEN,
// WHITE_BRUSH, the default font), exactly like GDI.
//
// WIRING.  CDrawRect / CDrawPoly / CDrawText have optional cache pointers (`pen_cache_`, `brush_cache_`, `font_cache_`).
// With a cache set, `select_pen/select_brush/select_font` go through `CGdiCache::Select(dc, &LOG*)` as in the original.
// With NO cache (the case of the ported Aaron, which has no pen/brush cache) the port applies the LOG* values directly;
// the original would leave the DC defaults (black pen, white brush) in that case: that is a deliberate deviation, because
// the ported Aaron relies on the values being honoured.
//
// QUIRKS of the original, kept or fixed:
//  * the destructor of CObjCache runs with the base vtable, so `delete_object` of the derived cache is never called
//    and every cached HGDIOBJ leaks.  Port: the CGdi*Cache destructors release their objects (no handles to leak, but
//    heap blocks would).
//  * when the cache is full the original shifts the key POINTER array by one without freeing keys[0] and leaves
//    keys[cap-1] aliased with keys[cap-2] (the new key is then written into the shared block, corrupting the key of the
//    second newest entry, and the destructor frees it twice).  Port: the evicted key block is recycled for the new key.
#pragma once

#include "moses/DrawGdi.h"

namespace moses {

// Software "GDI object": what the HGDIOBJ of the original refers to.
struct GdiObj {
    enum Kind : int { PEN = 1, BRUSH = 2, FONT = 3 };
    Kind kind = PEN;
    LOGPEN pen;
    LOGBRUSH brush;
    LOGFONT_LITE font;
};

namespace gdi {
// Stock objects (GetStockObject BLACK_PEN / WHITE_BRUSH / default font).
GdiObj* StockObject(GdiObj::Kind kind);
// SelectObject(hdc, h): returns the object that was selected before (stock object if none).
GdiObj* SelectObject(GdiCtx* dc, GdiObj* obj);
} // namespace gdi

// ---------------------------------------------------------------------------------------------
// CObjCache (0x18): +4 count, +8 capacity, +c key size, +10 object[], +14 key[].
class CObjCache {
public:
    CObjCache(u32 capacity, u32 keySize);      // moses.dll 0x100030e0
    virtual ~CObjCache();                      // moses.dll 0x10003130 (port: virtual so the derived caches clean up)
    CObjCache(const CObjCache&) = delete;
    CObjCache& operator=(const CObjCache&) = delete;

    // moses.dll 0x100031a0: find the key (searching from the newest entry backwards), create it on a miss, move it to
    // the newest position; returns nullptr when create_object fails.
    void* Select(const void* key);

    u32 Count() const { return count_; }       // +4
    u32 Capacity() const { return cap_; }      // +8
    u32 KeySize() const { return keySize_; }   // +c
    void* ObjectAt(u32 i) const { return i < count_ ? objects_[i] : nullptr; } // port: inspection (index 0 = oldest)

protected:
    virtual void* create_object(const void* key);   // vtable slot 0 (CreatePenIndirect / CreateBrushIndirect / ...)
    virtual void delete_object(void* obj);          // moses.dll 0x10003090 slot 1 (base: nothing; Gdi caches: DeleteObject)
    virtual bool keys_equal(const void* a, const void* b) const; // port: find() compares `keySize` bytes (memcmp)
    int find(const void* key) const;                // moses.dll 0x100030a0 (-1 = not found)
    void release_all();                             // port: delete_object on every entry (used by the derived destructors)

private:
    u32 count_ = 0, cap_ = 0, keySize_ = 0;
    void** objects_ = nullptr;   // +10
    void** keys_ = nullptr;      // +14 (each a private copy of the key)
};

// ---------------------------------------------------------------------------------------------
// CGdiCache: a CObjCache of GDI objects + Select into a device context.
class CGdiCache : public CObjCache {
public:
    CGdiCache(u32 capacity, u32 keySize);      // moses.dll 0x100010c0
    // moses.dll 0x10008cf0: obj = CObjCache::Select(key); with a DC the object is selected and the PREVIOUS object is
    // returned (callers re-select it afterwards); without a DC the cached object itself is returned.
    void* Select(GdiCtx* dc, const void* key);
    using CObjCache::Select;
};

class CGdiPenCache : public CGdiCache {         // key = LOGPEN (16 bytes)
public:
    explicit CGdiPenCache(u32 capacity);        // moses.dll 0x100011b0
    ~CGdiPenCache() override;
protected:
    void* create_object(const void* key) override;   // FUN_100011e0 CreatePenIndirect
    void delete_object(void* obj) override;          // FUN_100010f0 DeleteObject
};

class CGdiBrushCache : public CGdiCache {       // key = LOGBRUSH (12 bytes)
public:
    explicit CGdiBrushCache(u32 capacity);      // moses.dll 0x10001110
    ~CGdiBrushCache() override;
protected:
    void* create_object(const void* key) override;   // FUN_10001140 CreateBrushIndirect
    void delete_object(void* obj) override;
};

class CGdiFontCache : public CGdiCache {        // key = LOGFONT (0x3c bytes in the original; port: LOGFONT_LITE)
public:
    explicit CGdiFontCache(u32 capacity);       // moses.dll 0x10001160
    ~CGdiFontCache() override;
protected:
    void* create_object(const void* key) override;   // FUN_10001190 CreateFontIndirectA
    void delete_object(void* obj) override;
    bool keys_equal(const void* a, const void* b) const override; // field-wise (LOGFONT_LITE has padding)
};

} // namespace moses
