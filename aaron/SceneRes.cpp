// Scene: resources, background/overlay, sound. Package 3.
// Aaron OpenArchive ord 659 0x1000e08c, LoadResource ord 429 0x1000cfdb, FreeResource ord 207 0x1000cd0d, LoadString ord 430
// 0x1000d010, GetIndexedResId ord 289 0x1000cd33, SetBackground ord 606 0x1000df00, SetSurfaceColor ord 613 0x1000df8d,
// SetOverlayRes ord 656 0x1000e055, PlaySound ord 534 0x1000dd15, PlaySoundSimple ord 535 0x1000dd9a, StopSounds ord 693,
// StopSound ord 694 0x1000e162, common sound helper FUN_1000dcb5.
#include "aaron/Scene.h"

#include "aaron/Application.h"

namespace aaron {

int Scene::OpenArchive(int slot, const char* path, const char* baseDir) { // ord 659 0x1000e08c
    if (!path) { // 0x1000b97a: close the slot
        res_.Bind(u32(slot), nullptr);
        return 0; // (the original returns false here too)
    }
    Application* app = Application::GetApp();
    ResArchive* a = nullptr;
    if (app) {
        a = app->AcquireArchive(path, baseDir); // shared between scenes
    } else {
        a = new ResArchive(nullptr);
        if (!a->OpenPath(path, baseDir)) { delete a; a = nullptr; }
        else a->AddRef();
    }
    if (!a) return 0;
    res_.Bind(u32(slot), a); // AddRef inside
    a->Release();            // drop the reference given by AcquireArchive
    return 1;
}

// Aaron ord 138 0x1000ca5f -> FUN_1000b818(res table, obj)
u32 Scene::AddResource(void* obj, void (*deleter)(void*)) {
    u32 id = AddResource(obj);
    if (id != kNoRes) dyn_[id & 0xffff].deleter = deleter;
    return id;
}

void Scene::FreeDynamicResource(u32 id) { // ord 202 0x1000b797
    u32 i = id & 0xffff;
    if ((id >> 16) != 0xff80 || i >= dyn_.size() || !dyn_[i].obj) return;
    void* o = dyn_[i].obj;
    auto del = dyn_[i].deleter;
    dyn_[i] = DynEntry{};
    if (del) del(o); // the original runs the object's virtual destructor (delete flag 1)
}

u32 Scene::AddResource(void* obj) {
    u32 i = 0;
    while (i < dyn_.size() && dyn_[i].obj != nullptr) ++i; // first free entry (the original grows by 8 entries at a time)
    if (i >= dyn_.size()) dyn_.resize(i + 8);
    dyn_[i].obj = obj;
    dyn_[i].refcount = 1;
    return (i & 0xffff) | 0xff800000u;
}

void* Scene::DynamicResource(u32 id) const {
    u32 i = id & 0xffff;
    return (id >> 16) == 0xff80 && i < dyn_.size() ? dyn_[i].obj : nullptr;
}

// The dynamic resources (slot 0xff, ids 0xff80xxxx made by AddResource) live in the same kind of table as the archive resources:
// Load = FUN_1000b6d3 (-> the entry's object, refcount + 1), Free = ResTable::Release 0x1000bcdd (refcount < 2: the table's Free hook 0x1000b797 deletes the
// object through its virtual destructor and clears the entry; else refcount - 1).  AddResource gives the entry refcount 1 (the creator's reference).
void* Scene::LoadDynamicResource(u32 id) {
    u32 i = id & 0xffff;
    if (i >= dyn_.size() || !dyn_[i].obj) return nullptr;
    ++dyn_[i].refcount;
    return dyn_[i].obj;
}

void Scene::ReleaseDynamicResource(u32 id) {
    u32 i = id & 0xffff;
    if (i >= dyn_.size()) return;
    if (dyn_[i].refcount < 2) {
        if (dyn_[i].obj) FreeDynamicResource(id);   // deletes the object (deleter) and clears the entry
        dyn_[i] = DynEntry{};
    } else {
        --dyn_[i].refcount;
    }
}

void* Scene::LoadResource(u32 id) { // ord 429 0x1000cfdb
    if (id == kNoRes) return nullptr;
    if (ResSlot(id) == 0xff) return LoadDynamicResource(id);
    return res_.Load(id);
}

void Scene::FreeResource(u32 id) { // ord 207 0x1000cd0d
    if (id == kNoRes) return;
    if (ResSlot(id) == 0xff) { ReleaseDynamicResource(id); return; }
    res_.Free(id);
}

int Scene::LoadString(u32 id, char* buf, u32 max) { // ord 430 0x1000d010
    if (id == kNoRes) return 0;
    ResArchive* a = res_.Archive(id);
    if (!a) return 0;
    return int(a->LoadStringA(id, buf, max));
}

u32 Scene::GetIndexedResId(u32 groupId, u32 n) { return res_.GetIndexedResId(groupId, n); } // ord 289

// GetObj without refcount (FUN_1000ccd6): the object of an already loaded resource.
static void* loaded_obj(ResTable& t, u32 id) {
    if (id == kNoRes) return nullptr;
    ResTable::Slot& s = t.At(ResSlot(id));
    u32 i = ResIndex(id);
    if (i >= s.entries.size()) return nullptr;
    return s.entries[i].obj;
}

void Scene::SetBackground(u32 resId) { // ord 606 0x1000df00
    FreeResource(bgResId_);
    if (resId == kNoRes) {
        CDrawView::SetBackground(nullptr);
    } else {
        moses::CDibData* d = static_cast<moses::CDibData*>(LoadResource(resId));
        CDrawView::SetBackground(d);
        RECT r;
        GetDisplayBox(r);
        Application* app = Application::GetApp();
        if (app) app->Audio().Lib().SetPanInfo(0, (r.right - r.left) / 2); // FUN_10011f23(audio, 0, w/2)
    }
    bgResId_ = resId;
}

void Scene::SetSurfaceColor(u32 rawColor) { // ord 613 0x1000df8d
    surfaceColor_ = rawColor;
    CDrawView::SetSurfaceColor(rawColor);
}

void Scene::SetOverlayRes(u32 resId) { // ord 656 0x1000e055
    FreeResource(overlayRes_);
    LoadResource(resId);
    overlayRes_ = resId;
}

void Scene::ShowOverlay(int show) { // 0x1000e0d3
    if (overlayRes_ == kNoRes || !bgSprite_) return;
    if (!show) {
        bgSprite_->Hide();
    } else {
        bgSprite_->AnimateDib(static_cast<moses::CDibObj*>(loaded_obj(res_, overlayRes_)));
        Service();
    }
}

// ---------------------------------------------------------------------------------------------
// FUN_1000dcb5: load the sound resource, play it; on failure release it. (The resource is released again by AMSG_RESFREED
// when the sound ends.)
int Scene::PlaySoundInfo(u32 resId, moses::PLAYSOUNDINFO* info) { // ord 536 0x1000dcb5
    moses::CAudioSnd* snd = static_cast<moses::CAudioSnd*>(LoadResource(resId));
    if (!snd) return 0;
    Application* app = Application::GetApp();
    if (!app || !app->Audio().SoundPlay(snd, info)) { // FUN_10011dea = AudioMgr::SoundPlay -> CAudioLib::SoundPlay
        FreeResource(resId);
        return 0;
    }
    return 1;
}

static int play_common(Scene* sc, u32 resId, moses::PLAYSOUNDINFO* info) { return sc->PlaySoundInfo(resId, info); }

// 0x1000dd15. The decompiled body does NOT match the header's parameter names; the real mapping is:
//   flags = (a & 1) << 2 [PSF_LOOP] | 0x10 [PSF_PAN] | (b & 1) [PSF_MIX] | (user0 & 1) << 1 [0x02, "notify" bit]
//   info.pan = pan, info.user0 = volume-argument (e.g. 100: stored as user0, PSF_VOLUME is NOT set), info.user1 = user1.
int Scene::PlaySound(u32 resId, int a, int pan, int volume, int b, int user0, int user1) {
    moses::PLAYSOUNDINFO info;
    info.size = 0x18;
    info.flags = (u32(a & 1) << 2) | moses::PSF_PAN | u32(b & 1) | (u32(user0 & 1) << 1);
    info.pan = u32(pan);
    info.user0 = u32(volume);
    info.user1 = u32(user1);
    return play_common(this, resId, &info);
}

// 0x1000dd9a. The listener (64-bit pointer) is kept in a side table; info.user1 carries a nonzero marker.
int Scene::PlaySoundSimple(u32 resId, int flags, SceneListener* listener) {
    moses::PLAYSOUNDINFO info;
    info.size = 0x18;
    info.flags = u32(flags & 1) << 1;
    info.user1 = listener ? 1u : 0u;
    if (listener) soundListeners_.emplace_back(resId, listener);
    int r = play_common(this, resId, &info);
    if (!r && listener) SoundListenerFor(resId, true);
    return r;
}

SceneListener* Scene::SoundListenerFor(u32 sndId, bool remove) {
    for (size_t i = 0; i < soundListeners_.size(); ++i) {
        if (soundListeners_[i].first == sndId) {
            SceneListener* l = soundListeners_[i].second;
            if (remove) soundListeners_.erase(soundListeners_.begin() + long(i));
            return l;
        }
    }
    return nullptr;
}

void Scene::StopSounds(int waves, int midi, int cd) { // ord 693 0x1000e131 (FUN_10011f93 of the AudioMgr)
    Application* app = Application::GetApp();
    if (!app) return;
    app->Audio().StopAll(waves, midi, cd); // AudioMgr ord 695 0x10011f93
}

void Scene::StopSound(u32 resId) { // ord 694 0x1000e162
    Application* app = Application::GetApp();
    if (!app) return;
    if (resId == kNoRes) {
        app->Audio().Lib().SoundStop(nullptr);
    } else {
        moses::CAudioSnd* snd = static_cast<moses::CAudioSnd*>(loaded_obj(res_, resId));
        if (snd) app->Audio().Lib().SoundStop(snd);
    }
}

} // namespace aaron
