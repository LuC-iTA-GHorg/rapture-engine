// Module + static scene registry (replaces LoadLibrary/GetProcAddress). Package 3.
// Aaron Module::CreateScene ord 164 0x1000c170, Module::GetCreateFn 0x1000b233, Module::Matches ord 416.
#include "aaron/Module.h"

#include "aaron/Application.h"
#include "aaron/Path.h"
#include "aaron/Scene.h"

#include <cctype>
#include <map>

namespace aaron {

namespace {

std::string lower(const char* s) {
    std::string r = s ? s : "";
    for (char& c : r) c = char(std::tolower((unsigned char)c));
    return r;
}

std::string norm_func(const char* f) { return (f && *f) ? lower(f) : std::string("createscene"); }

std::map<std::string, CreateSceneFn>& registry() {
    static std::map<std::string, CreateSceneFn> r;
    return r;
}

std::string key(const char* mod, const char* fn) { return lower(mod) + "|" + norm_func(fn); }

} // namespace

void RegisterScene(const char* moduleName, const char* functionName, CreateSceneFn fn) {
    registry()[key(moduleName, functionName)] = fn;
}

CreateSceneFn FindScene(const char* moduleName, const char* functionName) {
    auto& r = registry();
    auto it = r.find(key(moduleName, functionName));
    return it == r.end() ? nullptr : it->second;
}

bool IsSceneModuleRegistered(const char* moduleName) {
    std::string prefix = lower(moduleName) + "|";
    for (auto& kv : registry())
        if (kv.first.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

// ---------------------------------------------------------------------------------------------
Module::Module(const char* dllName, const char* funcName, CreateSceneFn fn)
    : dll_(dllName ? dllName : ""), func_((funcName && *funcName) ? funcName : "CreateScene"), path_(dllName ? dllName : ""), fn_(fn) {
    // a registry scene counts as a "loaded library" (Unload() can drop it); a function that is not the registered one (the exe's own) never does
    loaded_ = fn_ && !dll_.empty() && FindScene(dll_.c_str(), func_.c_str()) == fn_;
}

Module::Module(CreateSceneFn fn) : fn_(fn) {}                                // ord 41 0x1000b0e3

Module::Module(const char* dllName, const char* funcName, const char* baseDir) // ord 42 0x1000b067
    : dll_(dllName ? dllName : ""), func_((funcName && *funcName) ? funcName : "CreateScene"), path_(dllName ? dllName : "") {
    if (baseDir && dllName) path_ = CombinePath(baseDir, dllName);
}

Module::Module() : path_(ExecutablePath()) {}                                // ord 43 0x1000afe0

Module::~Module() { // ord 74 0x1000b141
    Application* app = Application::GetApp();
    if (app) app->ForgetModule(this);
}

bool Module::Matches(const char* dllName, const char* funcName) const { // ord 417 0x1000b1c4
    return lower(dll_.c_str()) == lower(dllName) && norm_func(func_.c_str()) == norm_func(funcName);
}

CreateSceneFn Module::GetCreateFn() const { // 0x1000b233
    if (!fn_ && !dll_.empty()) {
        fn_ = FindScene(dll_.c_str(), func_.c_str()); // LoadLibraryA + GetProcAddress
        loaded_ = fn_ != nullptr;
    }
    return fn_;
}

void Module::Unload() { // ord 702 0x1000b3c9
    if (Release() == 1 && loaded_) { // (Release() == 0: the module is gone)
        fn_ = nullptr;
        loaded_ = false;
    }
}

std::string Module::BuildPath(const char* name) const { return CombinePath(path_, name ? name : ""); } // ord 439 0x1000b30b

// Aaron 0x1000c170. Errors are reported through Application::ReportError (0x80000003 dll missing, 0x80000005 function
// missing, 0x80000006 creation failed); the application asks for a retry by returning nonzero (original: IDRETRY = 4).
Scene* Module::CreateScene(Window* host, uintptr_t arg) {
    Application* app = Application::GetApp();
    for (;;) {
        CreateSceneFn fn = GetCreateFn();
        if (!fn) {
            int retry = app ? app->ReportError(ERR_SCENE_FN_NOT_FOUND, nullptr, this, 1) : 0;
            if (retry) continue;
            return nullptr;
        }
        Scene* s = fn(host);
        if (s) {
            POINT sz;
            if (host) host->GetClientSize(&sz);
            else { sz.x = 640; sz.y = 480; }
            u32 bpp = app ? u32(app->ReqBpp()) : 16u;
            if (s->Init(nullptr, nullptr, u32(sz.x), u32(sz.y), bpp)) { // CDrawView::Init
                s->module_ = this;                                        // +0x524
                s->arg_ = arg;                                            // +0x530
                return s;
            }
        }
        delete s;
        int retry = app ? app->ReportError(ERR_SCENE_CREATE_FAILED, nullptr, this, 1) : 0;
        if (!retry) return nullptr;
    }
}

} // namespace aaron
