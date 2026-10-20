// MidiPlayer backend. Windows-only (winmm MCI sequencer); stubs elsewhere.
#include "platform/Midi.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>

#include <cstdio>
#include <cstring>

namespace {
const char* kAlias = "hs_midi";
}

bool MidiPlayer::Play(const std::vector<uint8_t>& smf, bool loop) {
    Stop();
    char dir[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, dir);
    if (!n || n >= MAX_PATH) return false;
    tmpPath_ = std::string(dir) + "hs_music.mid";
    FILE* f = std::fopen(tmpPath_.c_str(), "wb");
    if (!f) return false;
    std::fwrite(smf.data(), 1, smf.size(), f);
    std::fclose(f);

    std::string cmd = "open \"" + tmpPath_ + "\" type sequencer alias " + kAlias;
    if (mciSendStringA(cmd.c_str(), nullptr, 0, nullptr) != 0) return false;
    open_ = true;
    loop_ = loop;
    mciSendStringA((std::string("set ") + kAlias + " time format milliseconds").c_str(), nullptr, 0, nullptr);
    if (mciSendStringA((std::string("play ") + kAlias + " from 0").c_str(), nullptr, 0, nullptr) != 0) {
        Stop();
        return false;
    }
    return true;
}

void MidiPlayer::Stop() {
    if (!open_) return;
    mciSendStringA((std::string("stop ") + kAlias).c_str(), nullptr, 0, nullptr);
    mciSendStringA((std::string("close ") + kAlias).c_str(), nullptr, 0, nullptr);
    open_ = false;
    if (!tmpPath_.empty()) std::remove(tmpPath_.c_str());
}

bool MidiPlayer::IsPlaying() {
    if (!open_) return false;
    char buf[64] = {0};
    if (mciSendStringA((std::string("status ") + kAlias + " mode").c_str(), buf, sizeof(buf), nullptr) != 0)
        return false;
    return std::strcmp(buf, "playing") == 0;
}

void MidiPlayer::Update() {
    if (open_ && loop_ && !IsPlaying())
        mciSendStringA((std::string("play ") + kAlias + " from 0").c_str(), nullptr, 0, nullptr);
}

#else

bool MidiPlayer::Play(const std::vector<uint8_t>&, bool) { return false; }
void MidiPlayer::Stop() {}
bool MidiPlayer::IsPlaying() { return false; }
void MidiPlayer::Update() {}

#endif
