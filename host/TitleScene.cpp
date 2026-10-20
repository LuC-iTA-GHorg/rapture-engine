// G2 - CTitleScene.  See TitleScene.h.  Shoot.exe addresses in comments.
#include "host/TitleScene.h"

#include "aaron/Application.h"
#include "host/HostApp.h"
#include "host/HostConfig.h"

#include <cstdio>

namespace rapture {

bool CTitleScene::g_firstVisit = true; // DAT_00418574
int CTitleScene::g_introStep = 0;      // DAT_00418e14

CTitleScene::CTitleScene(aaron::Window* host) : Scene(host, "Title Screen") {
    keys_.owner = this;
    sounds_.owner = this;
}

CTitleScene::~CTitleScene() {}

aaron::Scene* CreateTitleScene(aaron::Window* host) { return new CTitleScene(host); }

// 0x40932c.  The common archive (slot 100: the AVIs and the credits background) and the common strings archive (slot 101: strings, title
// picture, credits text) are opened on the first visit only; on the second one the credits open what they need.
bool CTitleScene::OpenResources() {
    if (g_firstVisit) return OpenArchive(kSlotCommon, Config().commonArchive, nullptr) && OpenArchive(kSlotCommonStrings, Config().commonStringsArchive, nullptr);
    return true;
}

// 0x408f91: SPACE / ESC (key up) or a mouse button release skips the current step
void CTitleScene::KeyListener::OnInput(u32 msg, uintptr_t wParam) {
    if (msg == aaron::WMSG_KEYUP) {
        if (wParam != aaron::AVK_SPACE && wParam != aaron::AVK_ESCAPE) return;
    } else if (msg != aaron::WMSG_LBUTTONUP && msg != aaron::WMSG_RBUTTONUP) {
        return;
    }
    // QUIRK: skipping the first logo sets the step to 1 by hand; the sound-end notification then advances to 2, so ONE skip
    // also skips DDLogo.avi.
    if (g_introStep == 0) {
        g_introStep = 1;
        owner->FreeResource(0x64850000);   // stops the AVI -> SoundListener::OnEnd -> AdvanceIntro(1)
    } else if (g_introStep == 1) {
        owner->FreeResource(0x64850001);
    } else {
        owner->AdvanceIntro(1);
    }
}

// 0x409031
void CTitleScene::SoundListener::OnEnd() { owner->AdvanceIntro(1); }

// 0x40904e
int CTitleScene::OnCommand(u32 id, uintptr_t) {
    if (id == 0x1401) { // second visit: music + credits
        OpenArchive(Config().fxSlot, Config().fxArchive, nullptr);
        PlaySound(0x1850000, 1, 0, 100, 0, 0, 0);
        if (HostApp* app = TheHostApp()) app->CreateCredits(1, nullptr);
        else if (aaron::Application* a = aaron::Application::GetApp()) a->Quit();
        return 1;
    }
    if (id == 0x2400) { // load the game scene
        aaron::Application* app = aaron::Application::GetApp();
        aaron::Module* m = app ? app->GetModule(Config().sceneDll, Config().sceneEntry, nullptr) : nullptr;
        if (m) {
            if (Host()) Host()->PostLoadScene(m, 0);     // Window::LoadScene (Aaron ord 697)
        } else if (app) {
            // PORT: the game scene is registered by RunHost (HostConfig::createGameScene).  When it is
            // not there, GetModule has already reported ERR_SCENE_DLL_NOT_FOUND on stderr: end the application cleanly.
            std::fprintf(stderr, "Title Screen: scene %s/%s is not registered - quitting\n", Config().sceneDll, Config().sceneEntry);
            app->Quit();
        }
        return 1;
    }
    return 0;
}

// 0x4090ea
bool CTitleScene::OnStart() {
    ChangeColorDepth(0x10);
    SetSurfaceColor(0);
    Cursor().ShowCursor(0);
    if (!g_firstVisit) {
        aaron::RECT box = {-320, -240, 320, 240};
        SetClipBox(&box);
        PostMessage(aaron::WMSG_COMMAND, 0x1401, 0);
    } else {
        g_firstVisit = false;
        char fmt[256];
        // wsprintfA(out, fmt) with no arguments = copy
        if (HostApp* app = TheHostApp()) {
            LoadString(0x658c0006, fmt, 0xff);
            app->SetKeySequence(0x10, fmt);
            LoadString(0x658c0007, fmt, 0xff);
            app->SetKeySequence(0x20, fmt);
        }
        Focus().AddKeyListener(&keys_);
        Focus().AddSoundListener(&sounds_);
        g_introStep = 0;
        AdvanceIntro(0);
    }
    return true;
}

// 0x409302: the QM_NOTIFY 0x1400 at the end of the title animation
void CTitleScene::OnObjectEvent2(aaron::Widget*, intptr_t a, intptr_t, intptr_t) {
    if (a == 0x1400) AdvanceIntro(1);
}

// 0x409390
void CTitleScene::AdvanceIntro(int delta) {
    g_introStep += delta;
    switch (g_introStep) {
    case 0:
        if (!PlaySoundSimple(0x64850000, 1, &sounds_)) AdvanceIntro(1);   // DILogo.avi
        break;
    case 1:
        if (!PlaySoundSimple(0x64850001, 1, &sounds_)) AdvanceIntro(1);   // DDLogo.avi
        break;
    case 2: {
        // table 0x4184e8: show the title for 2 s, play frames 1..3 (83 ms each), hide, notify 0x1400
        static moses::QUEUEMSG msgs[4];
        static moses::QUEUEMSG* list[4];
        msgs[0] = moses::qm::Anim(2000, 0, 0, 0);
        msgs[1] = moses::qm::Anim(83, 0, 1, 3);
        msgs[2] = moses::qm::Show(0);
        msgs[3] = moses::qm::Notify(0x1400);
        for (int i = 0; i < 4; ++i) list[i] = &msgs[i];
        aaron::WidgetParent root(RootWidget());
        aaron::Widget* w = aaron::Widget::InitOrDelete(new aaron::AnimSprite(root, Config().titleSpriteId, 0, 0, 0, 0, 0, list, 4));
        if (!w) AdvanceIntro(1);
        break;
    }
    case 3:
        PostMessage(aaron::WMSG_COMMAND, 0x2400, 0);
        Focus().RemoveKeyListener(&keys_);
        Focus().RemoveSoundListener(&sounds_);
        break;
    default:
        break;
    }
}

} // namespace rapture
