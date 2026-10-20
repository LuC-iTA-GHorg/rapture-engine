// Port debug overlay (WP-9): outlines of the rectangles CDrawView recomposes / displays.
//
// The original has rapture.ini [drawlib] ComposeRects / DisplayRects ("show recomposed / display rectangles") but the flags are
// DEAD: CDrawSys stores them (+8 / +0xc) and nothing ever reads them (ShowComposeRects/ShowDisplayRects have no callers).
// The port keeps them dead; this overlay is an extra, OFF by default:
//   * rapture.ini   [port] DebugRects=1       * command line  --debug-rects
// Red outline = rectangle recomposed for one object (Compose clip), green = rectangle displayed (Display).
// Rectangles are collected by CDrawView::Display and drawn (and cleared) by CDrawView::PresentTo on the framebuffer.
#pragma once

#include "moses/Types.h"

#include <vector>

namespace moses {

class DebugOverlay {
public:
    static void Enable(bool on);
    static bool Enabled();
    static void LoadIni();                  // reads [port] DebugRects (only ever turns the overlay ON)

    static void NoteCompose(const RECT& r); // view coordinates (origin = centre)
    static void NoteDisplay(const RECT& r);
    static size_t PendingCompose();
    static size_t PendingDisplay();

    // Draws the pending rectangles as 1px outlines on `fb` (surface = view surface rect: maps view -> framebuffer
    // coordinates) and clears the lists. Does nothing when disabled.
    static void Draw(::Surface& fb, const RECT& surface);

    static constexpr uint32_t kComposeColor = 0xFFFF0000u; // red
    static constexpr uint32_t kDisplayColor = 0xFF00FF00u; // green
};

} // namespace moses
