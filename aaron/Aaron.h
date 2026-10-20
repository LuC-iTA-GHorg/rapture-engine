// Umbrella header of the aaron layer (application framework). Includes the moses layer too.
#pragma once

#include "moses/Moses.h"

#include "aaron/Application.h"
#include "aaron/Containers.h"
#include "aaron/ControlWin.h"
#include "aaron/Cursors.h"
#include "aaron/Joystick.h"
#include "aaron/Path.h"
#include "aaron/Input.h"
#include "aaron/Managers.h"
#include "aaron/Module.h"
#include "aaron/Ordinals.h"
#include "aaron/RObject.h"
#include "aaron/RegKey.h"
#include "aaron/ResArchive.h"
#include "aaron/Scene.h"
#include "aaron/Sprites.h"
#include "aaron/Types.h"
#include "aaron/Widget.h"
#include "aaron/Window.h"

namespace aaron {
// Aaron ord 1 0x1001de04: the DLL exports a data item, the copyright string embedded at the start of the image
// ("Copyright (c) 1998-1999 Rapture Technologies, Inc."). It is the only export that is not a function.
inline constexpr const char kCopyright[] = "Copyright (c) 1998-1999 Rapture Technologies, Inc.";
} // namespace aaron
