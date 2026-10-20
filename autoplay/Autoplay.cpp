// --autoplay script runner. Script syntax (one command per line, '#' starts a comment; coordinates in the 640x480 framebuffer):
//   wait <ms>                       let the game run for <ms> of real time
//   waitfor dialog|nodialog <ms>    until a modal Dialog is open / none is open
//   waitfor scene <name> <ms>       until the current aaron scene is called <name> (the host's "Title Screen", the game's scene, ...)
//   move x y | click x y | rclick x y      mouse (click = move + left/right press + release)
//   button <cmdId>                  click the centre of the Button with that command id in the open dialog
//   key <name|vk>                   press+release: space esc enter f1..f12 up down left right plus minus star a..z 0..9, or a numeric VK
//   shot <name>                     save the 640x480 framebuffer as <outdir>/<name>.png
//   log                             one line on stdout: time, scene, dialog, then whatever the game's Extension appends
//   quit                            end the run
// Any other command, and any other `waitfor <kind> [args] <ms>`, is offered to the game's autoplay::Extension (HostConfig::autoplay).
// Exit code: 0 ok, 1 a waitfor timed out or a command failed, 2 script error.
#include "autoplay/Autoplay.h"

#include "aaron/Application.h"
#include "aaron/Input.h"
#include "aaron/Scene.h"
#include "aaron/Sprites.h"
#include "platform/Window.h"

#include <SDL.h>
#include <cxxabi.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <typeinfo>
#include <vector>

namespace autoplay {
namespace {

enum Op { O_WAIT, O_WAITFOR, O_MOVE, O_DOWN, O_UP, O_KEYDOWN, O_KEYUP, O_BUTTON, O_SHOT, O_LOG, O_EXT, O_QUIT };

struct Cmd {
    Op op = O_WAIT;
    int a = 0, b = 0;      // numbers: ms / x,y / button id / SDL key / mouse button / timeout
    std::string s, s2;     // names
    std::vector<std::string> args;   // O_EXT / extension waitfor: the words after the command / the kind
    int settle = 0;        // ms to let the game run after an injecting command
    int line = 0;
};

struct State {
    std::vector<Cmd> cmds;
    size_t pc = 0;
    std::string outDir = ".";
    bool started = false, done = false, waiting = false;
    uint32_t t0 = 0, notBefore = 0, waitUntil = 0, deadline = 0, doneAt = 0;
    int mx = 320, my = 240;
    int exitCode = 0;
    Extension* ext = nullptr;
};
State g;

void Fail(const Cmd& c, const char* what, int code = 1) {
    std::printf("AUTOPLAY FAIL line %d: %s\n", c.line, what);
    std::fflush(stdout);
    g.exitCode = code;
}

// ---------------------------------------------------------------------------------------------
// Key names -> SDL keycodes (the keycodes that aaron::SdlKeyToVk understands).
bool KeyFromName(const std::string& n, int& key) {
    const int M = 1 << 30;
    static const std::map<std::string, int> names = {
        {"space", 32}, {"esc", 27}, {"escape", 27}, {"enter", 13}, {"return", 13}, {"tab", 9}, {"backspace", 8},
        {"up", M | 82}, {"down", M | 81}, {"left", M | 80}, {"right", M | 79}, {"plus", 43}, {"minus", 45}, {"star", 42},
        {"pause", M | 72}};
    auto it = names.find(n);
    if (it != names.end()) { key = it->second; return true; }
    if (n.size() >= 2 && n[0] == 'f') {
        int f = std::atoi(n.c_str() + 1);
        if (f >= 1 && f <= 12) { key = M | (58 + f - 1); return true; }
    }
    if (n.size() == 1 && ((n[0] >= 'a' && n[0] <= 'z') || (n[0] >= '0' && n[0] <= '9'))) { key = n[0]; return true; }
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') { key = n[0] - 'A' + 'a'; return true; }
    if (!n.empty() && std::isdigit((unsigned char)n[0])) { // numeric VK: find the SDL key that maps to it
        unsigned vk = unsigned(std::strtoul(n.c_str(), nullptr, 0));
        for (int k = 8; k < 128; ++k)
            if (aaron::SdlKeyToVk(k) == vk) { key = k; return true; }
        for (int s = 40; s < 260; ++s)
            if (aaron::SdlKeyToVk(M | s) == vk) { key = M | s; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Tiny PNG writer (RGB, 8 bit, stored deflate blocks: no compression library needed).
uint32_t Crc(const uint8_t* p, size_t n) {
    static uint32_t tab[256];
    if (!tab[1])
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xedb88320u ^ (v >> 1) : v >> 1;
            tab[i] = v;
        }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) c = tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
void Put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back(uint8_t(x >> (8 * i))); }
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    Put32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    Put32(out, Crc(td.data(), td.size()));
}
bool WritePng(const std::string& path, const Surface& s) {
    const int w = s.Width(), h = s.Height();
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (size_t(w) * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        const uint32_t* row = s.Row(y);
        for (int x = 0; x < w; ++x) { raw.push_back(uint8_t(row[x] >> 16)); raw.push_back(uint8_t(row[x] >> 8)); raw.push_back(uint8_t(row[x])); }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t pos = 0; pos < raw.size();) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n >= raw.size() ? 1 : 0);
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8)); z.push_back(uint8_t(~n)); z.push_back(uint8_t((~n) >> 8));
        z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    Put32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a}, ihdr;
    Put32(ihdr, uint32_t(w));
    Put32(ihdr, uint32_t(h));
    for (uint8_t v : {8, 2, 0, 0, 0}) ihdr.push_back(v);
    Chunk(out, "IHDR", ihdr);
    Chunk(out, "IDAT", z);
    Chunk(out, "IEND", {});
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    return bool(f);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Game inspection (read-only), also for the game's Extension.
aaron::Scene* CurrentScene() {
    aaron::Application* app = aaron::Application::GetApp();
    return app ? app->GetScene() : nullptr;
}

namespace {
void CollectDialogs(aaron::TreeNode* n, std::vector<aaron::Dialog*>& out) {
    for (aaron::TreeNode* c = n ? n->FirstChild() : nullptr; c; c = c->NextSibling()) {
        if (auto* d = dynamic_cast<aaron::Dialog*>(c)) out.push_back(d);
        CollectDialogs(c, out);
    }
}
} // namespace

// The visible (innermost) open dialog, or nullptr.
aaron::Dialog* OpenDialog() {
    aaron::Scene* sc = CurrentScene();
    if (!sc || !sc->RootWidget()) return nullptr;
    std::vector<aaron::Dialog*> v;
    CollectDialogs(sc->RootWidget(), v);
    for (size_t i = v.size(); i-- > 0;)
        if (!v[i]->IsHidden()) return v[i];
    return v.empty() ? nullptr : v.back();
}

bool WidgetCenter(aaron::Widget* w, int& x, int& y) {
    aaron::Scene* sc = CurrentScene();
    moses::CDrawObj* o = w ? w->GetDrawObj() : nullptr;
    if (!sc || !o) return false;
    const moses::RECT* r = o->GetBounds();
    x = (r->left + r->right) / 2 + sc->Origin().x;
    y = (r->top + r->bottom) / 2 + sc->Origin().y;
    return true;
}

bool SpriteCenter(aaron::Widget* w, int& x, int& y) {
    moses::CDrawObj* o = w ? w->GetDrawObj() : nullptr;
    if (!o || !o->IsVisible() || w->IsHidden()) return false;
    return WidgetCenter(w, x, y) && x >= 0 && y >= 0 && x < 640 && y < 480;
}

void SetExtension(Extension* ext) { g.ext = ext; }

namespace {

std::string ClassName(const std::type_info& ti) {
    int st = 0;
    char* d = abi::__cxa_demangle(ti.name(), nullptr, nullptr, &st);
    std::string r = (st == 0 && d) ? d : ti.name();
    std::free(d);
    return r;
}

aaron::Button* FindButton(aaron::TreeNode* n, uint32_t id) {
    for (aaron::TreeNode* c = n ? n->FirstChild() : nullptr; c; c = c->NextSibling()) {
        if (auto* b = dynamic_cast<aaron::Button*>(c))
            if (b->GetCmdId() == id) return b;
        if (auto* r = FindButton(c, id)) return r;
    }
    return nullptr;
}

void PrintLog(uint32_t now, const Surface* fb = nullptr) {
    aaron::Scene* sc = CurrentScene();
    aaron::Dialog* d = OpenDialog();
    std::ostringstream o;
    o << "[autoplay] t=" << (now - g.t0) << "ms scene=" << (sc ? sc->Name() : "(none)")
      << " dialog=" << (d ? ClassName(typeid(*d)) + (d->IsHidden() ? "(hidden)" : "") : "-");
    if (g.ext) g.ext->AppendLog(o);
    if (fb) { // non-black pixel count of the framebuffer: tells "something is on screen" without looking at a PNG
        long nb = 0;
        for (int y = 0; y < fb->Height(); ++y) { const uint32_t* r = fb->Row(y); for (int x = 0; x < fb->Width(); ++x) if (r[x] & 0xf0f0f0) ++nb; }
        o << " nonblack=" << nb;
    }
    std::printf("%s\n", o.str().c_str());
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------------------------
void Push(::Window& w, InputEvent::Type t, int x, int y, int button = 0, int key = 0) {
    InputEvent e;
    e.type = t; e.x = x; e.y = y; e.button = button; e.key = key;
    w.PushEvent(e);
}

bool Condition(const Cmd& c) {
    aaron::Scene* sc = CurrentScene();
    if (c.s == "dialog") return OpenDialog() != nullptr;
    if (c.s == "nodialog") return OpenDialog() == nullptr;
    if (c.s == "scene") return sc && sc->Name() == c.s2;
    return g.ext && g.ext->CheckWaitFor(c.s, c.args);
}

void Finish(uint32_t now, int code) {
    if (code > g.exitCode) g.exitCode = code;
    g.done = true;
    g.doneAt = now;
    if (aaron::Application* app = aaron::Application::GetApp()) app->Quit();
}

void Hook(::Window& w) {
    const uint32_t now = ::Window::Ticks();
    if (!g.started) { g.started = true; g.t0 = now; }
    if (g.done) { // the loop should end by itself; if it is stuck (e.g. a blocking loop), force the exit
        if (now - g.doneAt > 3000) { std::fflush(stdout); std::_Exit(g.exitCode); }
        return;
    }
    if (g.ext) g.ext->OnTick(now);
    if (int32_t(now - g.notBefore) < 0) return;
    while (g.pc < g.cmds.size()) {
        Cmd c = g.cmds[g.pc];
        switch (c.op) {
        case O_WAIT:
            if (!g.waiting) { g.waiting = true; g.waitUntil = now + uint32_t(c.a); }
            if (int32_t(now - g.waitUntil) < 0) return;
            g.waiting = false; ++g.pc; break;
        case O_WAITFOR:
            if (!g.waiting) { g.waiting = true; g.deadline = now + uint32_t(c.a); }
            if (Condition(c)) { g.waiting = false; ++g.pc; break; }
            if (int32_t(now - g.deadline) >= 0) {
                std::string what = "waitfor " + c.s + (c.s2.empty() ? "" : " " + c.s2);
                for (const std::string& a : c.args) what += " " + a;
                what += " timed out after " + std::to_string(c.a) + " ms";
                Fail(c, what.c_str());
                PrintLog(now);
                Finish(now, 1);
                return;
            }
            return;
        case O_MOVE: g.mx = c.a; g.my = c.b; Push(w, InputEvent::kMouseMove, c.a, c.b); ++g.pc; g.notBefore = now + uint32_t(c.settle); return;
        case O_DOWN: Push(w, InputEvent::kMouseDown, g.mx, g.my, c.a); ++g.pc; g.notBefore = now + uint32_t(c.settle); return;
        case O_UP: Push(w, InputEvent::kMouseUp, g.mx, g.my, c.a); ++g.pc; g.notBefore = now + uint32_t(c.settle); return;
        case O_KEYDOWN: Push(w, InputEvent::kKeyDown, 0, 0, 0, c.a); ++g.pc; g.notBefore = now + uint32_t(c.settle); return;
        case O_KEYUP: Push(w, InputEvent::kKeyUp, 0, 0, 0, c.a); ++g.pc; g.notBefore = now + uint32_t(c.settle); return;
        case O_BUTTON: case O_EXT: {
            int x = 0, y = 0, btn = 1;
            if (c.op == O_BUTTON) {
                aaron::Dialog* d = OpenDialog();
                aaron::Button* b = d ? FindButton(d, uint32_t(c.a)) : nullptr;
                if (!b || !WidgetCenter(b, x, y)) { Fail(c, "button: no such button in the open dialog"); Finish(now, 1); return; }
            } else {
                Extension::Click click;
                if (!g.ext || !g.ext->Execute(c.s, c.args, click)) { ++g.pc; break; }   // nothing to click: the command is done
                x = click.x; y = click.y; btn = click.button;
            }
            Cmd m, dn, up;
            m.op = O_MOVE; m.a = x; m.b = y; m.settle = 30; m.line = c.line;
            dn.op = O_DOWN; dn.a = btn; dn.settle = 60; dn.line = c.line;
            up.op = O_UP; up.a = btn; up.settle = 60; up.line = c.line;
            g.cmds.erase(g.cmds.begin() + long(g.pc));
            g.cmds.insert(g.cmds.begin() + long(g.pc), {m, dn, up});
            break;
        }
        case O_SHOT: {
            std::string path = g.outDir + "/" + c.s + ".png";
            if (!WritePng(path, w.Framebuffer())) { Fail(c, ("cannot write " + path).c_str()); Finish(now, 1); return; }
            std::printf("[autoplay] shot %s\n", path.c_str());
            std::fflush(stdout);
            ++g.pc; break;
        }
        case O_LOG: PrintLog(now, &w.Framebuffer()); ++g.pc; break;
        case O_QUIT: ++g.pc; Finish(now, 0); return;
        }
    }
    Finish(now, 0); // end of script
}

} // namespace

bool Start(const char* scriptPath, const char* outDir) {
    std::ifstream f(scriptPath);
    if (!f) { std::printf("AUTOPLAY: cannot open script '%s'\n", scriptPath); g.exitCode = 2; return false; }
    if (outDir && *outDir) g.outDir = outDir;
    std::string text;
    int line = 0;
    while (std::getline(f, text)) {
        ++line;
        size_t h = text.find('#');
        if (h != std::string::npos) text.erase(h);
        std::istringstream in(text);
        std::string w;
        if (!(in >> w)) continue;
        for (char& ch : w) ch = char(std::tolower((unsigned char)ch));
        Cmd c;
        c.line = line;
        bool ok = true;
        auto num = [&](int& v) {
            std::string t;
            if (!(in >> t)) return false;
            char* e = nullptr;
            v = int(std::strtol(t.c_str(), &e, 0));
            return e && !*e;
        };
        if (w == "wait") { c.op = O_WAIT; ok = num(c.a); g.cmds.push_back(c); }
        else if (w == "waitfor") {
            c.op = O_WAITFOR;
            ok = bool(in >> c.s);
            if (ok && c.s == "scene") { // the name may contain spaces: everything before the last token (the timeout)
                std::string rest, tok;
                std::getline(in, rest);
                size_t e = rest.find_last_not_of(" \t\r");
                size_t sp = e == std::string::npos ? std::string::npos : rest.find_last_of(" \t", e);
                if (sp == std::string::npos) ok = false;
                else {
                    c.a = std::atoi(rest.substr(sp + 1, e - sp).c_str());
                    size_t b = rest.find_first_not_of(" \t");
                    c.s2 = b < sp ? rest.substr(b, rest.find_last_not_of(" \t", sp) - b + 1) : "";
                    ok = !c.s2.empty();
                }
            } else if (ok && (c.s == "dialog" || c.s == "nodialog")) {
                ok = num(c.a);
            } else if (ok) { // the game's conditions: waitfor <kind> [args...] <ms>
                std::vector<std::string> toks;
                std::string t;
                while (in >> t) toks.push_back(t);
                char* e = nullptr;
                ok = !toks.empty();
                if (ok) {
                    c.a = int(std::strtol(toks.back().c_str(), &e, 0));
                    ok = e && !*e;
                    toks.pop_back();
                    c.args = toks;
                    ok = ok && g.ext && g.ext->ParseWaitFor(c.s, c.args);
                }
            }
            g.cmds.push_back(c);
        }
        else if (w == "move") { c.op = O_MOVE; ok = num(c.a) && num(c.b); c.settle = 30; g.cmds.push_back(c); }
        else if (w == "click" || w == "rclick") {
            int x = 0, y = 0, btn = w == "click" ? 1 : 3;
            ok = num(x) && num(y);
            Cmd m, d, u;
            m.op = O_MOVE; m.a = x; m.b = y; m.settle = 30; m.line = line;
            d.op = O_DOWN; d.a = btn; d.settle = 60; d.line = line;
            u.op = O_UP; u.a = btn; u.settle = 60; u.line = line;
            g.cmds.push_back(m); g.cmds.push_back(d); g.cmds.push_back(u);
        }
        else if (w == "button") { c.op = O_BUTTON; ok = num(c.a); g.cmds.push_back(c); }
        else if (w == "key") {
            std::string n;
            int k = 0;
            ok = bool(in >> n) && KeyFromName(n, k);
            Cmd d, u;
            d.op = O_KEYDOWN; d.a = k; d.settle = 50; d.line = line;
            u.op = O_KEYUP; u.a = k; u.settle = 50; u.line = line;
            g.cmds.push_back(d); g.cmds.push_back(u);
        }
        else if (w == "keydown" || w == "keyup") { // hold / release a key (a game that moves while the key is down)
            std::string n;
            int k = 0;
            ok = bool(in >> n) && KeyFromName(n, k);
            c.op = w == "keydown" ? O_KEYDOWN : O_KEYUP;
            c.a = k;
            c.settle = 50;
            g.cmds.push_back(c);
        }
        else if (w == "shot") { c.op = O_SHOT; ok = bool(in >> c.s); g.cmds.push_back(c); }
        else if (w == "log") { c.op = O_LOG; g.cmds.push_back(c); }
        else if (w == "quit") { c.op = O_QUIT; g.cmds.push_back(c); }
        else { // a command of the game's Extension
            std::vector<std::string> args;
            std::string t;
            while (in >> t) args.push_back(t);
            ok = g.ext && g.ext->ParseCommand(w, args);
            if (ok) { c.op = O_EXT; c.s = w; c.args = args; g.cmds.push_back(c); }
        }
        if (!ok) {
            std::printf("AUTOPLAY: script error at line %d: '%s'\n", line, text.c_str());
            g.exitCode = 2;
            return false;
        }
    }
    std::printf("[autoplay] script %s: %zu steps, outdir %s\n", scriptPath, g.cmds.size(), g.outDir.c_str());
    std::fflush(stdout);
    ::Window::SetScriptedMode(true);
    ::Window::SetScriptHook(&Hook);
    // Silent by default: a verification run must not play sound on the user's PC.  Only the SDL `disk` driver (audio written to a
    // file, SDL_DISKAUDIOFILE) may be chosen from the environment: it makes the real mixer/device path testable without speakers
    // (docs/AUTOPLAY.md, "Real audio without speakers").  Use --fast 1 then: --fast scales the mixer position.
    {
        const char* drv = SDL_getenv("SDL_AUDIODRIVER");
        if (!(drv && std::strcmp(drv, "disk") == 0)) SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
    return true;
}

int ExitCode() { return g.exitCode; }

} // namespace autoplay
