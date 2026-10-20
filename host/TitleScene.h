// G2 - the "Title Screen" scene of Shoot.exe (CreateScene 0x40958d; class CTitleScene, vtable 0x415dec, 0x6d0 bytes).
//
// First visit (the global DAT_00418574 == 1): the two logo AVIs, the title picture (HostConfig::titleSpriteId) and then the game scene
// (HostConfig::sceneDll / sceneEntry) is loaded (posted command 0x2400).
// Second visit (coming back from the game): the FX archive MIDI + the credits (modal), which quit the application when they end.
#pragma once

#include "aaron/Scene.h"
#include "aaron/Sprites.h"

namespace rapture {

using aaron::u32;

class CTitleScene : public aaron::Scene {
public:
    // 0x408ed0.  Scene name "Title Screen" (0x4185b8): also the last component of the registry key of this scene.
    explicit CTitleScene(aaron::Window* host);
    ~CTitleScene() override;                                    // 0x409610

    bool OpenResources() override;                              // slot 5  "LoadResources" 0x40932c
    bool OnStart() override;                                    // slot 7  "OnInit" 0x4090ea
    int OnCommand(u32 id, uintptr_t param) override;            // slot 10 0x40904e
    void OnObjectEvent2(aaron::Widget* w, intptr_t a, intptr_t b, intptr_t c) override; // slot 15 "OnNotify" 0x409302
    void AdvanceIntro(int delta);                               // 0x409390

    // The two embedded listeners of the original (+0x6b0 KeyListener, +0x6c8 SoundListener)
    struct KeyListener : aaron::SceneListener {
        CTitleScene* owner = nullptr;
        void OnInput(u32 msg, uintptr_t wParam) override;       // 0x408f91
    };
    struct SoundListener : aaron::SceneListener {
        CTitleScene* owner = nullptr;
        void OnEnd() override;                                  // 0x409031
    };

    KeyListener& Keys() { return keys_; }
    SoundListener& Sounds() { return sounds_; }

    // Globals of the original (DAT_00418574 / DAT_00418e14), exposed for the tests.
    static bool g_firstVisit;
    static int g_introStep;

private:
    KeyListener keys_;
    SoundListener sounds_;
};

// CreateScene 0x40958d
aaron::Scene* CreateTitleScene(aaron::Window* host);

} // namespace rapture
