#include "platform/Midi.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

double MidiTempoScale = 1.0;

namespace {
void Vlq(std::vector<uint8_t>& o, uint32_t n) {
    uint8_t tmp[5];
    int k = 0;
    tmp[k++] = n & 0x7f;
    n >>= 7;
    while (n) {
        tmp[k++] = uint8_t(0x80 | (n & 0x7f));
        n >>= 7;
    }
    while (k) o.push_back(tmp[--k]);
}
void Be32(std::vector<uint8_t>& o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(uint8_t(v >> s));
}
} // namespace

// Same output as midi_to_smf() in engine/tools/rrs_extract.py (when MidiTempoScale == 1).
std::vector<uint8_t> CompactMidiToSmf(const uint8_t* data, size_t size) {
    std::vector<uint8_t> trk;
    // tempo meta: 1,000,000 us per quarter note with division 1000 -> 1 tick = 1 ms
    const uint8_t tempo[] = {0x00, 0xff, 0x51, 0x03, 0x0f, 0x42, 0x40};
    trk.insert(trk.end(), tempo, tempo + sizeof(tempo));
    uint64_t delta = 0;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t dw = uint32_t(data[i]) | uint32_t(data[i + 1]) << 8 | uint32_t(data[i + 2]) << 16 |
                      uint32_t(data[i + 3]) << 24;
        if (dw & 0x80000000u) {
            delta += dw & 0x7fffffffu;
            continue;
        }
        uint8_t st = dw & 0xff;
        if (st < 0x80) continue;
        int len = ((st & 0xf0) == 0xc0 || (st & 0xf0) == 0xd0) ? 2 : 3;
        Vlq(trk, uint32_t(double(delta) * MidiTempoScale));
        trk.push_back(st);
        trk.push_back(uint8_t(dw >> 8));
        if (len == 3) trk.push_back(uint8_t(dw >> 16));
        delta = 0;
    }
    Vlq(trk, uint32_t(double(delta) * MidiTempoScale));
    trk.push_back(0xff);
    trk.push_back(0x2f);
    trk.push_back(0x00);

    std::vector<uint8_t> smf = {'M', 'T', 'h', 'd'};
    Be32(smf, 6);
    const uint8_t hdr[] = {0, 0, 0, 1, 0x03, 0xe8}; // format 0, 1 track, division 1000
    smf.insert(smf.end(), hdr, hdr + sizeof(hdr));
    smf.push_back('M');
    smf.push_back('T');
    smf.push_back('r');
    smf.push_back('k');
    Be32(smf, uint32_t(trk.size()));
    smf.insert(smf.end(), trk.begin(), trk.end());
    return smf;
}

std::vector<uint8_t> MidiPrepareCompact(const std::vector<uint8_t>& in, bool remapDrums, uint32_t muteMask, uint32_t num, uint32_t den) {
    if (!remapDrums && muteMask == 0 && num == den) return in;
    std::vector<uint8_t> out;
    out.reserve(in.size());
    for (size_t i = 0; i + 4 <= in.size(); i += 4) {
        uint32_t v = uint32_t(in[i]) | uint32_t(in[i + 1]) << 8 | uint32_t(in[i + 2]) << 16 | uint32_t(in[i + 3]) << 24;
        if (v & 0x80000000u) {
            if (num != den && den != 0) v = 0x80000000u | uint32_t((uint64_t(v & 0x7fffffffu) * num / den) & 0x7fffffffu);
        } else {
            if (remapDrums) v = MidiRemapDrumsMsg(v);
            if ((v & 0xf0) == 0x90 && (muteMask >> (v & 0xf) & 1)) continue;
        }
        for (int k = 0; k < 4; ++k) out.push_back(uint8_t(v >> (8 * k)));
    }
    return out;
}
// ---------------------------------------------------------------------------------------------------------------------
// MidiPlayer: driver of the MidiSequencer (the original CMidiPlayer, moses.dll 0x10012de3 ... 0x10013120)
struct MidiPlayer::Impl {
    std::mutex m;
    std::condition_variable cv;
    std::thread th;
    bool quit = false;
    std::shared_ptr<MidiSink> sink;
    MidiRealClock realClock;
    MidiClock* clock = &realClock;
    bool manual = false;
};

MidiPlayer::MidiPlayer() : d_(new Impl) {}

MidiPlayer::~MidiPlayer() {
    {
        std::lock_guard<std::mutex> lk(d_->m);
        d_->quit = true;
        d_->cv.notify_all();
    }
    if (d_->th.joinable()) d_->th.join();
    seq_.Stop();
    delete d_;
}

void MidiPlayer::SetSink(std::shared_ptr<MidiSink> sink) {
    Stop();
    std::lock_guard<std::mutex> lk(d_->m);
    d_->sink = std::move(sink);
    seq_.SetSink(d_->sink.get());
}

void MidiPlayer::SetManualClock(MidiClock* clock) {
    Stop();
    {
        std::lock_guard<std::mutex> lk(d_->m);
        d_->quit = true;
        d_->cv.notify_all();
    }
    if (d_->th.joinable()) d_->th.join();
    std::lock_guard<std::mutex> lk(d_->m);
    d_->quit = false;
    d_->manual = clock != nullptr;
    d_->clock = clock ? clock : &d_->realClock;
}

uint64_t MidiPlayer::NowMs() { return d_->clock->NowMs(); }

bool MidiPlayer::Play(const std::vector<uint8_t>& compact, bool loop, bool remapDrums) {
    std::vector<uint32_t> ev(compact.size() / 4);
    for (size_t i = 0; i < ev.size(); ++i)
        ev[i] = uint32_t(compact[i * 4]) | uint32_t(compact[i * 4 + 1]) << 8 | uint32_t(compact[i * 4 + 2]) << 16 |
                uint32_t(compact[i * 4 + 3]) << 24;
    std::unique_lock<std::mutex> lk(d_->m);
    if (!d_->sink) d_->sink = std::shared_ptr<MidiSink>(CreateDefaultMidiSink().release());
    seq_.SetSink(d_->sink.get());
    if (!seq_.Start(ev.data(), ev.size(), loop, d_->clock->NowMs(), remapDrums)) return false;
    if (d_->manual) {
        uint64_t wake;
        seq_.Poll(d_->clock->NowMs(), &wake); // events at time 0 go out at once, like the thread does
        return true;
    }
    if (!d_->th.joinable()) {
        Impl* d = d_;
        MidiSequencer* seq = &seq_;
        d->th = std::thread([d, seq] {
            std::unique_lock<std::mutex> l(d->m);
            while (!d->quit) {
                uint64_t before = d->clock->NowMs(), wake = 0;
                auto st = seq->Poll(before, &wake);
                if (st == MidiSequencer::Status::Waiting && wake != UINT64_MAX) {
                    uint64_t ms = wake > before ? wake - before : 1; // wake == before: event budget used up, yield
                    d->cv.wait_for(l, std::chrono::milliseconds(ms));
                } else if (st != MidiSequencer::Status::Waiting || wake == UINT64_MAX) {
                    d->cv.wait(l); // idle, finished or held: sleep until a control call wakes us
                }
            }
        });
    }
    d_->cv.notify_all();
    return true;
}

void MidiPlayer::Stop() {
    std::lock_guard<std::mutex> lk(d_->m);
    seq_.Stop();
    d_->cv.notify_all();
}

bool MidiPlayer::IsPlaying() {
    std::lock_guard<std::mutex> lk(d_->m);
    return seq_.Running();
}

void MidiPlayer::Update() {
    if (!d_->manual) return;
    std::lock_guard<std::mutex> lk(d_->m);
    uint64_t wake;
    seq_.Poll(d_->clock->NowMs(), &wake);
}

void MidiPlayer::BreakLoops() {
    std::lock_guard<std::mutex> lk(d_->m);
    seq_.BreakLoops();
}

void MidiPlayer::SetChannelMute(uint32_t channel, bool mute) {
    std::lock_guard<std::mutex> lk(d_->m);
    seq_.SetChannelMute(channel, mute);
}

void MidiPlayer::SetTempo(uint32_t num, uint32_t den) {
    std::lock_guard<std::mutex> lk(d_->m);
    seq_.SetTempo(num, den);
    d_->cv.notify_all();
}

void MidiPlayer::Hold(bool on) {
    std::lock_guard<std::mutex> lk(d_->m);
    if (on) seq_.Pause(d_->clock->NowMs());
    else seq_.Resume(d_->clock->NowMs());
    d_->cv.notify_all();
}
