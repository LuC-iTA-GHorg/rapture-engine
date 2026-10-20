// --autoplay: deterministic, non-intrusive scripted run of the real game (see docs/AUTOPLAY.md).
//
//   <game>.exe --autoplay script.txt [--outdir DIR]
//
// The SDL window is created HIDDEN, real OS mouse/keyboard events are discarded, and the script's input is injected at the
// level where the platform Window delivers events to aaron (so the game code paths are the real ones). Nothing here is
// linked into the normal run: without --autoplay no hook is installed and platform::Window behaves as before.
//
// The engine knows the generic commands (wait, waitfor dialog|nodialog|scene, move, click, rclick, button, key, keydown, keyup, shot, log, quit).
// Commands that need to look inside a game (enemies, score, ...) are added by the game through an Extension, registered in
// HostConfig::autoplay.
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace aaron {
class Dialog;
class Scene;
class Widget;
} // namespace aaron

namespace autoplay {

// Game-specific part of the script language.  All methods have do-nothing defaults.
class Extension {
public:
    virtual ~Extension() = default;

    // ---- extra script commands ("fire hun 300 special") ---------------------------------------------------------
    // Parse time: `word` is an unknown command (lower case), `args` the rest of the line split at blanks.  Return false for a
    // syntax error / unknown command (the run is aborted with "script error at line N").
    virtual bool ParseCommand(const std::string& word, const std::vector<std::string>& args) { return false; }
    // Run time: the command is reached.  Return true to make the engine click at out.x/out.y with out.button (1 left, 3 right:
    // move, press, release, with the usual settle times); false = nothing to click (the command is done).
    struct Click {
        int x = 0, y = 0, button = 1;
    };
    virtual bool Execute(const std::string& word, const std::vector<std::string>& args, Click& out) { return false; }

    // ---- extra `waitfor <kind> [args...] <ms>` conditions ---------------------------------------------------------
    virtual bool ParseWaitFor(const std::string& kind, const std::vector<std::string>& args) { return false; }
    virtual bool CheckWaitFor(const std::string& kind, const std::vector<std::string>& args) { return false; }

    // ---- hooks ------------------------------------------------------------------------------------------------------
    virtual void OnTick(uint32_t nowMs) {}          // every frame of the script hook (sampling positions, ...)
    virtual void AppendLog(std::ostream& line) {}   // text appended to the `log` line, after "dialog=..." (start with a blank)
};

// The game's extension (HostConfig::autoplay); null = generic commands only.  HostMain installs it.
void SetExtension(Extension* ext);

// Parses the script and switches the platform Window to scripted mode (hidden window, injected input, script hook).
// Returns false (with a message on stdout) if the script cannot be read or has a syntax error.
bool Start(const char* scriptPath, const char* outDir);

// 0 = script finished normally; 1 = a waitfor timed out / a command failed; 2 = script error.
int ExitCode();

// ---- inspection helpers for extensions (read-only) -------------------------------------------------------------------
aaron::Scene* CurrentScene();                               // the application's current scene, or null
aaron::Dialog* OpenDialog();                                // the visible (innermost) open dialog, or null
bool WidgetCenter(aaron::Widget* w, int& x, int& y);        // centre of the widget in 640x480 framebuffer coordinates
bool SpriteCenter(aaron::Widget* w, int& x, int& y);        // same, only for a visible widget that is inside the framebuffer

} // namespace autoplay
