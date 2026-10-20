#include "platform/Window.h"

#include <SDL.h>

static bool g_scripted = false;
static std::function<void(Window&)> g_scriptHook;
void Window::SetScriptedMode(bool on) { g_scripted = on; }
bool Window::ScriptedMode() { return g_scripted; }
void Window::SetScriptHook(std::function<void(Window&)> hook) { g_scriptHook = std::move(hook); }

bool Window::Create(const char* title, bool fullscreen) {
    if (window_) return true;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) return false;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (g_scripted) flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI; // never shown, never takes focus
    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kWidth, kHeight, flags);
    if (!window_) return false;
    if (!g_scripted) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, 0);
    if (!renderer_) {
        Destroy();
        return false;
    }
    SDL_RenderSetLogicalSize(renderer_, kWidth, kHeight); // also maps mouse events to 640x480
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
    if (!texture_) {
        Destroy();
        return false;
    }
    return true;
}

void Window::Destroy() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    texture_ = nullptr;
    renderer_ = nullptr;
    if (window_) SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    window_ = nullptr;
}

void Window::Present() {
    if (!window_) return;
    SDL_UpdateTexture(texture_, nullptr, fb_.Pixels(), kWidth * 4);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

bool Window::IsFullscreen() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void Window::ToggleFullscreen() {
    if (window_) SDL_SetWindowFullscreen(window_, IsFullscreen() ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void Window::SetTitle(const char* title) {
    if (window_) SDL_SetWindowTitle(window_, title);
}

void Window::ShowCursor(bool show) { SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE); }

namespace {
double g_scale = 1.0;
bool g_noSleep = false;
uint32_t g_real0 = 0;
double g_virt0 = 0;
} // namespace

uint32_t Window::Ticks() {
    if (g_scale == 1.0) return SDL_GetTicks();
    return uint32_t(g_virt0 + double(uint32_t(SDL_GetTicks() - g_real0)) * g_scale);
}

void Window::SetTimeScale(double scale, bool noSleep) {
    if (scale < 1.0) scale = 1.0;
    const uint32_t v = Ticks(); // keep the clock continuous across the change
    g_real0 = SDL_GetTicks();
    g_virt0 = v;
    g_scale = scale;
    g_noSleep = noSleep && scale > 1.0;
}
double Window::TimeScale() { return g_scale; }
bool Window::NoSleep() { return g_noSleep; }

static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool Window::PollEvent(InputEvent& out) {
    if (g_scripted) { // scripted mode: discard real OS input, deliver only the injected events
        SDL_Event d;
        while (SDL_PollEvent(&d)) {}
        if (injected_.empty() && g_scriptHook) g_scriptHook(*this);
        if (injected_.empty()) return false;
        out = injected_.front();
        injected_.pop_front();
        return true;
    }
    if (!injected_.empty()) { // events handed back with PushEventFront
        out = injected_.front();
        injected_.pop_front();
        return true;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        out = InputEvent();
        switch (e.type) {
        case SDL_QUIT:
            out.type = InputEvent::kQuit;
            return true;
        case SDL_MOUSEMOTION:
            out.type = InputEvent::kMouseMove;
            out.x = ClampI(e.motion.x, 0, kWidth - 1);
            out.y = ClampI(e.motion.y, 0, kHeight - 1);
            return true;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            out.type = e.type == SDL_MOUSEBUTTONDOWN ? InputEvent::kMouseDown : InputEvent::kMouseUp;
            out.x = ClampI(e.button.x, 0, kWidth - 1);
            out.y = ClampI(e.button.y, 0, kHeight - 1);
            out.button = e.button.button;
            return true;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            SDL_Keycode k = e.key.keysym.sym;
            bool alt = (e.key.keysym.mod & KMOD_ALT) != 0;
            if (e.type == SDL_KEYDOWN && !e.key.repeat &&
                (k == SDLK_F11 || (alt && (k == SDLK_RETURN || k == SDLK_KP_ENTER)))) {
                ToggleFullscreen();
                break;
            }
            out.type = e.type == SDL_KEYDOWN ? InputEvent::kKeyDown : InputEvent::kKeyUp;
            out.key = int(k);
            out.mod = e.key.keysym.mod;
            return true;
        }
        case SDL_TEXTINPUT:
            if (e.text.text[0] && static_cast<unsigned char>(e.text.text[0]) < 0x80) {
                out.type = InputEvent::kText;
                out.text = e.text.text[0];
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

FrameTimer::FrameTimer(double fps) : fps_(fps) {
    period_ = uint64_t(double(SDL_GetPerformanceFrequency()) / (fps * Window::TimeScale()));
    Reset();
}

void FrameTimer::Reset() { next_ = SDL_GetPerformanceCounter() + period_; }

void FrameTimer::Wait() {
    const uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t now = SDL_GetPerformanceCounter();
    if (Window::NoSleep()) {
        next_ = now + period_;
        return;
    }
    if (now < next_) {
        uint64_t ms = (next_ - now) * 1000 / freq;
        if (ms > 1) SDL_Delay(Uint32(ms - 1));
        while (SDL_GetPerformanceCounter() < next_) {
        }
        next_ += period_;
    } else if (now - next_ > period_ * 4) {
        next_ = now + period_; // fell far behind: resynchronise
    } else {
        next_ += period_;
    }
}
