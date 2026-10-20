// moses.dll port - CDrawText + CTextHelper (rich text, moses.md sec. 4.1). Package 1.
#include "moses/DrawGdi.h"
#include "moses/DrawView.h"
#include "moses/GdiRaster.h"
#include "moses/ObjCache.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace moses {

// ---------------------------------------------------------------------------------------------
// CTextHelper (0x2c bytes in the original: record buffer + layout box + align + colour).
// Item types follow the original record types: 1 = text run, 2 FACENAME, 3 HEIGHT, 4 COLOR, 5 BOLD, 6 NORMAL,
// 7 ITALIC, 8 UNDERLINE, 9 TABS (ignored when applied, like the original), 10 LEFT, 11 CENTER, 12 RIGHT.
class CTextHelper {
public:
    struct Item {
        int type = 0;
        int value = 0;
        std::string str;   // text run / face name
        RECT rc;           // run rectangle in layout space (origin = top-left of the union)
    };

    std::string plain;        // +0 / +4 (non-rich text)
    std::vector<Item> items;  // rich records
    int rich = 0;             // +0x0c
    int laid = 0;             // +0x10 box valid (only ever set by the rich layout - plain text relays out every time)
    RECT box;                 // +0x14 layout box, centred on the origin after layout()
    u32 align = 0;            // +0x24 DT_* flags
    COLORREF color = 0;       // +0x28

    // 0x100098c0 clear (note: align and colour survive)
    int clear() {
        plain.clear();
        items.clear();
        laid = 0;
        return 0;
    }

    // 0x10009730 layout
    void layout(const LOGFONT_LITE& base) {
        if (!rich) {
            RECT rc{0, 0, 0, 0}; // original passes the stale box; CALCRECT keeps only left/top
            gdi::TextRect(base, plain.c_str(), int(plain.size()), rc, align | 0x440);
            box = rc;
        } else {
            LOGFONT_LITE f = base;
            u32 al = align;
            RECT cur{0, 0, 0, 0};
            for (Item& it : items) {
                if (it.type == 1) {
                    RECT rc{0, cur.top, 0, 0};
                    gdi::TextRect(f, it.str.c_str(), int(it.str.size()), rc, al | 0x440);
                    if (!laid) { laid = 1; box = rc; }
                    else { RECT t; UnionRect(&t, &box, &rc); box = t; }
                    it.rc = rc;
                    cur.top = rc.bottom;
                } else {
                    apply(it, f, al, nullptr);
                }
            }
            align = al; // alignment commands persist in the helper (this+0x24)
        }
        int w = box.right - box.left, h = box.bottom - box.top;
        box.right = w / 2;
        box.left = box.right - w;
        box.bottom = h / 2;
        box.top = box.bottom - h;
    }

    // 0x10009cf0 apply_command (colour goes to the DC text colour)
    static void apply(const Item& it, LOGFONT_LITE& f, u32& al, GdiCtx* dc) {
        switch (it.type) {
        case 2: std::strncpy(f.face, it.str.c_str(), sizeof f.face - 1); f.face[sizeof f.face - 1] = 0; break;
        case 3: f.height = it.value; break;
        case 4: if (dc) dc->textColor = COLORREF(it.value); break;
        case 5: case 6: f.weight = it.value; break;
        case 7: f.italic = it.value != 0; break;
        case 8: f.underline = it.value != 0; break;
        case 10: case 11: case 12: al = u32(it.value); break;
        default: break; // 9 TABS: stored, never applied
        }
    }

    // 0x1000a040 SetString
    int SetString(const char* s, const LOGFONT_LITE& base, int richFlag) {
        clear();
        if (s) {
            bool ok;
            if (!richFlag) { plain = s; ok = true; }
            else ok = parse_rich(reinterpret_cast<const unsigned char*>(s));
            if (ok) {
                rich = richFlag;
                layout(base);
                return 1;
            }
        }
        return clear();
    }

    // 0x10009fd0 get_bounds
    void get_bounds(const LOGFONT_LITE& base, RECT& r) {
        if (!laid) layout(base);
        r = box;
    }

    // 0x10009f70 draw (r = clip region of this Compose, bounds = object bounds in view coordinates)
    void draw(GdiCtx& dc, const RECT& bounds, const LOGFONT_LITE& base, const RECT& clip) {
        dc.textColor = color;
        if (!rich) {
            gdi::TextDraw(dc, base, plain.c_str(), int(plain.size()), bounds, align);
            return;
        }
        LOGFONT_LITE f = base;
        u32 al = align;
        for (const Item& it : items) {
            if (it.type == 1) { // draw_run 0x10009e00
                RECT rc{it.rc.left + bounds.left, it.rc.top + bounds.top, it.rc.right + bounds.left, it.rc.bottom + bounds.top};
                RECT tmp;
                if (IntersectRect(&tmp, &rc, &clip))
                    gdi::TextDraw(dc, f, it.str.c_str(), int(it.str.size()), rc, al | 0x40);
            } else {
                apply(it, f, al, &dc);
            }
        }
    }

private:
    // ---- parser (0x10009eb0 parse_rich and friends) ---------------------------------------------
    static bool isAlnum(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

    // 0x10009b00 parse_number: decimal (optional sign) or 0x hex; nullptr on error/overflow
    static const unsigned char* parse_number(const unsigned char* p, u32* out) {
        u32 v = 0;
        bool neg = false;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            if (!std::isxdigit(*p)) return nullptr;
            while (std::isxdigit(*p)) {
                unsigned c = *p;
                unsigned d = (c <= '9') ? c - '0' : (c & 0xf) + 9;
                v = v * 16 + d;
                ++p;
                if (v >= 0x80000000u) break;
            }
            *out = v;
            return p;
        }
        if (*p == '+' || *p == '-') { neg = *p == '-'; ++p; }
        if (!(*p >= '0' && *p <= '9')) return nullptr;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            ++p;
            if (v > 0x7fffffffu) return nullptr;
        }
        if (neg) v = 0u - v;
        *out = v;
        return p;
    }

    // 0x10009a80 parse_keyword
    static const unsigned char* parse_keyword(const unsigned char* p, int* id) {
        static const char* const kw[] = {"FACENAME", "HEIGHT", "COLOR", "BOLD", "NORMAL", "ITALIC", "UNDERLINE", "TABS", "LEFT", "CENTER", "RIGHT"};
        const unsigned char* e = p;
        while (*e && isAlnum(*e)) ++e;
        size_t n = size_t(e - p);
        if (n == 0) return nullptr;
        for (int i = 0; i < 11; ++i) {
            if (std::strlen(kw[i]) != n) continue;
            bool same = true;
            for (size_t k = 0; k < n; ++k)
                if (std::toupper(p[k]) != kw[i][k]) { same = false; break; }
            if (same) { *id = i + 2; return e; }
        }
        return nullptr;
    }

    // 0x10009c70 + 0x10009c10 parse_quoted: p points after the opening quote; returns the position after the
    // closing quote (or at the terminator).
    const unsigned char* parse_quoted(const unsigned char* p) {
        std::string out;
        while (true) {
            unsigned char c = *p;
            if (c == 0) break;
            if (c == '\n' || c == '\r' || c == 0x1a) break;
            if (c == '"') { ++p; break; }
            ++p;
            if (c == '\\') {
                unsigned char n = *p;
                if (n == 0 || n == '\n' || n == '\r' || n == 0x1a) break;
                if (n == 't') c = '\t';
                else if (n == 'r') c = '\r';
                else if (n == 'n') c = '\n';
                else if (n == 'c') c = 0xA9; // (c)
                else c = n;
                ++p;
            }
            out.push_back(char(c));
            if (out.size() >= 255) break; // buffer of 0x100 in the original
        }
        if (!out.empty()) {
            Item it;
            it.type = 1;
            it.str = out;
            items.push_back(it);
        }
        return p;
    }

    // 0x10009960 parse_command: p points after '/'
    const unsigned char* parse_command(const unsigned char* p) {
        int id = 0;
        const unsigned char* q = parse_keyword(p, &id);
        if (!q) return nullptr;
        Item it;
        it.type = id;
        u32 v = 0;
        switch (id) {
        case 2: { // FACENAME:"name"
            if (*q != ':' || q[1] != '"') return nullptr;
            const unsigned char* s = q + 2;
            const unsigned char* e = s;
            while (*e && *e != '"') ++e;
            if (*e == 0) return nullptr;
            it.str.assign(reinterpret_cast<const char*>(s), size_t(e - s));
            q = e + 1;
            break;
        }
        case 3: case 4: case 7: case 8: case 9:
            if (*q != ':') return nullptr;
            q = parse_number(q + 1, &v);
            if (!q) return nullptr;
            it.value = int(v);
            break;
        case 5: it.value = 700; break;
        case 6: it.value = 400; break;
        case 10: it.value = 0; break;
        case 11: it.value = 1; break;
        case 12: it.value = 2; break;
        default: return q;
        }
        items.push_back(it);
        return q;
    }

    bool parse_rich(const unsigned char* p) {
        while (true) {
            if (!p) return false;
            if (*p == 0) break;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
            unsigned char c = *p++;
            if (c == 0) { --p; break; }
            if (c == '"') p = parse_quoted(p);
            else if (c == '/') p = parse_command(p);
            else if (c == 0x1a) break;
        }
        return p != nullptr;
    }
};

// ---------------------------------------------------------------------------------------------
// CDrawText
CDrawText::CDrawText(CDrawView* view, int level) : CDrawGdi(view, level), helper_(new CTextHelper) { font_.weight = 0; }

// moses.dll 0x100090c0: copies only the CDrawGdi fields; the font and the text are NOT copied (original quirk)
CDrawText::CDrawText(const CDrawText& other) : CDrawGdi(other), helper_(new CTextHelper) { font_.weight = 0; }

CDrawText::~CDrawText() { delete helper_; }

CDrawObj* CDrawText::Copy() const { return new CDrawText(*this); } // 0x10009230

// moses.dll 0x10009160
int CDrawText::Init(const char* str) {
    if (!CDrawObj::Init()) return 0;
    if (str && !SetString(str, 0)) return 0;
    return 1;
}

// moses.dll 0x100091a0
void CDrawText::SetFontInfo(const char* face, int height) {
    std::strncpy(font_.face, face, sizeof font_.face - 1);
    font_.face[sizeof font_.face - 1] = 0;
    font_.height = height;
    recalc_bounds();
}

// moses.dll 0x100091d0
int CDrawText::SetString(const char* str, int rich) {
    helper_->color = color_;
    helper_->align = align_;
    int r = helper_->SetString(str, font_, rich);
    align_ = helper_->align;
    recalc_bounds();
    return r;
}

void CDrawText::SetTextAlign(u32 dtFlags) { align_ = dtFlags; helper_->align = dtFlags; } // 0x10009210
void CDrawText::SetTextColor(COLORREF c) { color_ = c; helper_->color = c; }              // 0x10009220

// moses.dll 0x10008fe0: measures (once for rich text); the result also becomes rect_
void CDrawText::get_bounds(RECT& r, u32 flip) {
    (void)flip;
    helper_->get_bounds(font_, r);
    rect_ = r;
}

// moses.dll 0x10009030
void CDrawText::display(CDibSect* view, GdiCtx* dc, RECT& r) {
    (void)view;
    helper_->draw(*dc, p_.bounds, font_, r);
}

void* CDrawText::select_font(GdiCtx* dc) { // 0x10002020: CGdiCache::Select(+0x24, hdc, &LOGFONT at +0x28)
    if (font_cache_) return font_cache_->Select(dc, &font_);
    dc->font = &font_; // no cache (port): use the font directly
    return nullptr;
}

} // namespace moses
