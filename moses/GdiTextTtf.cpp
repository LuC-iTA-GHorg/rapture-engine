// Portable text backend of the software GDI (WP-5): stb_truetype (third_party/stb, public domain / MIT) instead of
// Windows GDI.  Used automatically when the engine is not built for Windows (GdiRaster.cpp forwards here); on Windows the
// real GDI stays the default (fidelity with the original), and RAPTURE_TEXT_BACKEND=ttf selects this backend for testing.
//
// Rendering rules copy what the Windows path does:
//   * NO anti-aliasing (the original ran with NONANTIALIASED_QUALITY): glyph coverage >= 128 is a pixel;
//   * LOGFONT height semantics: negative = em height in pixels, positive = cell height (ascent + descent);
//   * DrawText flags honoured: DT_CENTER/RIGHT, DT_VCENTER/BOTTOM, DT_WORDBREAK, DT_SINGLELINE, DT_EXPANDTABS, DT_NOCLIP,
//     DT_NOPREFIX, DT_CALCRECT (all Win32 values), '\n' line breaks, '&' prefix characters;
//   * bold (weight >= 600) and italic are synthesised when the family has no bold / italic file.
// Font lookup (first hit wins): files registered with AddFontFile; <dir>/<face>.ttf and <dir>/rapture.ttf for every dir
// added with AddFontDir, the folder in $RAPTURE_FONT_DIR and ./fonts; $RAPTURE_FONT (one file for every face); then the
// well known system folders (C:\Windows\Fonts, /usr/share/fonts/..., /Library/Fonts) with a face -> file table (Arial,
// Times New Roman, Courier New, Tahoma ... with the Liberation / DejaVu equivalents).  If nothing is found a built-in
// 5x7 bitmap font is used so that text is still visible and measurable (HasOutlineFont() tells which one is in use).
#include "moses/GdiRaster.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace moses {
namespace gdi {
namespace ttf {

namespace {

// DrawText flags (Win32)
constexpr u32 F_CENTER = 0x1, F_RIGHT = 0x2, F_VCENTER = 0x4, F_BOTTOM = 0x8, F_WORDBREAK = 0x10, F_SINGLELINE = 0x20,
              F_EXPANDTABS = 0x40, F_NOCLIP = 0x100, F_CALCRECT = 0x400, F_NOPREFIX = 0x800;

// ---- built-in 5x7 bitmap font (ASCII 32..126, column major, bit0 = top row) ---------------------------------------
const u8 kFont5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x56,0x20,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x2A,0x1C,0x7F,0x1C,0x2A},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x72,0x49,0x49,0x49,0x46},{0x21,0x41,0x49,0x4D,0x33},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x31},{0x41,0x21,0x11,0x09,0x07},
    {0x36,0x49,0x49,0x49,0x36},{0x46,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},{0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x59,0x09,0x06},
    {0x3E,0x41,0x5D,0x59,0x4E},{0x7C,0x12,0x11,0x12,0x7C},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x41,0x51,0x73},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x1C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x26,0x49,0x49,0x49,0x32},
    {0x03,0x01,0x7F,0x01,0x03},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x59,0x49,0x4D,0x43},{0x00,0x7F,0x41,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x41,0x7F},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x03,0x07,0x08,0x00},{0x20,0x54,0x54,0x78,0x40},{0x7F,0x28,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x28},
    {0x38,0x44,0x44,0x28,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x00,0x08,0x7E,0x09,0x02},{0x18,0xA4,0xA4,0x9C,0x78},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x40,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x78,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0xFC,0x18,0x24,0x24,0x18},{0x18,0x24,0x24,0x18,0xFC},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x24},
    {0x04,0x04,0x3F,0x44,0x24},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x4C,0x90,0x90,0x90,0x7C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x77,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x02,0x01,0x02,0x04,0x02},
};

// Windows-1252 0x80..0x9f -> Unicode
const u16 kCp1252[32] = {0x20AC,0x81,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0x8D,0x017D,0x8F,
                         0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0x9D,0x017E,0x0178};
inline int Cp(u8 c) { return (c >= 0x80 && c < 0xA0) ? kCp1252[c - 0x80] : c; }

// ---- font files --------------------------------------------------------------------------------------------------
struct FontFile {
    std::vector<u8> bytes;
    stbtt_fontinfo info;
    bool ok = false;
};

struct Registry {
    std::map<std::string, std::string> files; // "face" / "face:bold" ... -> path ("*" = default)
    std::vector<std::string> dirs;
    std::map<std::string, std::shared_ptr<FontFile>> cache; // path -> loaded (nullptr = failed)
};
Registry& Reg() {
    static Registry r;
    return r;
}

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string Squash(const std::string& s) { // lower case, no spaces
    std::string r;
    for (char c : s)
        if (c != ' ') r.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    return r;
}
bool Exists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

std::shared_ptr<FontFile> LoadFont(const std::string& path) {
    Registry& r = Reg();
    auto it = r.cache.find(path);
    if (it != r.cache.end()) return it->second;
    std::shared_ptr<FontFile> ff;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f) {
        ff = std::make_shared<FontFile>();
        u8 tmp[16384];
        size_t n;
        while ((n = std::fread(tmp, 1, sizeof tmp, f)) > 0) ff->bytes.insert(ff->bytes.end(), tmp, tmp + n);
        std::fclose(f);
        int off = stbtt_GetFontOffsetForIndex(ff->bytes.data(), 0);
        ff->ok = off >= 0 && stbtt_InitFont(&ff->info, ff->bytes.data(), off) != 0;
        if (!ff->ok) ff.reset();
    }
    r.cache[path] = ff;
    return ff;
}

struct Family {
    const char* face;      // lower case, no spaces; "" = default
    const char* regular[3]; // candidate file names
    const char* bold[3];
    const char* italic[3];
    const char* boldItalic[3];
};
const Family kFamilies[] = {
    {"arial",         {"arial.ttf", "LiberationSans-Regular.ttf", "DejaVuSans.ttf"},      {"arialbd.ttf", "LiberationSans-Bold.ttf", "DejaVuSans-Bold.ttf"},
                      {"ariali.ttf", "LiberationSans-Italic.ttf", "DejaVuSans-Oblique.ttf"}, {"arialbi.ttf", "LiberationSans-BoldItalic.ttf", "DejaVuSans-BoldOblique.ttf"}},
    {"timesnewroman", {"times.ttf", "LiberationSerif-Regular.ttf", "DejaVuSerif.ttf"},    {"timesbd.ttf", "LiberationSerif-Bold.ttf", "DejaVuSerif-Bold.ttf"},
                      {"timesi.ttf", "LiberationSerif-Italic.ttf", "DejaVuSerif-Italic.ttf"}, {"timesbi.ttf", "LiberationSerif-BoldItalic.ttf", "DejaVuSerif-BoldItalic.ttf"}},
    {"couriernew",    {"cour.ttf", "LiberationMono-Regular.ttf", "DejaVuSansMono.ttf"},   {"courbd.ttf", "LiberationMono-Bold.ttf", "DejaVuSansMono-Bold.ttf"},
                      {"couri.ttf", "LiberationMono-Italic.ttf", "DejaVuSansMono-Oblique.ttf"}, {"courbi.ttf", "LiberationMono-BoldItalic.ttf", "DejaVuSansMono-BoldOblique.ttf"}},
};

const char* const kSystemDirs[] = {
    "C:/Windows/Fonts", "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/truetype/liberation", "/usr/share/fonts/truetype/liberation2",
    "/usr/share/fonts/TTF", "/usr/share/fonts/dejavu", "/usr/share/fonts/liberation-fonts", "/usr/share/fonts/liberation",
    "/usr/share/fonts/truetype/freefont", "/usr/share/fonts", "/System/Library/Fonts/Supplemental", "/Library/Fonts", "/System/Library/Fonts",
};

std::string Join(const std::string& dir, const std::string& f) { return dir.empty() ? f : dir + "/" + f; }

std::string ResolvePath(const LOGFONT_LITE& lf) {
    Registry& r = Reg();
    bool bold = lf.weight >= 600, italic = lf.italic;
    std::string face = Squash(lf.face);
    const char* suffix = bold && italic ? ":bolditalic" : bold ? ":bold" : italic ? ":italic" : "";
    for (const std::string& key : {face + suffix, face, std::string("*") + suffix, std::string("*")}) {
        auto it = r.files.find(key);
        if (it != r.files.end() && Exists(it->second)) return it->second;
    }
    std::vector<std::string> dirs = r.dirs;
    if (const char* d = std::getenv("RAPTURE_FONT_DIR")) dirs.push_back(d);
    dirs.push_back("fonts");
    if (const char* e = std::getenv("RAPTURE_FONT"))
        if (Exists(e)) return e;
    for (const std::string& d : dirs) {
        for (const std::string& n : {std::string(lf.face) + ".ttf", face + ".ttf", std::string("rapture.ttf")}) {
            std::string p = Join(d, n);
            if (n != "rapture.ttf" && Exists(p)) return p;
        }
        std::string p = Join(d, "rapture.ttf");
        if (Exists(p)) return p;
    }
    if (Exists("rapture.ttf")) return "rapture.ttf";
    // system fonts: pick the family, then the variant, then any candidate in any system folder
    const Family* fam = &kFamilies[0]; // default: sans serif
    for (const Family& f : kFamilies)
        if (face == f.face) fam = &f;
    if (face.find("times") != std::string::npos || face.find("serif") != std::string::npos) fam = &kFamilies[1];
    if (face.find("courier") != std::string::npos || face.find("mono") != std::string::npos || face.find("fixed") != std::string::npos) fam = &kFamilies[2];
    const char* const* list = bold && italic ? fam->boldItalic : bold ? fam->bold : italic ? fam->italic : fam->regular;
    for (int pass = 0; pass < 2; ++pass) {
        const char* const* l = pass == 0 ? list : fam->regular; // second pass: the regular file (bold/italic synthesised)
        for (int i = 0; i < 3; ++i)
            for (const char* sd : kSystemDirs) {
                std::string p = Join(sd, l[i]);
                if (Exists(p)) return p;
            }
    }
    return std::string();
}

// A face selected at a size
struct Face {
    std::shared_ptr<FontFile> ff; // null = bitmap font
    std::string path;
    float scale = 1.0f;           // stb scale (font units -> pixels)
    int px = 12;                  // em / nominal size in pixels
    int ascent = 0, descent = 0;  // pixels
    bool synthBold = false, synthItalic = false;
    bool bold = false, italic = false, underline = false;
    int bmScale = 1;              // bitmap font integer zoom
};

int Em(const LOGFONT_LITE& f) { return f.height == 0 ? 12 : std::abs(f.height); }

Face MakeFace(const LOGFONT_LITE& lf) {
    Face fc;
    fc.bold = lf.weight >= 600;
    fc.italic = lf.italic;
    fc.underline = lf.underline;
    std::string path = ResolvePath(lf);
    if (!path.empty()) fc.ff = LoadFont(path);
    if (fc.ff) {
        fc.path = path;
        std::string lp = Lower(path);
        // file name tells whether it already is the bold / italic variant
        bool fileBold = lp.find("bold") != std::string::npos || lp.find("bd.") != std::string::npos || lp.find("bi.") != std::string::npos;
        bool fileItalic = lp.find("italic") != std::string::npos || lp.find("oblique") != std::string::npos || lp.find("i.ttf") != std::string::npos || lp.find("bi.") != std::string::npos;
        fc.synthBold = fc.bold && !fileBold;
        fc.synthItalic = fc.italic && !fileItalic;
        stbtt_fontinfo* fi = &fc.ff->info;
        int asc, desc, gap;
        stbtt_GetFontVMetrics(fi, &asc, &desc, &gap);
        if (lf.height < 0) { // em height
            fc.scale = stbtt_ScaleForMappingEmToPixels(fi, float(-lf.height));
            fc.px = -lf.height;
        } else { // cell height = ascent + descent
            fc.scale = stbtt_ScaleForPixelHeight(fi, float(Em(lf)));
            fc.px = Em(lf);
        }
        fc.ascent = int(std::floor(asc * fc.scale + 0.5f));
        fc.descent = int(std::floor(-desc * fc.scale + 0.5f));
        if (fc.ascent + fc.descent < 1) { fc.ascent = fc.px; fc.descent = 0; }
    } else {
        fc.bmScale = std::max(1, Em(lf) / 8);
        fc.px = 8 * fc.bmScale;
        fc.ascent = 7 * fc.bmScale;
        fc.descent = 1 * fc.bmScale;
    }
    return fc;
}

int Advance(const Face& f, int cp) {
    if (!f.ff) return 6 * f.bmScale;
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&f.ff->info, cp, &adv, &lsb);
    int a = int(std::floor(adv * f.scale + 0.5f));
    return a + (f.synthBold ? 1 : 0);
}
int LineHeight(const Face& f) { return f.ascent + f.descent; }

struct Run { std::string text; int width = 0; bool hardBreak = false; };

// Splits the text into display lines according to the flags (tabs expanded, '&' prefix removed, word wrap to `maxw`).
std::vector<Run> Layout(const Face& f, const char* s, int len, u32 flags, int maxw, std::vector<int>* underlineAt = nullptr) {
    (void)underlineAt;
    std::vector<Run> lines(1);
    auto adv = [&](unsigned char c) { return Advance(f, Cp(c)); };
    int tab = 8 * adv('0');
    auto push = [&]() { lines.emplace_back(); };
    bool wrap = (flags & F_WORDBREAK) && !(flags & F_SINGLELINE);
    int lastBreak = -1; // index in lines.back().text after which we may break
    for (int i = 0; i < len && s[i]; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (!(flags & F_NOPREFIX) && c == '&') {
            if (i + 1 < len && s[i + 1] == '&') ++i; // "&&" -> "&"
            else continue;                            // prefix marker
            c = '&';
        }
        if (c == '\r') continue;
        if (c == '\n') {
            if (flags & F_SINGLELINE) continue;
            lines.back().hardBreak = true;
            push();
            lastBreak = -1;
            continue;
        }
        Run& cur = lines.back();
        int w;
        if (c == '\t' && (flags & F_EXPANDTABS)) {
            w = tab > 0 ? tab - (cur.width % tab) : 0;
        } else {
            w = adv(c);
        }
        if (wrap && maxw > 0 && cur.width + w > maxw && !cur.text.empty()) {
            if (lastBreak > 0 && size_t(lastBreak) <= cur.text.size()) { // break at the last space
                std::string rest = cur.text.substr(size_t(lastBreak));
                cur.text.resize(size_t(lastBreak));
                while (!cur.text.empty() && cur.text.back() == ' ') cur.text.pop_back();
                int ww = 0;
                for (unsigned char ch : cur.text) ww += adv(ch);
                cur.width = ww;
                push();
                Run& nx = lines.back();
                nx.text = rest;
                for (unsigned char ch : rest) nx.width += adv(ch);
            } else {
                push();
            }
            lastBreak = -1;
        }
        Run& cur2 = lines.back();
        if (c == '\t' && (flags & F_EXPANDTABS)) {
            cur2.text.push_back('\t');
            cur2.width += w;
        } else {
            cur2.text.push_back(char(c));
            cur2.width += w;
            if (c == ' ') lastBreak = int(cur2.text.size());
        }
    }
    return lines;
}

// width of a run with tab stops
int RunWidth(const Face& f, const std::string& t, u32 flags) {
    int w = 0, tab = 8 * Advance(f, '0');
    for (unsigned char c : t) {
        if (c == '\t' && (flags & F_EXPANDTABS)) w += tab > 0 ? tab - (w % tab) : 0;
        else w += Advance(f, Cp(c));
    }
    return w;
}

} // namespace

// ---------------------------------------------------------------------------------------------
bool AddFontFile(const char* face, const char* path) {
    if (!path || !Exists(path)) return false;
    std::string key = face && *face ? Squash(face) : std::string("*");
    Reg().files[key] = path;
    return true;
}
void AddFontDir(const char* dir) {
    if (dir && *dir) Reg().dirs.push_back(dir);
}
void ResetFonts() {
    Reg() = Registry();
}
bool HasOutlineFont(const LOGFONT_LITE& f) { return MakeFace(f).ff != nullptr; }

int TextAscent(const LOGFONT_LITE& f) { return MakeFace(f).ascent; }

// DrawText(DT_CALCRECT): rc.left/top kept; right/bottom computed. Returns the text height.
int TextRect(const LOGFONT_LITE& lf, const char* s, int len, RECT& rc, u32 flags) {
    Face f = MakeFace(lf);
    int maxw = rc.right - rc.left;
    std::vector<Run> lines = Layout(f, s, len, flags, maxw);
    int w = 0;
    for (Run& r : lines) {
        r.width = RunWidth(f, r.text, flags);
        w = std::max(w, r.width);
    }
    int h = int(lines.size()) * LineHeight(f);
    if (len == 0 || !s || !*s) h = LineHeight(f);
    rc.right = rc.left + w;
    rc.bottom = rc.top + h;
    return h;
}

// Draws one glyph with the Windows-like non-antialiased threshold.
static void DrawGlyph(GdiCtx& dc, const Face& f, int cp, int penX, int baseY, uint32_t col, const RECT* clip) {
    auto put = [&](int x, int y) {
        if (clip && (x < clip->left || x >= clip->right || y < clip->top || y >= clip->bottom)) return;
        PutPixel(dc, x, y, col);
    };
    if (!f.ff) { // bitmap font
        int idx = (cp >= 32 && cp <= 126) ? cp - 32 : 31; // '?'
        const u8* g = kFont5x7[idx];
        for (int cx = 0; cx < 5; ++cx)
            for (int ry = 0; ry < 7; ++ry)
                if (g[cx] >> ry & 1)
                    for (int dy = 0; dy < f.bmScale; ++dy)
                        for (int dx = 0; dx < f.bmScale; ++dx) put(penX + cx * f.bmScale + dx, baseY - 7 * f.bmScale + ry * f.bmScale + dy);
        return;
    }
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&f.ff->info, cp, f.scale, f.scale, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    std::vector<u8> bm(size_t(w) * size_t(h));
    stbtt_MakeCodepointBitmap(&f.ff->info, bm.data(), w, h, w, f.scale, f.scale, cp);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (bm[size_t(y) * size_t(w) + size_t(x)] < 128) continue;
            int px = penX + x0 + x, py = baseY + y0 + y;
            if (f.synthItalic) px += (baseY - py) / 4; // ~14 degrees
            put(px, py);
            if (f.synthBold) put(px + 1, py);
        }
}

void TextDraw(GdiCtx& dc, const LOGFONT_LITE& lf, const char* s, int len, const RECT& rc, u32 flags) {
    if (!s || len <= 0) return;
    Face f = MakeFace(lf);
    int maxw = rc.right - rc.left;
    std::vector<Run> lines = Layout(f, s, len, flags, maxw);
    int lh = LineHeight(f);
    int total = int(lines.size()) * lh;
    int y = rc.top;
    if (flags & F_VCENTER) y = rc.top + (rc.bottom - rc.top - total) / 2;
    else if (flags & F_BOTTOM) y = rc.bottom - total;
    uint32_t col = ToArgb(dc.textColor);
    RECT clipRc = rc;
    const RECT* clip = (flags & F_NOCLIP) ? nullptr : &clipRc;
    int tab = 8 * Advance(f, '0');
    for (const Run& r : lines) {
        int w = RunWidth(f, r.text, flags);
        int x = rc.left;
        if (flags & F_CENTER) x = rc.left + (maxw - w) / 2;
        else if (flags & F_RIGHT) x = rc.right - w;
        int base = y + f.ascent;
        int pen = x;
        for (unsigned char c : r.text) {
            if (c == '\t' && (flags & F_EXPANDTABS)) {
                int rel = pen - x;
                pen += tab > 0 ? tab - (rel % tab) : 0;
                continue;
            }
            int cp = Cp(c);
            DrawGlyph(dc, f, cp, pen, base, col, clip);
            pen += Advance(f, cp);
        }
        if (f.underline && w > 0) {
            int th = std::max(1, f.px / 14);
            for (int t = 0; t < th; ++t)
                for (int xx = x; xx < x + w; ++xx) {
                    int yy = base + 1 + t;
                    if (!clip || (xx >= clip->left && xx < clip->right && yy >= clip->top && yy < clip->bottom)) PutPixel(dc, xx, yy, col);
                }
        }
        y += lh;
    }
}

} // namespace ttf
} // namespace gdi
} // namespace moses
