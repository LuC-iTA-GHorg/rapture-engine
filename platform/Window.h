// SDL2 window with a 640x480 logical framebuffer scaled to the window.
//
// - Framebuffer() is a 640x480 ARGB Surface; Present() uploads it and letterboxes/scales it.
// - F11 and Alt+Enter toggle desktop fullscreen (handled inside PollEvent, not forwarded).
// - Mouse coordinates in events are already mapped to 0..639 x 0..479 (SDL logical size).
// - FrameTimer provides fixed-rate pacing.
// No game/engine API here: just window, input events and timing.
#pragma once

#include "platform/Gfx.h"

#include <cstdint>
#include <deque>
#include <functional>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

struct InputEvent {
    enum Type { kNone, kQuit, kMouseMove, kMouseDown, kMouseUp, kKeyDown, kKeyUp, kText } type = kNone;
    int x = 0, y = 0;  // mouse, framebuffer coordinates
    int button = 0;    // SDL_BUTTON_LEFT=1, MIDDLE=2, RIGHT=3
    int key = 0;       // SDL_Keycode for key events
    int mod = 0;       // SDL_Keymod bits
    char text = 0;     // ASCII for kText
};

class Window {
public:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 480;

    Window() = default;
    ~Window() { Destroy(); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Initialises SDL video, creates window + renderer + streaming texture.
    bool Create(const char* title, bool fullscreen = false);
    void Destroy();
    bool IsOpen() const { return window_ != nullptr; }

    Surface& Framebuffer() { return fb_; }
    void Present();

    // Returns false when the queue is empty. Fullscreen hotkeys are consumed here.
    bool PollEvent(InputEvent& ev);

    void ToggleFullscreen();
    bool IsFullscreen() const;
    void SetTitle(const char* title);
    void ShowCursor(bool show);

    static uint32_t Ticks(); // ms since SDL init (scaled by SetTimeScale: the one virtual game clock)
    // --fast N (autoplay only): the virtual clock runs N times faster than real time; frame pacing, the engine main-loop sleep, movie
    // pacing and the mixer position (Audio) follow it. 1 = off (default). "max" = scale 20 and no frame sleeping (NoSleep()).
    static void SetTimeScale(double scale, bool noSleep = false);
    static double TimeScale();
    static bool NoSleep();

    // ---- scripted mode (--autoplay, source/autoplay/): the window is created hidden, real OS input is discarded and
    // PollEvent() delivers only events injected with PushEvent(). The hook is called by PollEvent() whenever the
    // injected queue is empty (so it also runs inside blocking loops such as PlayMovie). Off by default.
    static void SetScriptedMode(bool on);
    static bool ScriptedMode();
    static void SetScriptHook(std::function<void(Window&)> hook);
    void PushEvent(const InputEvent& ev) { injected_.push_back(ev); }
    // Hands an event back to the next PollEvent() (also in normal mode): PlayMovie returns the skip/quit event it consumed so the
    // scene sees it like in the original, where the AVI is played by the OS and the key goes to the scene's listeners.
    void PushEventFront(const InputEvent& ev) { injected_.push_front(ev); }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    Surface fb_{kWidth, kHeight};
    std::deque<InputEvent> injected_;
};

// Fixed-rate frame pacing (sleep + spin on a high-resolution clock). Skips ahead rather than
// bursting if the caller falls behind.
class FrameTimer {
public:
    explicit FrameTimer(double fps = 60.0);
    void Reset();
    void Wait(); // blocks until the next frame boundary
    double Fps() const { return fps_; }

private:
    double fps_;
    uint64_t next_ = 0, period_ = 0;
};
