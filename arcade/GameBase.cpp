// G1 - CGameBase.  See GameBase.h.  Shoot.dll addresses in comments.
#include "arcade/GameBase.h"

#include "aaron/Window.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace rapture {

using moses::QUEUEMSG;

CGameBase* CGameBase::s_instance = nullptr;

static IPopupFactory* g_popupFactory = nullptr;
void SetPopupFactory(IPopupFactory* f) { g_popupFactory = f; }
IPopupFactory* GetPopupFactory() { return g_popupFactory; }

// ---------------------------------------------------------------------------------------------
// 0x100144a8.  The constructor zeroes 0x13 dwords from +0x6b0 (all the fields below), sets DAT_100325a8 = this,
// irisClosed = 1 (+0x6ec), slots (+0x6d4), def (+0x6c0) and creates the global CPropertyList.
CGameBase::CGameBase(aaron::Window* host, const char* name, int slotCount, int propCapacity, const GameDef* gd)
    : aaron::Scene(host, name), def(gd), slots(slotCount), players(1) {
    s_instance = this;
    props = new CPropertyList(propCapacity);
    irisClosed = 1;

    // Transition message tables (Shoot.dll 0x10031bf0 .. 0x10031cb4).  QUEUEMSG tmIris_[kind]: 100/101 XY, 104/105 Y, 102/103 X
    // (QM_IRIS_CLOSE_XY..): kind 0/2/4 close, 1/3/5 open; delay 50 ms, 10 steps.
    tmSync_ = moses::qm::Sync(1);                                  // 0x10031c50 {13, 1}
    tmNotify_ = moses::qm::Notify(CMD_TRANSITION_DONE, 0, 0);      // 0x10031c58 {9, 0xF000100A, b, c}: b/c filled by StartTransition
    tmHide_ = moses::qm::Show(0);                                  // 0x10031c68 {14, 0}
    static const u32 ops[6] = {moses::QM_IRIS_CLOSE_XY, moses::QM_IRIS_OPEN_XY, moses::QM_IRIS_CLOSE_Y,
                               moses::QM_IRIS_OPEN_Y, moses::QM_IRIS_CLOSE_X, moses::QM_IRIS_OPEN_X};
    for (int k = 0; k < 6; ++k) {
        tmIris_[k] = moses::qm::Iris(ops[k], 50, 10);
        int n = 0;
        tmList_[k][n++] = &tmSync_;
        tmList_[k][n++] = &tmIris_[k];
        if (k & 1) tmList_[k][n++] = &tmHide_; // opening kinds hide the cover afterwards (4 messages)
        tmList_[k][n++] = &tmNotify_;
        tmCount_[k] = n;
    }
}

// the destructor of the original deletes the player array (PtrArray::DeleteAll) after Scene-level members
CGameBase::~CGameBase() {
    DestroyBase();
    for (int i = 0; i < players.Count(); ++i) delete static_cast<CPlayerBase*>(players.At(i));
    players.Clear();
    if (s_instance == this) s_instance = nullptr;
}

// 0x10014e8e: props, ev0, ev1, menu sequence, transition (in this order), then the players array.
// Idempotent (the derived DestroyMembers / the destructor may both call it).
void CGameBase::DestroyBase() {
    delete props;
    props = nullptr;
    delete ev0;
    ev0 = nullptr;
    delete ev1;
    ev1 = nullptr;
    delete menuSeq;
    menuSeq = nullptr;
    delete transition;
    transition = nullptr;
}

void CGameBase::DestroyMembers() {
    DestroyBase();
    // original: players->vtbl+8() (RemoveAll); the players themselves are deleted by the array destructor
}

// ---------------------------------------------------------------------------------------------
void CGameBase::GetClientSizeSafe(aaron::POINT* p) {
    if (Host()) {
        Host()->GetClientSize(p);
    } else {
        p->x = 640;
        p->y = 480;
    }
}

void* CGameBase::CreatePopup(void* parent, int type, uintptr_t arg) {
    IPopupFactory* f = GetPopupFactory();
    return f ? f->CreateObject(parent, type, arg) : nullptr;
}

// ---------------------------------------------------------------------------------------------
// 0x10015078 (Aaron OnPreCreate).  Creates the players (CreatePlayer(i) + Init), loads the settings, creates the two
// event managers and the iris transition widget, registers the SPACE accelerator.
// QUIRKS: a player that fails Init is leaked; settings0 is incremented on every start.
static CEventSpriteSequencer* MakeManager(CGameBase* g) { // new CEventSpriteSequencer(g,1,1,0) + FUN_10017780 (Init or delete)
    CEventSpriteSequencer* m = new CEventSpriteSequencer(g, 1, 1, nullptr);
    if (!m->Init()) {
        delete m;
        return nullptr;
    }
    return m;
}

bool CGameBase::Init() {
    for (int i = 0; i < slots; ++i) {
        CPlayerBase* p = CreatePlayer(i);
        if (!p || !p->Init()) return false;
        players.Insert(p, -1);
    }
    LoadSaveSettings(GetMachineKey(), 0);
    ev0 = MakeManager(this);
    ev1 = MakeManager(this);
    aaron::POINT sz;
    GetClientSizeSafe(&sz);
    aaron::RECT r;
    r.left = 0;
    r.top = 0;
    r.right = sz.x;
    r.bottom = sz.y;
    int ox = -sz.x / 2, oy = -sz.y / 2; // OffsetRect
    r.left += ox;
    r.right += ox;
    r.top += oy;
    r.bottom += oy;
    aaron::AnimRectSprite* t = new aaron::AnimRectSprite(aaron::WidgetParent(RootWidget()), 0, 0, 10000, 0, 0x2000, &r, 0);
    transition = static_cast<aaron::AnimRectSprite*>(aaron::Widget::InitOrDelete(t));
    AddAccelerator(0x20, CMD_SKIP, 0, 0);
    settings0 += 1;
    return ev0 && ev1 && transition;
}

// ---------------------------------------------------------------------------------------------
// 0x10014e0f.  CMD_SKIP: stop both managers (skips the running intro/sequence); CMD_LEAVE: back to the scene that loaded us.
int CGameBase::OnCommand(u32 id, uintptr_t) {
    if (id == CMD_SKIP) {
        if (ev0) ev0->Stop(1);
        if (ev1) ev1->Stop(1);
    } else if (id == CMD_LEAVE) {
        // ord 697 (Window::LoadScene) on the scene's arg (+0x530): the Module of the previous scene (the Title)
        if (Host() && Arg()) Host()->PostLoadScene(reinterpret_cast<aaron::Module*>(Arg()), 0);
        return 1;
    }
    return 0;
}

// Aaron slot 3 -> slot 15 (0x3c): see header.
void CGameBase::OnObjectEvent(moses::CAnimObj* obj, intptr_t a, intptr_t b, intptr_t c) {
    aaron::Widget* w = obj ? aaron::Widget::GetFromObj(obj->Target()) : nullptr;
    OnSoundMessage(w, u32(a), u32(b), c);
}

// ---------------------------------------------------------------------------------------------
// 0x100153af.  (w = the widget whose queue sent the QM_NOTIFY; cmd = a; b, c as sent)
void CGameBase::OnSoundMessage(aaron::Widget* w, u32 cmd, u32 b, intptr_t c) {
    switch (cmd) {
    case CMD_TRANSITION_DONE: {
        Focus().Lock(1); // unlock
        switch (b) {
        case 1: { // an event waits for the transition: it is done
            IEvent* ev = reinterpret_cast<IEvent*>(c);
            if (ev && ev->owner) ev->owner->OnChildDone();
            break;
        }
        case 2: EvB0(); break;
        case 3: StartPlayers(int(c)); break;
        case 4: EvA4(); break;
        case 5: Resume(); break;
        case 6: EvA8(int(c)); break;
        case 7: Ev98(); break;
        case 8: PostCommand(); break;
        default: break;
        }
        break;
    }
    case CMD_SFX_POS:
    case CMD_SFX_POS_LOOP: {
        aaron::POINT pt;
        pt.x = pt.y = 0;
        if (c != 0 && w && w->GetDrawObj()) w->GetDrawObj()->GetBoundsMid(pt);
        PlaySfxIfEnabled(b, 0, pt.x, 100, cmd == CMD_SFX_POS_LOOP ? 1 : 0, 0, 0);
        break;
    }
    case CMD_SFX: PlaySoundRaw(b, 0, 0, 100, 0, 0, 0); break;
    case CMD_SND:
    case CMD_SND_LOOP: PlaySound(b, 0, 0, 100, cmd == CMD_SND_LOOP ? 1 : 0, 0, 0); break;
    case CMD_SND_STOP: StopSound(b); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------------------------
// 0x10014633.
int CGameBase::StartTransition(int kind, u32 action, uintptr_t arg, int level) {
    switch (kind) {
    case 0:
    case 2:
    case 4:
        if (irisClosed != 0) return 0;
        irisClosed = 1;
        break;
    case 1:
    case 3:
    case 5:
        if (irisClosed == 0) return 0;
        irisClosed = 0;
        break;
    default: return 0;
    }
    Focus().Lock(0); // lock
    tmNotify_.b = intptr_t(action);
    tmNotify_.c = intptr_t(arg); // action 1: arg = IEvent* (pointer-sized, like the original)
    static_cast<moses::CDrawObj*>(static_cast<moses::CDrawRect*>(transition))->SetLevel(level);
    static_cast<moses::CAnimObj*>(transition)->Start(tmList_[kind], u32(tmCount_[kind]));
    return 1;
}

// 0x10014420 (CEventUserFunc callback; p1 = kind, p2 = level)
void* CGameBase::TransitionMsgCallback(CEventUserFunc* ev, uintptr_t p1, uintptr_t p2, int phase, void*) {
    CGameBase* g = s_instance;
    if (!g) return nullptr;
    if (phase == 1) {
        if (g->StartTransition(int(p1), 1, reinterpret_cast<uintptr_t>(static_cast<IEvent*>(ev)), int(p2)) == 0 && ev->owner)
            ev->owner->OnChildDone();
    } else if (phase == 3) {
        g->transition->Show(false);
        g->irisClosed = 0;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// 0x100145c3.  QUIRK: with `simultaneous` the next step is StartPlayers(0) (optionally behind a close transition).
void CGameBase::NextStateOrQuit() {
    if (!AllPlayersDone()) {
        if (simultaneous == 0) NextAlivePlayer();
        else if (useTransitions == 0 || StartTransition(0, 3, 0, 10000) == 0) StartPlayers(0);
    } else {
        Ev9C();
    }
}

void CGameBase::PostCommand() { PostMessage(0x111, CMD_LEAVE, 0); } // 0x10014817

// 0x10014837.  Runs the menu sequence on ev0.  The extra refcount keeps the sequence alive after ev0 removes it.
void CGameBase::ShowMenu() {
    if (menuSeq) {
        Cursor().ShowCursor(0);
        menuSeq->refcount += 1;
        ev0->Add(menuSeq);
        ev0->StartSequence(0, &CGameBase::MenuCallback, reinterpret_cast<uintptr_t>(this), CMD_STATE_8);
    }
}

// 0x10016150
void CGameBase::MenuCallback(CEventSequencer*, int code, uintptr_t game, uintptr_t st) {
    CGameBase* g = reinterpret_cast<CGameBase*>(game);
    if (code == 1) g->state = u32(st);
    else if (code == 2) g->DispatchState();
}

// 0x10014912
void CGameBase::EndGame(int completed) {
    Pause();
    ev1->Stop(1);
    if (completed != 0) AdvanceLevel();
    EvA8(completed);
}

// 0x10014969
void CGameBase::StartPlayers(int index) {
    if (simultaneous == 0) {
        SetCurrentPlayer(index);
    } else {
        for (u32 i = 0; i < numPlayers; ++i) SetCurrentPlayer(int(i));
    }
    if (useTransitions == 0 || StartTransition(1, 4, 0, 10000) == 0) EvA4();
}

// 0x100149f9
void CGameBase::RestartFromFirstPlayer() {
    Cursor().SetCursor(0);
    curIndex = 0;
    curPlayer = static_cast<CPlayerBase*>(players.At(curIndex));
    if (StartTransition(1, 2, 0, 10000) == 0) EvB0();
}

// 0x10014a6d
aaron::RECT* CGameBase::GetPlayRect(aaron::RECT* out) {
    if (def->playRect == nullptr) {
        aaron::POINT sz;
        GetClientSizeSafe(&sz);
        int half = sz.y / 2 - 0x28;
        out->left = -(sz.x / 2);
        out->top = -half;
        out->right = sz.x / 2;
        out->bottom = half;
    } else {
        *out = *def->playRect;
    }
    return out;
}

// 0x10014b48
bool CGameBase::AllPlayersDone() {
    if (simultaneous == 0) return curPlayer ? curPlayer->IsFinished() : false;
    for (u32 i = 0; i < numPlayers; ++i) {
        CPlayerBase* p = static_cast<CPlayerBase*>(players.At(int(i)));
        if (!p->IsFinished()) return false;
    }
    return true;
}

// 0x10014bd0
void CGameBase::SetMute(int sfx, int music) {
    int oldSfx = sfxMute;
    if (oldSfx != sfx) sfxMute = sfx;
    bool musicChanged = musicMute != music;
    if (musicChanged) musicMute = music;
    StopSounds(sfx, music, 0);
    OnMuteChanged(oldSfx != sfx, musicChanged);
    if (musicId != 0 && music == 0 && musicChanged) PlayMusicIfEnabled(musicId, 1, 0, 100, 0, 0, 0);
    LoadSaveSettings(GetMachineKey(), 1);
}

// 0x10014ca5
void CGameBase::SaveSettingsAndReturn() {
    LoadSaveSettings(GetMachineKey(), 1);
    ResetGame();
    Ev94();
}

// 0x10014ce8.  Round robin from the current player: first alive one; none -> EvAC (game over); else go to it (behind a
// transition when enabled).
void CGameBase::NextAlivePlayer() {
    if (numPlayers == 0) return;
    bool none = true;
    u32 tries = 0;
    uint8_t b = uint8_t(curIndex);
    do {
        if (!none) break;
        if (int(b) < int(numPlayers) - 1) b = uint8_t(b + 1);
        else b = 0;
        CPlayerBase* p = static_cast<CPlayerBase*>(players.At(b));
        if (p && p->IsAlive()) none = false;
        tries += 1;
    } while (tries < numPlayers);
    if (none) {
        EvAC();
    } else if (useTransitions == 0 || StartTransition(0, 3, b, 10000) == 0) {
        StartPlayers(b);
    }
}

// 0x10014fa0 ("GotoState0"): back to the main menu state: cursor on, stop the sfx, open the iris
void CGameBase::StateB4() {
    Cursor().ShowCursor(1);
    StopSounds(1, 0, 0);
    StartTransition(1, 0, 0, 10000);
}

// 0x10014fde ("GotoState3")
void CGameBase::StateB8() {
    if (useTransitions != 0 && StartTransition(0, 3, 0, 10000) != 0) return;
    StartPlayers(0);
}

// 0x10015019 ("GotoState8")
void CGameBase::StateC4() {
    if (StartTransition(0, 8, 0, 10000) == 0) PostCommand();
}

// 0x10015046
void CGameBase::StateD0() {
    if (simultaneous == 0) NextAlivePlayer();
    else EvAC();
}

// 0x10015381 (arcade 0x100195d1 overrides)
void CGameBase::StateD4(int arg) {
    if (arg == 0) Ev98();
    else ShowNewGameDialog();
}

// ---------------------------------------------------------------------------------------------
// 0x100155e0 / 0x1001562f
int CGameBase::PlayMusicIfEnabled(u32 res, int a, int pan, int vol, int b, int u0, int u1) {
    return musicMute == 0 ? PlaySound(res, a, pan, vol, b, u0, u1) : 0;
}
int CGameBase::PlaySfxIfEnabled(u32 res, int a, int pan, int vol, int b, int u0, int u1) {
    return sfxMute == 0 ? PlaySound(res, a, pan, vol, b, u0, u1) : 0;
}

// 0x1001567e: ResetGame on every player, then player 0 becomes current
void CGameBase::ResetPlayers() {
    for (u32 i = 0; i < numPlayers; ++i) {
        CPlayerBase* p = static_cast<CPlayerBase*>(players.At(int(i)));
        if (p) p->ResetGame();
    }
    curIndex = 0;
    curPlayer = static_cast<CPlayerBase*>(players.At(curIndex));
}

// 0x10015703.  state CMD_STATE_x -> hook: 1:B8 2:C4 3:C8 4:D0 5:BC 6:C0 7:D8 8:B4
void CGameBase::DispatchState() {
    switch (state) {
    case CMD_STATE_1: StateB8(); break;
    case CMD_STATE_2: StateC4(); break;
    case CMD_STATE_3: StateC8(); break;
    case CMD_STATE_4: StateD0(); break;
    case CMD_STATE_5: StateBC(); break;
    case CMD_STATE_6: StateC0(); break;
    case CMD_STATE_7: StateD8(); break;
    case CMD_STATE_8: StateB4(); break;
    default: break;
    }
}

// 0x100157da.  Single mode: the player becomes current and is activated; "simultaneous": all are activated.  Then the
// game properties (player list) of every player are snapshotted.
void CGameBase::SetCurrentPlayer(int index) {
    if (simultaneous == 0) {
        curIndex = index;
        curPlayer = static_cast<CPlayerBase*>(players.At(curIndex));
        curPlayer->OnActivate(1);
    } else {
        for (u32 i = 0; i < numPlayers; ++i) static_cast<CPlayerBase*>(players.At(int(i)))->OnActivate(1);
    }
    for (u32 i = 0; i < numPlayers; ++i) static_cast<CPlayerBase*>(players.At(int(i)))->PropSnapshot(-1);
}

// 0x10015c7f
void CGameBase::Resume() {
    if (running == 0) {
        running = 1;
        if (simultaneous == 0) {
            if (curPlayer) curPlayer->Resume();
        } else {
            for (u32 i = 0; i < numPlayers; ++i) static_cast<CPlayerBase*>(players.At(int(i)))->Resume();
        }
        if (musicId != 0) PlayMusicIfEnabled(musicId, 1, 0, 100, 0, 0, 0);
    }
}

// 0x10015d40
void CGameBase::Pause() {
    if (running != 0) {
        running = 0;
        if (simultaneous == 0) {
            if (curPlayer) curPlayer->Pause();
        } else {
            for (u32 i = 0; i < numPlayers; ++i) static_cast<CPlayerBase*>(players.At(int(i)))->Pause();
        }
        if (musicId != 0) StopSound(musicId);
    }
}

// ---------------------------------------------------------------------------------------------
// 0x100158d5.  Registry values (all in `key`): GBNumPlayers, GBCurPlayer, GBP<i>Prop<j> (the game properties of every
// player).  NOTE: SAVING WRITES THE SNAPSHOT of each property (list vtbl +0x3c = GetSnapshot), loading Set()s the value.
// Loading sets numPlayers through SetNumPlayers() and curIndex directly (curPlayer is not updated).
void CGameBase::SerializeGlobals(aaron::RegKey* key, int save) {
    char name[52];
    if (save == 0) {
        u32 n = key->GetDword("GBNumPlayers", 0);
        SetNumPlayers(int(n));
        curIndex = int(key->GetDword("GBCurPlayer", 0));
    } else {
        key->SetDword("GBNumPlayers", numPlayers);
        key->SetDword("GBCurPlayer", u32(curIndex));
    }
    for (u32 i = 0; i < numPlayers && int(i) < players.Count(); ++i) {
        CPlayerBase* p = static_cast<CPlayerBase*>(players.At(int(i)));
        int count = p->props->Size();
        for (int j = 0; j < count; ++j) {
            std::snprintf(name, sizeof(name), "GBP%iProp%d", int(i), j);
            if (save == 0) p->PropSetValue(j, int(key->GetDword(name, 0)));
            else key->SetDword(name, u32(p->PropGetSnapshot(j)));
        }
    }
}

// 0x10015df5.  16 bytes: {settings0, numPlayers (dword), sfxMute, musicMute}.  Loading forces numPlayers >= 1.
void CGameBase::SerializeSettings(int save, uint8_t** pp) {
    u32 v[4];
    if (save == 0) {
        std::memcpy(v, *pp, 16);
        settings0 = v[0];
        numPlayers = uint8_t(v[1]);
        sfxMute = int(v[2]);
        musicMute = int(v[3]);
        if (numPlayers < 2) numPlayers = 1;
    } else {
        v[0] = settings0;
        v[1] = numPlayers;
        v[2] = u32(sfxMute);
        v[3] = u32(musicMute);
        std::memcpy(*pp, v, 16);
    }
    *pp += 0x10;
}

// 0x10015aba.  The blob is GetSettingsSize() bytes: SerializeSettings + Serialize of each of the `slots` players.
// QUIRK: CArcadeGame::GetSettingsSize is slots*4+0x18, but CPlayer writes 8 bytes: exact only for 2 slots.
void CGameBase::LoadSaveSettings(aaron::RegKey* key, int save) {
    if (!key) return;
    u32 size = u32(GetSettingsSize());
    std::vector<uint8_t> buf(std::max<size_t>(size, 16 + size_t(slots) * 8), 0);
    uint8_t* p = buf.data();
    if (save == 0) {
        key->GetBinary("Settings", buf.data(), size, nullptr);
        SerializeSettings(0, &p);
        for (int i = 0; i < slots && i < players.Count(); ++i) static_cast<CPlayerBase*>(players.At(i))->Serialize(0, &p);
    } else {
        SerializeSettings(save, &p);
        for (int i = 0; i < slots && i < players.Count(); ++i) static_cast<CPlayerBase*>(players.At(i))->Serialize(save, &p);
        key->SetBinary("Settings", buf.data(), size);
    }
}

} // namespace rapture
