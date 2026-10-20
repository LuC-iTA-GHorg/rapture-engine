// Software MIDI sequencer tests (limits branch, task 1): `<game>.exe --engine-test 10`.
// Deterministic: the sequencer is driven with a virtual clock and a recording sink; the threaded test uses the real clock but only
// asserts properties that hold on any machine (order, never early, minimum duration).
#include "tests/EngineTests.h"

#include "moses/AudioLib.h"
#include "platform/Midi.h"
#include "platform/MidiSeq.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <utility>

using namespace moses;

namespace {

int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

uint32_t Msg(uint8_t st, uint8_t d1, uint8_t d2) { return st | d1 << 8 | d2 << 16; }
uint32_t Delay(uint32_t ms) { return 0x80000000u | ms; }
std::vector<uint8_t> Bytes(const std::vector<uint32_t>& l) {
    std::vector<uint8_t> v;
    for (uint32_t d : l) for (int k = 0; k < 4; ++k) v.push_back(uint8_t(d >> (8 * k)));
    return v;
}

using St = MidiSequencer::Status;

struct Rig {
    MidiRecordingSink sink;
    MidiManualClock clock;
    MidiSequencer seq;
    Rig() : seq(&sink) { sink.clock = &clock; }
    bool Start(const std::vector<uint32_t>& ev, bool loop, uint64_t at, bool remap = false) {
        clock.now = at;
        return seq.Start(ev.data(), ev.size(), loop, at, remap);
    }
    St Poll(uint64_t t, uint64_t* wake = nullptr) {
        clock.now = t;
        uint64_t w = 0;
        St s = seq.Poll(t, &w);
        if (wake) *wake = w;
        return s;
    }
};

void TestOrderingAndTiming() {
    Rig r;
    std::vector<uint32_t> ev = {Msg(0x90, 60, 100), Delay(100), Msg(0x80, 60, 0), Delay(250), Msg(0x91, 64, 90), Delay(50)};
    CHECK(r.Start(ev, false, 1000));
    CHECK(r.sink.opens == 1);
    uint64_t wake = 0;
    CHECK(r.Poll(1000, &wake) == St::Waiting); // the first event is due at once
    CHECK(wake == 1100);
    CHECK(r.sink.events.size() == 1 && r.sink.events[0].msg == Msg(0x90, 60, 100) && r.sink.events[0].scheduledMs == 1000);
    CHECK(r.Poll(1099, &wake) == St::Waiting && wake == 1100 && r.sink.events.size() == 1); // never early
    CHECK(r.Poll(1100, &wake) == St::Waiting && wake == 1350);
    CHECK(r.sink.events.size() == 2 && r.sink.events[1].scheduledMs == 1100 && r.sink.events[1].msg == Msg(0x80, 60, 0));
    CHECK(r.Poll(1349, &wake) == St::Waiting && r.sink.events.size() == 2);
    CHECK(r.Poll(1350, &wake) == St::Waiting && wake == 1400);
    CHECK(r.sink.events.size() == 3 && r.sink.events[2].scheduledMs == 1350);
    CHECK(r.Poll(1399) == St::Waiting && r.seq.Running()); // the trailing delay is part of the length
    CHECK(r.sink.closes == 0);
    CHECK(r.Poll(1400) == St::Finished);
    CHECK(!r.seq.Running());
    CHECK(r.sink.closes == 1 && r.sink.resets == 1);
    CHECK(r.Poll(1500) == St::Idle);
    CHECK(r.sink.events.size() == 3);
}

void TestCatchUp() {
    // A late poll fires everything that is due in one go, with the exact scheduled times (no drift, no re-sync to "now").
    Rig r;
    std::vector<uint32_t> ev = {Msg(0x90, 1, 1), Delay(10), Msg(0x90, 2, 1), Delay(20), Msg(0x90, 3, 1), Delay(30), Msg(0x90, 4, 1)};
    CHECK(r.Start(ev, false, 100));
    CHECK(r.Poll(5000) == St::Finished);
    CHECK(r.sink.events.size() == 4);
    CHECK(r.sink.events[0].scheduledMs == 100 && r.sink.events[1].scheduledMs == 110 && r.sink.events[2].scheduledMs == 130 &&
          r.sink.events[3].scheduledMs == 160);
    CHECK(r.sink.events[3].emittedMs == 5000);
    // zero delays and consecutive events at the same time keep their order
    Rig q;
    std::vector<uint32_t> e2 = {Msg(0xc0, 5, 0), Msg(0x90, 1, 1), Delay(0), Msg(0x80, 1, 0)};
    CHECK(q.Start(e2, false, 0));
    CHECK(q.Poll(0) == St::Finished);
    CHECK(q.sink.events.size() == 3 && q.sink.events[0].msg == Msg(0xc0, 5, 0) && q.sink.events[2].msg == Msg(0x80, 1, 0));
}

void TestMute() {
    Rig r;
    std::vector<uint32_t> ev = {Msg(0x90, 60, 100), Msg(0x91, 61, 100), Delay(100), Msg(0x80, 60, 0), Msg(0x81, 61, 0),
                                Msg(0x90, 62, 0), Msg(0xb0, 7, 90), Delay(100), Msg(0x90, 63, 100), Msg(0x91, 64, 100),
                                Delay(100), Msg(0x90, 65, 100), Delay(10)};
    CHECK(r.Start(ev, false, 0));
    r.Poll(0); // both note-ons go out
    CHECK(r.sink.events.size() == 2);
    r.seq.SetChannelMute(0, true); // live, while playing
    CHECK(r.seq.ChannelMuted(0) && !r.seq.ChannelMuted(1) && r.seq.ChannelMuted(16)); // index = channel % 16
    r.Poll(100);
    // note-offs of ch0 and ch1 pass, the note-on (velocity 0) of the muted ch0 is dropped, the controller of ch0 passes
    CHECK(r.sink.events.size() == 5);
    CHECK(r.sink.events[2].msg == Msg(0x80, 60, 0) && r.sink.events[3].msg == Msg(0x81, 61, 0) && r.sink.events[4].msg == Msg(0xb0, 7, 90));
    r.Poll(200); // ch0 note-on dropped, ch1 note-on goes
    CHECK(r.sink.events.size() == 6 && r.sink.events[5].msg == Msg(0x91, 64, 100));
    r.seq.SetChannelMute(0, false); // unmute live
    r.Poll(300);
    CHECK(r.sink.events.size() == 7 && r.sink.events[6].msg == Msg(0x90, 65, 100));
    CHECK(r.Poll(310) == St::Finished);
    // the mute table survives the next Start (CMidiPlayer keeps it)
    r.seq.SetChannelMute(1, true);
    std::vector<uint32_t> e2 = {Msg(0x91, 1, 1), Msg(0x92, 2, 1)};
    r.sink.events.clear();
    CHECK(r.Start(e2, false, 0));
    r.Poll(0);
    CHECK(r.sink.events.size() == 1 && r.sink.events[0].msg == Msg(0x92, 2, 1));
}

void TestDrumRemap() {
    // RemapDrums: low nibble 9 -> 15, THEN the mute test on channel 15
    std::vector<uint32_t> ev = {Msg(0x99, 36, 100), Msg(0x89, 36, 0), Msg(0x90, 50, 100)};
    {
        Rig r;
        CHECK(r.Start(ev, false, 0, true));
        r.Poll(0);
        CHECK(r.sink.events.size() == 3);
        CHECK(r.sink.events[0].msg == Msg(0x9f, 36, 100) && r.sink.events[1].msg == Msg(0x8f, 36, 0) && r.sink.events[2].msg == Msg(0x90, 50, 100));
    }
    {
        Rig r; // muting channel 10 (index 9) does nothing once the drums are on 16
        r.seq.SetChannelMute(9, true);
        CHECK(r.Start(ev, false, 0, true));
        r.Poll(0);
        CHECK(r.sink.events.size() == 3 && r.sink.events[0].msg == Msg(0x9f, 36, 100));
    }
    {
        Rig r; // muting channel 16 (index 15) silences the drums
        r.seq.SetChannelMute(15, true);
        CHECK(r.Start(ev, false, 0, true));
        r.Poll(0);
        CHECK(r.sink.events.size() == 2 && r.sink.events[0].msg == Msg(0x8f, 36, 0));
    }
    {
        Rig r; // option off: nothing is rewritten, the channel 10 mute works
        r.seq.SetChannelMute(9, true);
        CHECK(r.Start(ev, false, 0, false));
        r.Poll(0);
        CHECK(r.sink.events.size() == 2 && r.sink.events[0].msg == Msg(0x89, 36, 0));
    }
}

void TestTempo() {
    std::vector<uint32_t> ev = {Msg(0x90, 1, 1), Delay(100), Msg(0x90, 2, 1), Delay(100), Msg(0x90, 3, 1), Delay(3), Msg(0x90, 4, 1)};
    {
        Rig r; // half time: ms = delay * 1 / 2 (the odd 3 truncates to 1)
        r.seq.SetTempo(1, 2);
        CHECK(r.Start(ev, false, 0));
        r.Poll(10000);
        CHECK(r.sink.events.size() == 4);
        CHECK(r.sink.events[1].scheduledMs == 50 && r.sink.events[2].scheduledMs == 100 && r.sink.events[3].scheduledMs == 101);
    }
    {
        Rig r; // double: num 2, den 1
        r.seq.SetTempo(2, 1);
        CHECK(r.Start(ev, false, 0));
        r.Poll(10000);
        CHECK(r.sink.events[1].scheduledMs == 200 && r.sink.events[2].scheduledMs == 400 && r.sink.events[3].scheduledMs == 406);
    }
    {
        Rig r; // LIVE change: the delay being waited for keeps its target, the ones read afterwards are scaled
        CHECK(r.Start(ev, false, 0));
        uint64_t wake = 0;
        CHECK(r.Poll(0, &wake) == St::Waiting && wake == 100);
        r.seq.SetTempo(1, 2);
        CHECK(r.Poll(50, &wake) == St::Waiting && wake == 100);  // unchanged
        CHECK(r.Poll(100, &wake) == St::Waiting && wake == 150); // next delay: 100 * 1/2
        CHECK(r.sink.events.size() == 2 && r.sink.events[1].scheduledMs == 100);
        r.seq.SetTempo(7, 7); // equal ratio = off
        CHECK(r.seq.TempoNum() == 0 && r.seq.TempoDen() == 0);
        r.Poll(150, &wake);
        CHECK(r.sink.events.size() == 3 && r.sink.events[2].scheduledMs == 150 && wake == 153); // delay 3 unscaled
        r.seq.SetTempo(5, 0); // den 0 = off as well
        CHECK(r.seq.TempoDen() == 0);
    }
    {
        Rig r; // 32-bit unsigned product like the original: 0x40000000 * 4 wraps to 0
        r.seq.SetTempo(4, 1);
        std::vector<uint32_t> e = {Msg(0x90, 1, 1), Delay(0x40000000u), Msg(0x90, 2, 1)};
        CHECK(r.Start(e, false, 0));
        CHECK(r.Poll(0) == St::Finished && r.sink.events.size() == 2 && r.sink.events[1].scheduledMs == 0);
    }
}

void TestLoopAndStop() {
    {
        Rig r;
        std::vector<uint32_t> ev = {Msg(0x90, 60, 100), Delay(100), Msg(0x80, 60, 0), Delay(50)};
        CHECK(r.Start(ev, true, 1000));
        uint64_t wake = 0;
        // 3 full passes of 150 ms plus the note-on of the fourth: seamless (the target keeps accumulating)
        CHECK(r.Poll(1000 + 3 * 150 + 20, &wake) == St::Waiting);
        CHECK(r.sink.events.size() == 7);
        for (size_t i = 0; i < 7; ++i) CHECK(r.sink.events[i].scheduledMs == 1000 + (i / 2) * 150 + (i % 2) * 100);
        CHECK(wake == 1000 + 3 * 150 + 100);
        r.seq.BreakLoops(); // finish the run in progress, no further pass
        CHECK(r.Poll(1000 + 3 * 150 + 100) == St::Waiting);
        CHECK(r.sink.events.size() == 8); // the note-off of the running pass
        CHECK(r.Poll(1000 + 4 * 150) == St::Finished);
        CHECK(r.sink.events.size() == 8);
        CHECK(r.sink.closes == 1);
    }
    {
        Rig r; // Stop(): silent, all-notes-off, nothing more is played
        std::vector<uint32_t> ev = {Msg(0x90, 1, 1), Delay(100), Msg(0x90, 2, 1)};
        CHECK(r.Start(ev, false, 0));
        r.Poll(0);
        r.seq.Stop();
        CHECK(!r.seq.Running() && r.sink.resets == 1 && r.sink.closes == 1);
        CHECK(r.Poll(500) == St::Idle && r.sink.events.size() == 1);
        r.seq.Stop(); // harmless twice
        CHECK(r.sink.resets == 1);
    }
    {
        Rig r; // a new Start replaces the running sequence (reset + close of the old one, open of the new one)
        std::vector<uint32_t> ev = {Msg(0x90, 1, 1), Delay(100), Msg(0x90, 2, 1)};
        CHECK(r.Start(ev, false, 0));
        r.Poll(0);
        CHECK(r.Start(ev, false, 10));
        CHECK(r.sink.opens == 2 && r.sink.closes == 1);
        r.Poll(10);
        CHECK(r.sink.events.size() == 2 && r.sink.events[1].scheduledMs == 10);
    }
    {
        Rig r; // degenerate streams
        std::vector<uint32_t> none;
        CHECK(r.Start(none, true, 0));
        CHECK(r.Poll(0) == St::Finished); // empty + loop would spin forever in the original
        std::vector<uint32_t> e = {Msg(0x90, 1, 1)};
        CHECK(r.Start(e, true, 0)); // loop without any delay: bounded work per call, still running
        uint64_t wake = 123;
        CHECK(r.Poll(0, &wake) == St::Waiting && wake == 0 && r.seq.Running());
        CHECK(r.sink.events.size() > 1000);
        r.seq.Stop();
        r.sink.failOpen = true;
        CHECK(!r.Start(e, false, 0) && !r.seq.Running()); // no device
    }
}

void TestHold() {
    Rig r;
    std::vector<uint32_t> ev = {Msg(0x90, 1, 1), Delay(100), Msg(0x90, 2, 1), Delay(100)};
    CHECK(r.Start(ev, false, 0));
    r.Poll(0);
    r.clock.now = 40;
    r.seq.Pause(40);
    CHECK(r.sink.resets == 1 && r.seq.Paused());
    uint64_t wake = 0;
    CHECK(r.Poll(5000, &wake) == St::Waiting && wake == UINT64_MAX && r.sink.events.size() == 1);
    r.clock.now = 240;
    r.seq.Resume(240); // 200 ms spent paused push the target from 100 to 300
    CHECK(r.Poll(299, &wake) == St::Waiting && wake == 300 && r.sink.events.size() == 1);
    CHECK(r.Poll(300, &wake) == St::Waiting && wake == 400 && r.sink.events.size() == 2 && r.sink.events[1].scheduledMs == 300);
    CHECK(r.Poll(400) == St::Finished);
}

// -- MidiPlayer with a manual clock -----------------------------------------------------------------------------------------
void TestPlayerManual() {
    auto sink = std::make_shared<MidiRecordingSink>();
    MidiManualClock clk;
    MidiPlayer p;
    sink->clock = &clk;
    p.SetSink(sink);
    p.SetManualClock(&clk);
    std::vector<uint32_t> ev = {Msg(0x90, 60, 100), Delay(100), Msg(0x91, 61, 100), Delay(100), Msg(0x80, 60, 0), Delay(100)};
    clk.now = 500;
    CHECK(p.Play(Bytes(ev), false, false));
    CHECK(p.IsPlaying() && sink->events.size() == 1);
    // Play already read the first delay (100 ms); mute and tempo apply to what follows
    p.SetChannelMute(1, true);
    p.SetTempo(1, 2);
    clk.now = 1000;
    p.Update();
    // ch1 note-on at 600 dropped; delays 50 + 50 -> note-off at 650, end at 700
    CHECK(sink->events.size() == 2 && sink->events[1].msg == Msg(0x80, 60, 0) && sink->events[1].scheduledMs == 650);
    CHECK(!p.IsPlaying());
    // loop + BreakLoops through the player
    sink->events.clear();
    clk.now = 0;
    std::vector<uint32_t> lp = {Msg(0x92, 1, 1), Delay(10)};
    p.SetTempo(1, 1);
    CHECK(p.Play(Bytes(lp), true));
    clk.now = 35;
    p.Update();
    CHECK(sink->events.size() == 4 && p.IsPlaying());
    p.BreakLoops();
    clk.now = 60;
    p.Update();
    CHECK(!p.IsPlaying());
    CHECK(sink->events.size() == 4 && sink->events.back().scheduledMs == 30);
    // stop
    CHECK(p.Play(Bytes(lp), true));
    p.Stop();
    CHECK(!p.IsPlaying());
    // hold keeps the position
    clk.now = 100;
    sink->events.clear();
    CHECK(p.Play(Bytes(lp), false));
    p.Hold(true);
    clk.now = 400;
    p.Update();
    p.Hold(false);
    clk.now = 405;
    p.Update();
    CHECK(p.IsPlaying());
    clk.now = 410;
    p.Update();
    CHECK(!p.IsPlaying());
}

// -- real clock, worker thread --------------------------------------------------------------------------------------------
void TestPlayerThreaded() {
    auto sink = std::make_shared<MidiRecordingSink>();
    MidiRealClock clk;
    sink->clock = &clk;
    MidiPlayer p;
    p.SetSink(sink);
    std::vector<uint32_t> ev = {Msg(0x90, 1, 1), Delay(100), Msg(0x91, 2, 1), Delay(100), Msg(0x92, 3, 1), Delay(100)};
    uint64_t t0 = clk.NowMs();
    CHECK(p.Play(Bytes(ev), false));
    p.SetChannelMute(1, true); // live, from the main thread while the worker sleeps
    for (int i = 0; i < 300 && p.IsPlaying(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(!p.IsPlaying());
    uint64_t t1 = clk.NowMs();
    CHECK(sink->events.size() == 2 && sink->events[0].msg == Msg(0x90, 1, 1) && sink->events[1].msg == Msg(0x92, 3, 1));
    // never early: an event is emitted at or after its scheduled time, and the run lasted at least the stream length
    for (auto& e : sink->events) CHECK(e.emittedMs >= e.scheduledMs);
    CHECK(sink->events[1].scheduledMs - sink->events[0].scheduledMs == 200);
    CHECK(t1 - t0 >= 300);
    CHECK(sink->closes == 1);
    // loop for a while, break it live, it must end by itself
    sink->events.clear();
    std::vector<uint32_t> lp = {Msg(0x93, 9, 9), Delay(5)};
    CHECK(p.Play(Bytes(lp), true));
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK(p.IsPlaying());
    p.BreakLoops();
    for (int i = 0; i < 100 && p.IsPlaying(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(!p.IsPlaying());
    CHECK(sink->events.size() >= 2);
    for (size_t i = 1; i < sink->events.size(); ++i) CHECK(sink->events[i].scheduledMs == sink->events[i - 1].scheduledMs + 5);
    // destroying the player while it plays must not hang
    CHECK(p.Play(Bytes(lp), true));
}

// -- CAudioLib on top of the sequencer ---------------------------------------------------------------------------------------
struct Note {
    std::vector<std::pair<u32, int>> v;
    bool Has(int n) const {
        for (auto& e : v) if (e.second == n) return true;
        return false;
    }
};
void OnSnd(void* d, u32 id, SNDNOTIFY n, PLAYSOUNDINFO*) { static_cast<Note*>(d)->v.push_back({id, int(n)}); }

::Sound MidiSound(const std::vector<uint32_t>& ev, u32 prio) {
    ::Sound s;
    s.kind = ::Sound::kMidi;
    s.param = prio;
    s.midi = Bytes(ev);
    return s;
}

void TestAudioLib() {
    auto sink = std::make_shared<MidiRecordingSink>();
    MidiManualClock clk;
    sink->clock = &clk;
    CAudioLib lib;
    lib.Midi().SetSink(sink);
    lib.Midi().SetManualClock(&clk);
    Note notes;
    lib.SetSoundProc(OnSnd, &notes);
    std::vector<uint32_t> ev = {Msg(0x90, 60, 100), Msg(0x91, 61, 100), Delay(100), Msg(0x80, 60, 0), Msg(0x90, 62, 100), Delay(100), Msg(0x91, 63, 100), Delay(100)};
    CAudioSnd* a = lib.SoundCreate(MidiSound(ev, 5));
    lib.SoundSetId(a, 77);
    clk.now = 1000;
    CHECK(lib.SoundPlay(a, nullptr) == 1);
    lib.Service();
    CHECK(notes.v.size() == 1 && notes.v[0] == std::make_pair(u32(77), int(SND_STARTED)));
    CHECK(sink->events.size() == 2);
    // live mute of channel 0 and live tempo change while it plays
    lib.MidiSetChannel(0, 1);
    lib.MidiSetTempo(1, 2);
    clk.now = 1100;
    lib.Service();
    CHECK(sink->events.size() == 3 && sink->events[2].msg == Msg(0x80, 60, 0)); // the ch0 note-on is dropped
    // the first delay (100) was read before the tempo change, the second is 50 -> 1150: ch1 note, then 50 -> 1200
    clk.now = 1149;
    lib.Service();
    CHECK(sink->events.size() == 3);
    clk.now = 1150;
    lib.Service();
    CHECK(sink->events.size() == 4 && sink->events[3].scheduledMs == 1150 && sink->events[3].msg == Msg(0x91, 63, 100));
    CHECK(notes.v.size() == 1); // not ended yet
    clk.now = 1200;
    lib.Service();
    CHECK(notes.v.size() == 2 && notes.v[1] == std::make_pair(u32(77), int(SND_ENDED)));
    // priority: a lower-priority MIDI does not replace a running one
    CAudioSnd* b = lib.SoundCreate(MidiSound(ev, 1));
    lib.SoundSetId(b, 78);
    lib.MidiSetTempo(3, 3);
    lib.MidiSetChannel(0, 0);
    sink->events.clear();
    notes.v.clear();
    clk.now = 2000;
    CHECK(lib.SoundPlay(a, nullptr) == 1);
    CHECK(lib.SoundPlay(b, nullptr) == 0);
    // Pause stops the player (silently); un-pause restarts the sound from the beginning (moses.dll 0x10011be9)
    clk.now = 2050;
    lib.Service();
    size_t before = sink->events.size();
    lib.Pause(1);
    CHECK(!lib.Midi().IsPlaying());
    clk.now = 5000;
    lib.Service();
    CHECK(sink->events.size() == before);
    CHECK(!notes.Has(SND_ENDED)); // a suspended sound is not "ended"
    lib.Pause(0);
    CHECK(lib.Midi().IsPlaying());
    CHECK(sink->events.size() == before + 2); // the two notes at time 0 again
    CHECK(sink->events[before].scheduledMs == 5000);
    // Activate(0)/(1) does the same; Pause and Activate must both be released
    lib.Activate(0);
    CHECK(!lib.Midi().IsPlaying());
    lib.Pause(1);
    lib.Activate(1);
    CHECK(!lib.Midi().IsPlaying());
    lib.Pause(0);
    CHECK(lib.Midi().IsPlaying());
    // stop everything
    lib.SoundStop(nullptr);
    CHECK(!lib.Midi().IsPlaying());
    lib.Service(); // delivers the queued ENDED of the stopped sound
    CHECK(notes.Has(SND_ENDED));
    // loop + MidiBreakLoops through the library
    std::vector<uint32_t> lp = {Msg(0x94, 1, 1), Delay(10)};
    CAudioSnd* c = lib.SoundCreate(MidiSound(lp, 9));
    lib.SoundSetId(c, 79);
    PLAYSOUNDINFO inf;
    inf.flags = PSF_LOOP;
    notes.v.clear();
    clk.now = 10000;
    CHECK(lib.SoundPlay(c, &inf) == 1);
    clk.now = 10100;
    lib.Service();
    CHECK(lib.Midi().IsPlaying());
    CHECK(!notes.Has(SND_ENDED));
    lib.MidiBreakLoops();
    clk.now = 10200;
    lib.Service();
    CHECK(notes.Has(SND_ENDED) && !lib.Midi().IsPlaying());
    lib.SoundDelete(a);
    lib.SoundDelete(b);
    lib.SoundDelete(c);
}

} // namespace

int RunMidiSeqTests() {
    std::printf("[midi sequencer]\n");
    TestOrderingAndTiming();
    TestCatchUp();
    TestMute();
    TestDrumRemap();
    TestTempo();
    TestLoopAndStop();
    TestHold();
    TestPlayerManual();
    TestPlayerThreaded();
    TestAudioLib();
    std::printf("midi sequencer: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail;
}
