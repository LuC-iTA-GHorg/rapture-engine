// Port debug overlay for the dead rapture.ini ComposeRects/DisplayRects flags (WP-9). See DebugOverlay.h.
#include "moses/DebugOverlay.h"

#include "moses/Ini.h"

namespace moses {

namespace {
bool g_enabled = false;
std::vector<RECT> g_compose, g_display;
constexpr size_t kMaxPending = 4096; // PresentTo clears the lists every frame; this only guards against a missing present

void Outline(::Surface& fb, int l, int t, int r, int b, uint32_t argb) { // r/b exclusive
    if (r <= l || b <= t) return;
    fb.FillRect(::Rect{l, t, r, t + 1}, argb);
    fb.FillRect(::Rect{l, b - 1, r, b}, argb);
    fb.FillRect(::Rect{l, t, l + 1, b}, argb);
    fb.FillRect(::Rect{r - 1, t, r, b}, argb);
}
} // namespace

void DebugOverlay::Enable(bool on) {
    g_enabled = on;
    if (!on) { g_compose.clear(); g_display.clear(); }
}
bool DebugOverlay::Enabled() { return g_enabled; }

void DebugOverlay::LoadIni() {
    if (IniGetInt("port", "DebugRects", 0) != 0) Enable(true);
}

void DebugOverlay::NoteCompose(const RECT& r) {
    if (g_enabled && g_compose.size() < kMaxPending) g_compose.push_back(r);
}
void DebugOverlay::NoteDisplay(const RECT& r) {
    if (g_enabled && g_display.size() < kMaxPending) g_display.push_back(r);
}
size_t DebugOverlay::PendingCompose() { return g_compose.size(); }
size_t DebugOverlay::PendingDisplay() { return g_display.size(); }

void DebugOverlay::Draw(::Surface& fb, const RECT& surface) {
    if (!g_enabled) return;
    for (const RECT& r : g_compose)
        Outline(fb, r.left - surface.left, r.top - surface.top, r.right - surface.left, r.bottom - surface.top, kComposeColor);
    for (const RECT& r : g_display) // drawn last: green stays visible where both coincide
        Outline(fb, r.left - surface.left, r.top - surface.top, r.right - surface.left, r.bottom - surface.top, kDisplayColor);
    g_compose.clear();
    g_display.clear();
}

} // namespace moses
