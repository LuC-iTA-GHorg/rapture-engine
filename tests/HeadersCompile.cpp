// Compile-time check that every engine header is self-contained and consistent.
// Contains no code: nothing here may call anything (the implementations do not exist yet).
// Each header is included on its own, FIRST in its own scope order, so a missing include in one
// of them shows up here. Keep this file in sync when adding headers.

#include <type_traits>

// ---- moses (each header alone) ----
#include "moses/Types.h"
#include "moses/QueueMsg.h"
#include "moses/Mem.h"
#include "moses/DibData.h"
#include "moses/DrawObj.h"
#include "moses/DrawGdi.h"
#include "moses/DrawView.h"
#include "moses/AnimView.h"
#include "moses/AnimObjs.h"
#include "moses/AudioLib.h"
#include "moses/ResMgr.h"
#include "moses/Moses.h"

// ---- tests ----
#include "tests/EngineTests.h"

// ---- aaron ----
#include "aaron/Types.h"
#include "aaron/RObject.h"
#include "aaron/RegKey.h"
#include "aaron/Module.h"
#include "aaron/Managers.h"
#include "aaron/Input.h"
#include "aaron/Widget.h"
#include "aaron/Sprites.h"
#include "aaron/ResArchive.h"
#include "aaron/Scene.h"
#include "aaron/Window.h"
#include "aaron/Application.h"
#include "aaron/Ordinals.h"
#include "aaron/Aaron.h"

// Layout sanity checks that do not need any implementation.
namespace {
using namespace moses;
static_assert(sizeof(RECT) == 16 && sizeof(POINT) == 8, "RECT/POINT must keep the Win32 layout");
static_assert(QM_ANIM == 100 && QM_LINETO == 8 && QM_SETSPEED == 0xf, "QUEUEMSG opcodes changed");
static_assert(DROP_TRANSPARENT == 0 && DROP_OPAQUE == 1 && DROP_ON_DEST == 2, "DROP values changed");
static_assert(aaron::WMSG_COMMAND == 0x111 && aaron::AMSG_TICK == 0xc05 && aaron::AMSG_NOTIFY == 0xc06, "message ids changed");
static_assert(sizeof(aaron::kOrdinals) / sizeof(aaron::kOrdinals[0]) == 150, "150 ordinals used by Shoot.*");
// A scene must be usable as a CAnimView / CDrawView / RObject.
static_assert(std::is_base_of<moses::CAnimView, aaron::Scene>::value, "Scene : CAnimView");
static_assert(std::is_base_of<moses::CAnimObj, aaron::AnimSprite>::value, "AnimSprite embeds CAnimObj");
static_assert(std::is_base_of<moses::CDrawObj, aaron::Button>::value, "Button embeds CDrawDib");
static_assert(std::is_base_of<aaron::Window, aaron::Application>::value, "Application : Window");
} // namespace
