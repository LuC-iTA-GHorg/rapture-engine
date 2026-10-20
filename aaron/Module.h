// Module + static scene registry (replaces LoadLibrary/GetProcAddress, aaron.md sec. 2.4).
//
// The original scenes are DLL exports `CDrawView* CreateScene(Window* host)`; Application::GetModule
// loads "Shoot.DLL" and resolves "CreateScene". In the port everything is linked statically and
// each "DLL" registers its entry points at start-up:
//
//     // Shoot.cpp (static initialiser or called from main()):
//     aaron::RegisterScene("<Game>.DLL", "CreateScene", &game::CreateScene);
//
// Lookup is case-insensitive on the module name (the exe's own scene is registered under the
// name given in APPINIT::sceneDll or via APPINIT::createSceneFn, no registry needed).
#pragma once

#include "aaron/RObject.h"

#include <string>

namespace aaron {

// Registry ----------------------------------------------------------------------------------
void RegisterScene(const char* moduleName, const char* functionName, CreateSceneFn fn);
// nullptr if not registered. functionName may be null/"" -> "CreateScene".
CreateSceneFn FindScene(const char* moduleName, const char* functionName);
// port addition (pkg3): true if any entry point is registered for `moduleName` (distinguishes ERR_SCENE_DLL_NOT_FOUND / ERR_SCENE_FN_NOT_FOUND).
bool IsSceneModuleRegistered(const char* moduleName);

// Module (0x21c bytes): a loaded scene library. Refcounted; cached by Application (App+0x428).
//   +8 dll name, +0x10c function name, +0x210 function, +0x214 HMODULE.
class Module : public RObject {
public:
    Module(const char* dllName, const char* funcName, CreateSceneFn fn);
    // Aaron ord 41 0x1000b0e3: Module(fn): no names, the create function given directly (ordinal / exe-local scenes).
    explicit Module(CreateSceneFn fn);
    // Aaron ord 42 0x1000b067: Module(dll, func, baseDir): names + (if baseDir) the path of the DLL = CombinePath(baseDir, dll).
    // The create function is resolved lazily from the static registry (no LoadLibrary in the port).
    Module(const char* dllName, const char* funcName, const char* baseDir);
    // Aaron ord 43 0x1000afe0: Module(): the module of the running executable (GetModuleHandle(NULL) + its file name); no create function.
    Module();
    // Aaron ord 74 0x1000b141: (FreeLibrary if loaded) and leaves the application's module cache.
    ~Module() override;
    virtual u32 GetClassId() const { return CID_MODULE; } // Aaron ord 383 0x1000b460

    // Aaron ord 417 0x1000b1c4 (Module::Matches): same dll + function name (case-insensitive)?
    bool Matches(const char* dllName, const char* funcName) const;
    // Aaron ord 416 0x1000b213 (Ghidra: "CMFCToolBar::IsDragButton", a false library match): is `fn` this module's create function?
    bool HasCreateFn(CreateSceneFn fn) const { return fn_ != nullptr && fn_ == fn; }
    // Module::GetCreateFn 0x1000b233 (ord 426): in the original LoadLibraryA + GetProcAddress on first use; port: registry lookup by name
    // (and a "loaded" flag so that Unload() can drop it again).
    CreateSceneFn GetCreateFn() const;
    // Aaron ord 702 0x1000b3c9: Release(); if only the application's cache reference is left (count 1) and the library is loaded: post
    // FreeLibrary (0xc00) and forget the function / handle. Port: the function pointer is dropped (re-resolved by GetCreateFn).
    void Unload();
    bool IsLoaded() const { return loaded_; }
    // Aaron ord 439 0x1000b30b: CombinePath(DLL path, name) - `name` relative to the folder of this module's file.
    std::string BuildPath(const char* name) const;
    const std::string& FilePath() const { return path_; }
    // Aaron ord 164 0x1000c170 Module::CreateScene: calls fn(host), reporting errors (ERR_SCENE_*) and retrying when
    // Application::ReportError asks to; then CDrawView::Init(640x480x16) on the scene, scene->module = this,
    // scene->arg = arg. Returns nullptr on failure.
    Scene* CreateScene(Window* host, uintptr_t arg = 0);
    const std::string& DllName() const { return dll_; }
    const std::string& FuncName() const { return func_; }

private:
    std::string dll_, func_;
    std::string path_; // +8 of the original: the file name / path of the module
    mutable CreateSceneFn fn_ = nullptr;
    mutable bool loaded_ = false; // +0x218
};

} // namespace aaron
