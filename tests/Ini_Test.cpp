// WP-9 tests: rapture.ini reader/writer, CDrawSys options, port debug-rects overlay, RemapDrums. `<game>.exe --engine-test 9`.
// Headless: temporary ini file in the current folder, software Surfaces, synthetic compact MIDI. No window, no audio device.
#include "tests/EngineTests.h"

#include "moses/AudioLib.h"
#include "moses/DebugOverlay.h"
#include "moses/DrawView.h"
#include "moses/Ini.h"
#include "platform/Midi.h"

#include <cstdio>
#include <fstream>

using namespace moses;

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

const char* kTmp = "rapture_wp9_test.ini";

void WriteFile(const char* text) {
    std::ofstream o(kTmp, std::ios::binary | std::ios::trunc);
    o << text;
}
std::string ReadFile() {
    std::ifstream i(kTmp, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>());
}

uint32_t Dw(uint8_t st, uint8_t d1, uint8_t d2) { return st | d1 << 8 | d2 << 16; }
std::vector<uint8_t> Stream(std::initializer_list<uint32_t> l) {
    std::vector<uint8_t> v;
    for (uint32_t d : l) for (int k = 0; k < 4; ++k) v.push_back(uint8_t(d >> (8 * k)));
    return v;
}
uint32_t At(const std::vector<uint8_t>& v, size_t i) {
    return uint32_t(v[i * 4]) | uint32_t(v[i * 4 + 1]) << 8 | uint32_t(v[i * 4 + 2]) << 16 | uint32_t(v[i * 4 + 3]) << 24;
}

void TestIniRead() {
    std::remove(kTmp);
    IniSetFile(kTmp);
    // no file: defaults (short sign-extended)
    CHECK(IniGetInt("drawlib", "NoDirectDraw", 0) == 0);
    CHECK(IniGetInt("drawlib", "NoDirectDraw", 7) == 7);
    CHECK(IniGetInt("x", "y", -1) == 0xFFFFFFFFu);
    WriteFile("; comment\r\n[drawlib]\r\nNoDirectDraw = 1\r\nComposeRects=-3\r\nDisplayRects=abc\r\n"
              "Empty=\r\nHex=0x10\r\nTrail=12xyz\r\n\r\n[Audio]\r\nremapdrums=1\n[other]\nNoDirectDraw=9\n");
    CHECK(IniGetInt("drawlib", "NoDirectDraw", 0) == 1);          // spaces around '=' trimmed
    CHECK(i16(IniGetInt("drawlib", "ComposeRects", 0)) == -3);    // negative, wraps as unsigned
    CHECK(IniGetInt("drawlib", "DisplayRects", 5) == 0);          // present but not a number -> 0
    CHECK(IniGetInt("drawlib", "Empty", 5) == 5);                 // empty value -> default
    CHECK(IniGetInt("drawlib", "Hex", 5) == 0);                   // decimal only
    CHECK(IniGetInt("drawlib", "Trail", 5) == 12);                // trailing text ignored
    CHECK(IniGetInt("DRAWLIB", "nodirectdraw", 0) == 1);          // case-insensitive
    CHECK(IniGetInt("audio", "RemapDrums", 0) == 1);              // LF-only lines, other case
    CHECK(IniGetInt("other", "NoDirectDraw", 0) == 9);            // sections are separate
    CHECK(IniGetInt("drawlib", "Missing", 4) == 4);
    CHECK(IniGetInt("nosection", "NoDirectDraw", 3) == 3);
    std::string s;
    CHECK(IniGetString("drawlib", "Trail", s) && s == "12xyz");
    CHECK(!IniGetString("drawlib", "Nope", s));
}

void TestIniWrite() {
    std::remove(kTmp);
    IniSetFile(kTmp);
    IniWriteInt("audio", "RemapDrums", 1);                         // creates file + section
    CHECK(ReadFile() == "[audio]\r\nRemapDrums=1\r\n");
    IniWriteInt("drawlib", "ComposeRects", -2);
    IniWriteString("audio", "Other", "x y");
    CHECK(IniGetInt("audio", "RemapDrums", 0) == 1);
    CHECK(i16(IniGetInt("drawlib", "ComposeRects", 0)) == -2);
    std::string s;
    CHECK(IniGetString("audio", "Other", s) && s == "x y");
    IniWriteInt("audio", "RemapDrums", 0);                         // replaces in place
    CHECK(IniGetInt("audio", "RemapDrums", 9) == 0);
    CHECK(ReadFile() == "[audio]\r\nRemapDrums=0\r\nOther=x y\r\n\r\n[drawlib]\r\nComposeRects=-2\r\n");
    IniWriteString("audio", "Other", nullptr);                     // delete
    CHECK(!IniGetString("audio", "Other", s));
    CHECK(IniGetInt("drawlib", "ComposeRects", 0) != 0);
    std::remove(kTmp);
}

void TestDrawSysAndOverlay() {
    std::remove(kTmp);
    IniSetFile(kTmp);
    WriteFile("[drawlib]\nComposeRects=1\nDisplayRects=2\nNoDirectDraw=1\n");
    CDrawSys sys;
    CHECK(sys.ShowComposeRects() == 1 && sys.ShowDisplayRects() == 2 && sys.NoDirectDraw() == 1);
    CHECK(sys.ChangeDisplayMode(nullptr, 640, 480, 16) == 1);       // port: no display mode change, reports success
    CHECK(!DebugOverlay::Enabled());                                 // dead flags do NOT enable the overlay
    std::remove(kTmp);
    CDrawSys def;
    CHECK(def.ShowComposeRects() == 0 && def.ShowDisplayRects() == 0 && def.NoDirectDraw() == 0);

    // [port] DebugRects=1 turns the overlay on
    WriteFile("[port]\nDebugRects=1\n");
    DebugOverlay::LoadIni();
    CHECK(DebugOverlay::Enabled());
    DebugOverlay::Enable(false);

    // direct drawing into a Surface
    Surface fb(64, 48);
    fb.Clear(0xFF000000u);
    RECT surf{-32, -24, 32, 24};
    DebugOverlay::NoteCompose(RECT{-30, -20, -10, -10}); // ignored: disabled
    CHECK(DebugOverlay::PendingCompose() == 0);
    DebugOverlay::Enable(true);
    DebugOverlay::NoteCompose(RECT{-30, -20, -10, -10});
    DebugOverlay::NoteDisplay(RECT{-32, -24, 32, 24});
    CHECK(DebugOverlay::PendingCompose() == 1 && DebugOverlay::PendingDisplay() == 1);
    DebugOverlay::Draw(fb, surf);
    CHECK(DebugOverlay::PendingCompose() == 0 && DebugOverlay::PendingDisplay() == 0);
    CHECK(fb.Row(0)[0] == DebugOverlay::kDisplayColor);              // display outline = frame border
    CHECK(fb.Row(47)[63] == DebugOverlay::kDisplayColor);
    CHECK(fb.Row(4)[2] == DebugOverlay::kComposeColor);              // compose rect (2,4)-(22,14): left edge
    CHECK(fb.Row(4)[10] == DebugOverlay::kComposeColor);             // top edge
    CHECK(fb.Row(13)[21] == DebugOverlay::kComposeColor);            // bottom-right corner (exclusive bounds)
    CHECK(fb.Row(8)[10] == 0xFF000000u);                             // interior untouched
    CHECK(fb.Row(30)[30] == 0xFF000000u);

    // through CDrawView: Display records the rects, PresentTo draws them over the copied frame
    CDrawView view;
    CHECK(view.Init(nullptr, nullptr, 64, 48, 16) == 1);
    DebugOverlay::Enable(true);
    RECT all = view.ViewData().surface;
    view.Display(all);
    CHECK(DebugOverlay::PendingDisplay() >= 1);
    Surface out(64, 48);
    out.Clear(0xFF123456u);
    CHECK(view.PresentTo(out));
    CHECK(out.Row(0)[0] == DebugOverlay::kDisplayColor);
    CHECK(out.Row(47)[0] == DebugOverlay::kDisplayColor);
    CHECK(out.Row(20)[30] == 0xFF000000u);                           // rest of the frame = view surface (black)
    CHECK(DebugOverlay::PendingDisplay() == 0);
    DebugOverlay::Enable(false);
    out.Clear(0xFF123456u);
    view.Display(all);
    CHECK(DebugOverlay::PendingDisplay() == 0);                      // disabled: nothing recorded
    CHECK(view.PresentTo(out) && out.Row(0)[0] == 0xFF000000u);      // no outline when disabled
}

void TestRemapDrums() {
    CHECK(MidiRemapDrumsMsg(Dw(0x99, 36, 100)) == Dw(0x9f, 36, 100));
    CHECK(MidiRemapDrumsMsg(Dw(0x89, 36, 0)) == Dw(0x8f, 36, 0));
    CHECK(MidiRemapDrumsMsg(Dw(0xb9, 7, 90)) == Dw(0xbf, 7, 90));
    CHECK(MidiRemapDrumsMsg(Dw(0x90, 36, 100)) == Dw(0x90, 36, 100));  // other channels untouched
    CHECK(MidiRemapDrumsMsg(Dw(0x9e, 36, 100)) == Dw(0x9e, 36, 100));
    auto in = Stream({Dw(0x99, 36, 100), 0x80000000u | 120, Dw(0x90, 60, 90), Dw(0x89, 36, 0), Dw(0xc9, 5, 0)});
    auto off = MidiPrepareCompact(in, false, 0, 1, 1);
    CHECK(off == in);                                                 // option off: stream untouched
    auto on = MidiPrepareCompact(in, true, 0, 1, 1);
    CHECK(on.size() == in.size());
    CHECK(At(on, 0) == Dw(0x9f, 36, 100) && At(on, 1) == (0x80000000u | 120) && At(on, 2) == Dw(0x90, 60, 90));
    CHECK(At(on, 3) == Dw(0x8f, 36, 0) && At(on, 4) == Dw(0xcf, 5, 0));
    // the mute test sees the REMAPPED channel (remap first, like the original thread)
    auto m15 = MidiPrepareCompact(in, true, 1u << 15, 1, 1);
    CHECK(m15.size() == in.size() - 4 && At(m15, 0) == (0x80000000u | 120));
    auto m9 = MidiPrepareCompact(in, true, 1u << 9, 1, 1);
    CHECK(m9.size() == in.size());                                   // channel 9 no longer exists after the remap
    auto m9off = MidiPrepareCompact(in, false, 1u << 9, 1, 1);
    CHECK(m9off.size() == in.size() - 4);
    // SMF export carries the remapped status
    auto smf = CompactMidiToSmf(on.data(), on.size());
    bool has9f = false, has99 = false;
    for (size_t i = 0; i + 2 < smf.size(); ++i) {
        if (smf[i] == 0x9f && smf[i + 1] == 36) has9f = true;
        if (smf[i] == 0x99 && smf[i + 1] == 36) has99 = true;
    }
    CHECK(has9f && !has99);

    // CAudioLib reads [audio] RemapDrums in its constructor
    std::remove(kTmp);
    IniSetFile(kTmp);
    { CAudioLib a; CHECK(a.RemapDrums() == 0); }
    WriteFile("[audio]\nRemapDrums=1\n");
    { CAudioLib a; CHECK(a.RemapDrums() == 1); }
    std::remove(kTmp);
}

} // namespace

int RunPkg9Tests(int, char**) {
    std::printf("WP-9 tests (rapture.ini, debug rects, RemapDrums)\n");
    TestIniRead();
    TestIniWrite();
    TestDrawSysAndOverlay();
    TestRemapDrums();
    IniSetFile(nullptr);
    std::remove(kTmp);
    std::printf("%d checks, %d failures%s\n", g_checks, g_fail, g_fail ? "" : " - OK");
    return g_fail ? 1 : 0;
}
