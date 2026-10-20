// Aaron.dll port - shared constants, ids and small structs (aaron.md).
//
// Everything of this layer lives in `namespace aaron`. NOTE: `aaron::Window` has the same name as
// the platform class `::Window` (source/platform/Window.h). Game code must always write
// `aaron::Window` (do not `using namespace aaron` at global scope); the platform class is only
// ever reached through `aaron::Window::Platform()`.
#pragma once

#include "moses/Types.h"

#include <cstddef>
#include <cstdint>

namespace aaron {

using moses::COLORREF;
using moses::i16;
using moses::i32;
using moses::POINT;
using moses::RECT;
using moses::u16;
using moses::u32;
using moses::u8;

class Application;
class Window;
class Scene;
class Module;
class Widget;
class RegKey;

// ---------------------------------------------------------------------------------------------
// Resource ids (aaron.md sec. 2.5): id = slot(8) << 24 | class(8) << 16 | index(16).
//   slot  = archive opened with Scene::OpenArchive(slot, ...)   (Shoot.exe/Shoot.dll use 0 = main archive, 1 = FX archive, 3 = lines archive,
//           100 / 101 = the two common archives shared by all the games; see HostConfig)
//   class = 0x80|rrs type (0x82 dib, 0x85 sound, 0x86 sound group, 0x8b group2 of dibs, 0x8c string, 0x83 raw);
//           bit 0x800000 set = "direct id", clear = "group + index" reference resolved with GetIndexedResId.
constexpr u32 MakeResId(u32 slot, u32 cls, u32 index) { return (slot << 24) | (cls << 16) | index; }
constexpr u32 ResSlot(u32 id) { return id >> 24; }
constexpr u32 ResClass(u32 id) { return (id >> 16) & 0xff; }
constexpr u32 ResIndex(u32 id) { return id & 0xffff; }
constexpr u32 kResDirect = 0x800000;
constexpr u32 kNoRes = 0xffffffffu; // Widget "no resource" (-1)

// ---------------------------------------------------------------------------------------------
// Class ids returned by the virtual GetClassId() (aaron.md sec. 1). The original has no RTTI.
enum ClassId : u32 {
    // Base classes below 0x20080 (WP-4; ordinals 366/381/382/389/390 return them from vtable slot 1 of the original):
    CID_ROBJECT = 0x20000,    // RObject / PtrArray               Aaron ord 366 0x100010c0
    CID_LISTITEM = 0x20001,   // LinkItem (doubly linked item)    Aaron ord 382 0x10001e40
    CID_LIST = 0x20002,       // LinkList                         Aaron ord 381 0x10001e30
    CID_TREEBASE = 0x20003,   // TreeNode (vtable 0x1001d4cc)     Aaron ord 390 0x10002d80
    CID_TREEROOT = 0x20004,   // TreeRoot (holder of the root)    Aaron ord 389 0x10002d70
    // vtable slot 1 (GetClassId) of each class: Application 0x100118e0 (ord 388), Window 0x1000f2b0 (387), Scene 0x1000e350 (386),
    // Module 0x1000b460 (383), ResArchive 0x1000c160 (385)
    CID_APPLICATION = 0x20080, CID_WINDOW = 0x20081, CID_SCENE = 0x20082, CID_MODULE = 0x20083,
    CID_RESARCHIVE = 0x20084,
    CID_NODE = 0x20100,       // Node / the timer-subscribing root node       Aaron ord 380 0x10004c10
    CID_CONTAINER1 = 0x20101, // the ResNode base (vtable 0x1001d6d8)         Aaron ord 384 0x10004c20
    CID_CONTAINER2 = 0x20102, // the second container class (vtable 0x1001d718) Aaron ord 391 0x10004c30
    CID_SPRITE = 0x20103,     // DibSprite / AnimSprite (+ Poly sprites ...)  Aaron ord 370,371,373,374,375,377 0x100069c0
    CID_CONTROLWIN = 0x20104, // ControlWin / EditControl                     Aaron ord 379 0x10007a60
    CID_TIMER = 0x20105,      //                                               Aaron ord 378 0x10006f40
    CID_GDISPRITE = 0x20106,  // Rect/Poly/Text sprites                       Aaron ord 372,376 0x10006a00
    CID_BUTTON = 0x20107,     //                                               Aaron ord 368 0x10003570
    CID_CURSOR = 0x20108,     // Cursor and subclasses                        Aaron ord 367 0x10008be0
    CID_DIALOG = 0x20109,     //                                               Aaron ord 369 0x10003d00
};

// ---------------------------------------------------------------------------------------------
// Windows-style message ids. Named WMSG_* (not WM_*) so a .cpp that includes <windows.h> still compiles.
// Platform events are translated to these by aaron::Window (see README "input").
enum WMSG : u32 {
    WMSG_CLOSE = 0x0010,
    WMSG_ACTIVATEAPP = 0x001c,
    WMSG_KEYDOWN = 0x0100,
    WMSG_KEYUP = 0x0101,
    WMSG_CHAR = 0x0102,
    WMSG_COMMAND = 0x0111, // Scene::PostMessage(0x111, cmd, param) = "scene command" -> Scene::OnCommand
    WMSG_SYSCOMMAND = 0x0112,
    WMSG_MOUSEMOVE = 0x0200,
    WMSG_LBUTTONDOWN = 0x0201,
    WMSG_LBUTTONUP = 0x0202,
    WMSG_LBUTTONDBLCLK = 0x0203,
    WMSG_RBUTTONDOWN = 0x0204,
    WMSG_RBUTTONUP = 0x0205,
    WMSG_RBUTTONDBLCLK = 0x0206,
    WMSG_MBUTTONDOWN = 0x0207,
    WMSG_MBUTTONUP = 0x0208,
    // Aaron private messages (aaron.md sec. 2.3)
    AMSG_FREELIBRARY = 0x0c00, // wParam = Module*  (port: releases the Module)
    AMSG_RESFREED = 0x0c01,    // resource / sound released
    AMSG_LOADSCENE = 0x0c02,   // posted: LoadScene(Module*, arg) -> scene change
    AMSG_PAUSE = 0x0c04,       // Broadcaster, wParam 0/1 (Pause key)
    AMSG_TICK = 0x0c05,        // TimerMgr tick
    AMSG_NOTIFY = 0x0c06,      // deferred widget notification -> widget->OnNotify(id, widget)
    AMSG_ABOUT = 0x0c07,       // Ctrl+Shift+Alt+V (About box) - not ported
    AMSG_SOUNDSTART = 0x0c08,  // CAudioLib::SetSoundProc callbacks
    AMSG_SOUNDEND = 0x0c09,
    AMSG_SCENE_BASE = 0x1400,  // >= 0x1400: handled by the scene (OnMessage)
};

// Windows virtual-key codes used by the game (names AVK_* to avoid <windows.h> macros).
enum AVK : u32 {
    AVK_BACK = 0x08, AVK_TAB = 0x09, AVK_RETURN = 0x0d, AVK_SHIFT = 0x10, AVK_CONTROL = 0x11, AVK_MENU = 0x12 /*Alt*/,
    AVK_PAUSE = 0x13, AVK_ESCAPE = 0x1b, AVK_SPACE = 0x20,
    AVK_LEFT = 0x25, AVK_UP = 0x26, AVK_RIGHT = 0x27, AVK_DOWN = 0x28,
    AVK_MULTIPLY = 0x6a, AVK_ADD = 0x6b, AVK_SUBTRACT = 0x6d,
    AVK_F1 = 0x70, AVK_F2 = 0x71, AVK_F3 = 0x72, AVK_F4 = 0x73, AVK_F5 = 0x74,
};
// Modifier bits for accelerators / WMSG_KEY*: 1 = Shift, 2 = Ctrl, 4 = Alt.
enum AMOD : u32 { AMOD_NONE = 0, AMOD_SHIFT = 1, AMOD_CTRL = 2, AMOD_ALT = 4 };

// ---------------------------------------------------------------------------------------------
// Widget style bits (aaron.md sec. 2.7). Bits whose meaning is not confirmed are marked TODO.
enum WidgetStyle : u32 {
    WS_A_HIDDEN = 0x00000001,       // created hidden
    WS_A_NOTIFY_PARENT = 0x00000004, // TODO exact meaning: 4 and 8 both route OnNotify to the parent
    WS_A_NOTIFY = 0x00000008,       // OnNotify -> parent->OnNotify, else scene->OnCommand
    WS_A_PICK_LEFT = 0x00000010,    // 0x10/0x20/0x40: pick with left / right / ... button
    WS_A_PICK_RIGHT = 0x00000020,
    WS_A_PICK_MID = 0x00000040,
    WS_A_CONTAINER = 0x00000080,
    WS_A_TABSTOP = 0x00000200,
    WS_A_CUSTOMCURSOR = 0x00000400,
    WS_A_SCROLLABLE = 0x00000800,
    WS_A_ALIGN_H1 = 0x00010000,     // text alignment
    WS_A_ALIGN_H2 = 0x00020000,     // Dialog creation flag used everywhere (0x20000)
    // 0x40000 .. 0x1000000 / 0x400000 / 0x600000: modal-dialog policies (TODO, see Dialog)
};

// ---------------------------------------------------------------------------------------------
// Input codes delivered to Scene::OnInput(code, x, y, user) by a Controller (aaron.md sec. 2.8).
enum InputCode : u32 {
    IN_DIR_MASK = 0x0000000f,   // bits 0-3 direction. Verified (KeyboardSource 0x1000a132): Up=1, Down=2, Right=4, Left=8 (diagonals = OR)
    IN_BTN1 = 0x00000010,       // Ctrl  -> button 0x10 (left mouse button = 0x10 as well)
    IN_BTN2 = 0x00000020,       // Alt   -> 0x20 (right mouse button)
    IN_BTN3 = 0x00000040,       // Space -> 0x40
    IN_BTN4 = 0x00000080,       // Shift -> 0x80
    IN_BTN_MASK = 0x00000ff0,   // buttons 0x10..0x800
    IN_RELEASE = 0x10000000,    // combined with a button bit: release
    IN_ABSOLUTE = 0x40000000,   // x,y are an absolute position (mouse move)
    IN_ONOFF = 0x80000000,      // on/off notification (Shoot: show/hide the aim cursor)
};

// Source types for Controller::EnableSource(type, bool)
// Values verified in Aaron.dll: InputSource vtable slot 2 returns 1 (KeyboardSource 0x1000a2ff vtbl), 2 (MouseSource), 3 (JoystickSource).
// Shoot calls Controller::EnableSource(type, 3) with the player's chosen device type.
enum InputSourceType : int { SRC_KEYBOARD = 1, SRC_MOUSE = 2, SRC_JOYSTICK = 3 };

// ---------------------------------------------------------------------------------------------
// Application::ReportError codes (aaron.md sec. 2.1)
enum AaronError : u32 {
    ERR_UNKNOWN = 0x80000001,
    ERR_SCENE_DLL_NOT_FOUND = 0x80000003,
    ERR_SCENE_FN_NOT_FOUND = 0x80000005,
    ERR_SCENE_CREATE_FAILED = 0x80000006,
    ERR_VIDEO_MODE = 0x8000000a, // "%d x %d" unsupported video mode
    // 0x80000002, 0x80000004, 0x80000007..9 : TODO
};

// ---------------------------------------------------------------------------------------------
// APPINIT: passed to Application::Application (ord 52). Original: 0x39c bytes, DAT_00418040 in Shoot.exe.
// Port: only the meaningful fields.
using CreateSceneFn = Scene* (*)(Window* host); // `extern "C" CDrawView* CreateScene(Window* host)`, cdecl

struct APPINIT {
    u32 size = 0x39c;                    // +0
    char name[128] = {0};                // +4    window title (the game title)
    char sceneDll[256] = {0};            // +0x84 scene module name (empty = use createSceneFn)
    CreateSceneFn createSceneFn = nullptr; // +0x184 (Shoot.exe: &CreateScene of the exe, 0x40958d)
    char regKey[256] = {0};              // +0x188 registry key path: vendor, game name, locale (see HostApp::BuildAppInit)
    u32 extra38c = 0;                    // +0x38c (WP-4): the third argument of the two Application constructors (ord 53 / 54); no known reader
};

// Window::Create parameter block (Aaron ord 166 0x1000e829 / ord 165): {height, width, y, x, style, exStyle, title, showCmd} - this is
// the order of the ints the original reads (param_2[0..7]); Win32 style bits are only kept for documentation in the port.
struct WINCREATE {
    int height = 0, width = 0, y = 0, x = 0;
    u32 style = 0, exStyle = 0;
    const char* title = nullptr;
    int showCmd = 0; // SW_*: 0 = do not show
};

// WINDOWPLACEMENT (44 bytes) as stored by Window::SavePlacement under the value name "Placement".
struct WINPLACEMENT {
    u32 length = 0x2c, flags = 0, showCmd = 1; // SW_SHOWNORMAL
    i32 minX = 0, minY = 0, maxX = 0, maxY = 0;
    i32 left = 0, top = 0, right = 0, bottom = 0; // rcNormalPosition
};
static_assert(sizeof(WINPLACEMENT) == 0x2c, "WINDOWPLACEMENT is 44 bytes");

// WININFO for Application::CreateMainWindow(hParent, WININFO*)
struct WININFO {
    int width = 640, height = 480, bpp = 16;
    bool fullscreen = false;
    const char* title = nullptr;
};

} // namespace aaron
