// G2 - HostApp (Shoot.exe 0x401000-0x401d00).  See HostApp.h.
#include "host/HostApp.h"

#include "aaron/Module.h"
#include "aaron/Scene.h"
#include "host/HostConfig.h"
#include "host/Popups.h"
#include "host/TitleScene.h"

#include <cstdio>
#include <cstring>

namespace rapture {

namespace {

HostApp* g_app = nullptr; // DAT_00418e10

} // namespace

HostApp* TheHostApp() { return g_app; }

// The RT_STRING table of the host exe (Config().strings), ids 0x5007..0x501d.
const char* HostApp::ResourceString(u32 resId) {
    const HostConfig& c = Config();
    for (int i = 0; i < c.stringCount; ++i)
        if (c.strings[i].id == resId) return c.strings[i].text;
    return nullptr;
}
int HostApp::ResourceStringCount() { return Config().stringCount; }

// 0x401000
HostApp::HostApp(const aaron::APPINIT* init) : Application(init) {}
// 0x401043
HostApp::~HostApp() {}

// WinMain 0x401b6f (the title and registry key part).  The original loads the strings with LoadStringA from its own
// resources into DAT_00418044 (title, 0x80 bytes) and DAT_004181c8 (key, 0xff): <0x5017> + "\" + <game name id> + "\" + <0x501d>.
// The two game-specific ids (and the table) come from HostConfig.
void HostApp::BuildAppInit(aaron::APPINIT& out) {
    const HostConfig& c = Config();
    out = aaron::APPINIT();
    const char* title = ResourceString(c.titleStrId);
    std::snprintf(out.name, sizeof out.name, "%s", title ? title : c.defaultTitle);
    const char* vendor = ResourceString(kStrVendorKey);
    const char* game = ResourceString(c.registryNameStrId);
    const char* locale = ResourceString(kStrLocale);
    std::snprintf(out.regKey, sizeof out.regKey, "%s\\%s\\%s", vendor ? vendor : "Software\\Disney Interactive",
                  game ? game : c.registryNameFallback, locale ? locale : "US");
    std::snprintf(out.sceneDll, sizeof out.sceneDll, "%s", c.hostBinary);
    out.createSceneFn = &CreateTitleScene; // CreateScene 0x40958d
}

// 0x4010ab "GetString": resource id = id + 0x5006.  QUIRK kept: 0x5010 is a printf format fed with the application title.
int HostApp::LoadString(u32 id, char* buf, int len) {
    if (!buf || len <= 0) return 0;
    u32 resId = id + 0x5006;
    const char* s = ResourceString(resId);
    if (!s) return Application::LoadString(id, buf, len); // 0x4010ab -> Application::LoadString(id, buf, len)
    if (resId == 0x5010) {
        char tmp[256];
        std::snprintf(tmp, sizeof tmp, s, init_.name);   // wsprintfA(tmp, fmt, this + 0x1c)
        std::snprintf(buf, size_t(len), "%s", tmp);
    } else {
        std::snprintf(buf, size_t(len), "%s", s);
    }
    return int(std::strlen(buf));
}

// 0x401a3c
void HostApp::OnDestroy() {
    Audio().SaveVolumes(GetMachineKey());
    Application::OnDestroy();
}

// 0x401a71
void HostApp::LoadScene(aaron::Module* module) {
    Audio().LoadVolumes(GetMachineKey());
    Application::LoadScene(module);
}

// 0x401069
void HostApp::SetKeySequence(int seqId, const char* keys) {
    if (!keys) return;
    if (seqId == 0x10) std::snprintf(keySeqA_, sizeof keySeqA_, "%s", keys);
    else if (seqId == 0x20) std::snprintf(keySeqB_, sizeof keySeqB_, "%s", keys);
}

// 0x401aac: sink->vtbl[7](seqId, char, first) for each char of both strings
bool HostApp::FeedKeySequences(IKeySequenceSink* sink) {
    if (!sink) return false;
    for (int i = 0; keySeqA_[i]; ++i) sink->AddKeySequenceKey(0x10, keySeqA_[i], i == 0);
    for (int i = 0; keySeqB_[i]; ++i) sink->AddKeySequenceKey(0x20, keySeqB_[i], i == 0);
    return true;
}

aaron::Widget* HostApp::DefaultParent() const {
    aaron::Scene* s = const_cast<HostApp*>(this)->GetScene(); // *(app + 0x10) + 0x53c = scene->RootWidget()
    if (!s) s = fallbackScene_;
    return s ? s->RootWidget() : nullptr;
}

// 0x40114c.  Needs the common archive (Config().commonArchive, slot kSlotCommon) for the background 0x64820002; the text is in the common
// strings archive (opened by OnInit).
aaron::Widget* HostApp::CreateCredits(uintptr_t modal, aaron::Widget* parent) {
    aaron::Widget* result = nullptr;
    aaron::Widget* p = parent ? parent : DefaultParent();
    aaron::Scene* sc = p ? p->GetScene() : nullptr;
    if (sc && sc->OpenArchive(kSlotCommon, Config().commonArchive, nullptr))
        result = aaron::Widget::InitOrDelete(new CCreditsScreen(aaron::WidgetParent(p), modal));
    if (!result && modal != 0) Quit();
    return result;
}

// 0x40127a.  Dialog(parent, dib, 0, 0, 0x26fc, type, 0x20000, arg, 3) + InitOrDelete for every type.
void* HostApp::CreateObject(void* parentPtr, int type, uintptr_t arg) {
    aaron::Widget* parent = static_cast<aaron::Widget*>(parentPtr);
    if (!parent) parent = DefaultParent();
    if (!parent) return nullptr;
    aaron::WidgetParent wp(parent);
    CPopup* p = nullptr;
    switch (type) {
    case POPUP_OPTIONS: p = new COptionsMenu(wp, arg); break;
    case POPUP_NEWGAME: p = new CNewGameDlg(wp, POPUP_NEWGAME, 0x20000, arg); break;
    case POPUP_NEWGAME_END: p = new CNewGameDlg2(wp, arg); break;
    case POPUP_SAVE: p = new CSaveDlg(wp, arg); break;
    case POPUP_RESTORE: p = new CRestoreDlg(wp, arg); break;
    case POPUP_AUDIO: p = new CAudioDlg(wp, arg); break;
    case POPUP_NAME_P1:
    case POPUP_NAME_P2: p = new CNameEntryDlg(wp, type, arg); break;
    case POPUP_HIGHSCORES: p = new CHighScoreDlg(wp, arg); break;
    case POPUP_QUIT: p = new CQuitConfirmDlg(wp, POPUP_QUIT, 0x20000, arg); break;
    case POPUP_PLAYAGAIN: p = new CPlayAgainDlg(wp, arg); break;
    case POPUP_CREDITS: return CreateCredits(arg, parent);
    case POPUP_NAME2_P1:
    case POPUP_NAME2_P2: p = new CNameEntryDlg2(wp, type, arg); break;
    default: return nullptr;
    }
    return aaron::Widget::InitOrDelete(p);
}

// WinMain 0x401b6f
int RunHost(const char* cmdline) {
    aaron::APPINIT init;
    HostApp::BuildAppInit(init);
    aaron::RegisterScene(init.sceneDll, "CreateScene", &CreateTitleScene);
    const HostConfig& cfg = Config();
    if (cfg.createGameScene && cfg.sceneDll[0]) aaron::RegisterScene(cfg.sceneDll, cfg.sceneEntry, cfg.createGameScene);   // what GetModule(sceneDll, sceneEntry) finds
    HostApp* app = new HostApp(&init);
    g_app = app;
    SetPopupFactory(app);
    int rc = app->Run(cmdline ? cmdline : "");
    SetPopupFactory(nullptr);
    g_app = nullptr;
    delete app;
    return rc;
}

} // namespace rapture
