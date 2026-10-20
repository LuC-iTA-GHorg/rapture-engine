// Software MIDI sequencer: see MidiSeq.h for the semantics taken from moses.dll's MIDI thread.
#include "platform/MidiSeq.h"
#include "platform/Config.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

uint64_t MidiRealClock::NowMs() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return uint64_t(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

// moses.dll 0x10013120 CMidiPlayer::Play (+ the thread start 0x10014c30, whose first act is timeGetTime())
bool MidiSequencer::Start(const uint32_t* events, size_t count, bool loop, uint64_t nowMs, bool remapDrums) {
    Stop();
    if (!sink_ || !sink_->Open()) return false; // FUN_10012d3b midiOutOpen
    events_.assign(events, events + count);
    idx_ = 0;
    loop_ = loop;
    remap_ = remapDrums;
    target_ = nowMs;
    waiting_ = false;
    paused_ = false;
    running_ = true;
    return true;
}

// moses.dll 0x100131e6 -> 0x10012d00 (reset + close).  The thread is simply killed in the original (TerminateThread).
void MidiSequencer::Stop() {
    if (!running_) return;
    running_ = false;
    paused_ = false;
    events_.clear();
    idx_ = 0;
    if (sink_) {
        sink_->Reset();
        sink_->Close();
    }
}

void MidiSequencer::Finish() {
    // end of thread_proc: FUN_10012d00 (reset + close), then the "ended" notification (done by the caller)
    running_ = false;
    paused_ = false;
    events_.clear();
    idx_ = 0;
    if (sink_) {
        sink_->Reset();
        sink_->Close();
    }
}

void MidiSequencer::Pause(uint64_t nowMs) {
    if (!running_ || paused_) return;
    paused_ = true;
    pausedAt_ = nowMs;
    if (sink_) sink_->Reset(); // silence the hanging notes (running status of the device is left alone)
}

void MidiSequencer::Resume(uint64_t nowMs) {
    if (!running_ || !paused_) return;
    paused_ = false;
    if (nowMs > pausedAt_) target_ += nowMs - pausedAt_;
}

// moses.dll 0x10012ff9
void MidiSequencer::SetChannelMute(uint32_t channel, bool mute) { mute_[channel % 16] = mute ? 1 : 0; }

// moses.dll 0x100130ae: equal ratio = no scaling (both stored as 0)
void MidiSequencer::SetTempo(uint32_t num, uint32_t den) {
    if (num == den) num = den = 0;
    num_ = num;
    den_ = den;
}

// moses.dll 0x10012de3 CMidiPlayer::thread_proc, one pass of its loop per fetched dword
MidiSequencer::Status MidiSequencer::Poll(uint64_t nowMs, uint64_t* wakeMs) {
    if (!running_) return Status::Idle;
    if (paused_) {
        if (wakeMs) *wakeMs = UINT64_MAX;
        return Status::Waiting;
    }
    for (int budget = 0; budget < (1 << 20); ++budget) {
        if (waiting_) {
            if (nowMs < target_) {
                if (wakeMs) *wakeMs = target_;
                return Status::Waiting;
            }
            waiting_ = false;
        }
        // while (idx < count || (idx = 0, loop)):  an empty stream cannot loop (the original would spin forever)
        if (idx_ >= events_.size()) {
            if (!loop_ || events_.empty()) {
                Finish();
                return Status::Finished;
            }
            idx_ = 0;
        }
        uint32_t v = events_[idx_++];
        if (!(v & 0x80000000u)) {
            if (remap_ && (v & 0xf) == 9) v |= 0xf; // 0x10012de3: RemapDrums, tested on the low nibble of the whole dword
            if (!(mute_[v & 0xf] && (v & 0xf0) == 0x90)) // only note-ons of muted channels are dropped
                if (sink_) sink_->ShortMsg(v, target_);
        } else {
            uint32_t d = v & 0x7fffffffu;
            if (den_ != 0) d = (d * num_) / den_; // 32-bit unsigned arithmetic, as in the original
            target_ += d;
            waiting_ = true; // `if (now < target) Sleep(target - now)` happens at the top of the next iteration
        }
    }
    if (wakeMs) *wakeMs = nowMs; // budget exhausted (a looping stream without any delay): let the caller yield
    return Status::Waiting;
}

// ---------------------------------------------------------------------------------------------------------------------
#ifndef RAPTURE_WIN32
namespace {
// Non-Windows (or RAPTURE_FORCE_PORTABLE) default backend: accepts everything, plays nothing.  With RAPTURE_MIDI_LOG=1 every
// message is printed to stderr (useful to check a sequence without a synthesiser).
class NullMidiSink : public MidiSink {
public:
    NullMidiSink() {
        const char* e = std::getenv("RAPTURE_MIDI_LOG");
        log_ = e && e[0] == '1';
    }
    bool Open() override {
        if (log_) std::fprintf(stderr, "[midi] open\n");
        return true;
    }
    void ShortMsg(uint32_t msg, uint64_t ms) override {
        if (log_)
            std::fprintf(stderr, "[midi] %8llu ms  %02x %02x %02x\n", (unsigned long long)ms, msg & 0xff, (msg >> 8) & 0xff,
                         (msg >> 16) & 0xff);
    }
    void Reset() override {
        if (log_) std::fprintf(stderr, "[midi] reset\n");
    }
    void Close() override {
        if (log_) std::fprintf(stderr, "[midi] close\n");
    }

private:
    bool log_ = false;
};
} // namespace
std::unique_ptr<MidiSink> CreateDefaultMidiSink() { return std::unique_ptr<MidiSink>(new NullMidiSink()); }
#endif
