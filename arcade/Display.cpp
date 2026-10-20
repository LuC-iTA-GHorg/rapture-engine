// G1 - HUD number display.  See Display.h.  Shoot.dll addresses in comments.
#include "arcade/Display.h"

namespace rapture {

using aaron::AnimSprite;
using moses::QUEUEMSG;

static moses::CDrawDib* DibOf(AnimSprite* s) { return static_cast<moses::CDrawDib*>(s); }

// 0x1001a5cf / 0x1001a72f.  The digits are created hidden (style 1); unless the style says "hidden" the display starts at 0.
CDisplay::CDisplay(aaron::WidgetParent parent, u32 resId, const DisplayStyle& st, bool animated)
    : aaron::Node(parent, 0, 0) {
    resId_ = resId;
    dx_ = st.dx;
    dy_ = st.dy;
    x_ = st.x;
    y_ = st.y;
    z_ = st.z;
    flags_ = st.flags;
    maxDigits_ = st.maxDigits;
    if (animated) { // CDisplayAnimated: pre-fill the counting messages (the plain constructor leaves them zeroed)
        msgLoop_ = moses::qm::Loop(1);           // +0xa4 = 1, +0xa8 = 1
        msgLoopEnd_ = moses::qm::LoopEnd();      // +0xac = 3
        msgDelay_ = moses::qm::Delay(0);         // +0xd0 = 2, +0xd4 = 0
        msgCount_ = moses::qm::Call(&CDisplay::CountStep, reinterpret_cast<uintptr_t>(this), 0); // +0xb0
        ResetCallback();                          // +0xc0 = {5, nop, this, 0}
    }
    if ((flags_ & 2) == 0) {
        if ((flags_ & 1) == 0) {
            CreateDigits();
            if ((st.hidden & 1) == 0) DisplayNumber(0, 0);
        } else {
            InitCounter(u32(st.step), st.p13);
            if ((st.hidden & 1) == 0) DisplayNumber(0, 0);
        }
    } else {
        CreateTimeDigits();
    }
}

CDisplay::~CDisplay() { // 0x1001a936 (the digits are children of the node: deleted by ~TreeNode)
    delete queue_;
}

static void NopCallback(moses::CAnimObj*, uintptr_t, uintptr_t) {} // FUN_1001a5bf

void CDisplay::ResetCallback() { msgEnd_ = moses::qm::Call(&NopCallback, reinterpret_cast<uintptr_t>(this), 0); } // 0x1001b457

// 0x1001b426: {5, fn, ctx, arg} - runs when the counting queue has finished
void CDisplay::SetCallback(moses::QMSG_CALLBACK fn, uintptr_t ctx, uintptr_t arg) { msgEnd_ = moses::qm::Call(fn, ctx, arg); }

void CDisplay::OnAdvise(int code, intptr_t value, aaron::Broadcaster*) { // 0x1001bbaa
    if (code == -1) SetValue(u32(value), 0);
}

void CHSDisplay::OnAdvise(int code, intptr_t value, aaron::Broadcaster*) { // 0x1001d9e5
    // QUIRK: unsigned comparison, as the original (`*(uint*)(this+0xdc) < param_2`)
    if (code == -1 && u32(value_) < u32(value)) SetValue(u32(value), 0);
}

void CDisplay::SetDigitPosition(AnimSprite* s, int x, int y, int rel) { DibOf(s)->SetPosition(x, y, rel); }

// 0x1001a9ed.  relative == 0: absolute anchor with the alignment flags applied; otherwise every digit is moved by (x, y).
void CDisplay::SetPosition(int x, int y, int relative) {
    if (relative == 0) {
        if ((flags_ & 0x10) == 0) {
            if ((flags_ & 0x20) != 0) x = x + -(nSprites_ * dx_) / 2;
        } else {
            x = x - nSprites_ * dx_;
        }
        if ((flags_ & 0x40) == 0) {
            if ((flags_ & 0x80) == 0) y = y + dy_ / 2;
        } else {
            y = y + -dy_ / 2;
        }
        for (int i = 0; i < nSprites_; ++i) SetDigitPosition(digits_[i], x + i * dx_, y, 0);
    } else {
        for (int i = 0; i < nSprites_; ++i) SetDigitPosition(digits_[i], x, y, relative);
    }
}

// 0x1001a500: one counting step (QM_CALL of the queue).  QUIRK: unsigned comparisons (a negative step counts as huge).
void CDisplay::CountStep(moses::CAnimObj*, uintptr_t ctx, uintptr_t) {
    CDisplay* d = reinterpret_cast<CDisplay*>(ctx);
    if (!d) return;
    bool reached;
    if (d->step_ < 0) reached = u32(d->value_ + d->step_) <= u32(d->target_);
    else reached = u32(d->target_) <= u32(d->value_ + d->step_);
    int delta = reached ? d->target_ - d->value_ : d->step_;
    d->value_ += delta;
    d->DisplayNumber(d->value_, 0);
}

// 0x1001ab5f: start counting to `target`: LOOP(ceil(|cur-target| / step)) { DELAY; CountStep } then the end callback,
// hosted by the least significant digit sprite.  QUIRK: |diff| < step with a remainder gives 2 loops (1 + 1).
void CDisplay::CountTo(int target, int p2) {
    target_ = target;
    target2_ = p2;
    int diff = value_ - target_;
    int ad = diff < 0 ? -diff : diff; // (x ^ s) - s
    int loops = (ad / step_ < 2) ? 1 : ad / step_;
    msgLoop_.a = loops;
    if (ad % step_ != 0) msgLoop_.a += 1;
    int host = (flags_ & 0x100) == 0 ? nSprites_ - 1 : 0;
    if (queue_ != nullptr && host >= 0 && host < nSprites_ && digits_[host] != nullptr) {
        moses::CAnimObj* a = static_cast<moses::CAnimDib*>(digits_[host]);
        a->GetAnimView()->QStart(a, queue_->Data(), queue_->Count());
    }
}

// 0x1001acb0
void CDisplay::DisplayNumber(int n, int p2) {
    int idx = nSprites_ - 1;   // local_18
    int processed = 0;         // local_10
    value_ = n;
    value2_ = p2;
    int nd = 1;                // local_8
    for (int t = n; 9 < t; t = t / 10) nd += 1;
    if ((flags_ & 0x4000) != 0) nd = nd - 1 + nd / 3;
    if (nd > maxDigits_) return; // original: DebugMsg("CDisplay::DisplayNumber WARNING: %d is greater than %d digits!") removed
    int t = n;
    int cnt = nd; // local_1c
    if ((flags_ & 0x100) == 0) {
        while (idx > -1) {
            int cur = idx;
            bool show = !(((flags_ & 0x8000) == 0) && t == 0 && idx != nSprites_ - 1);
            if (show) DibOf(digits_[idx])->Animate(u32(t % 10), nullptr);
            else digits_[idx]->Show(false);
            idx -= 1;
            t = t / 10;
            processed += 1;
            if (processed % 3 == 0 && (flags_ & 0x4000) != 0) {
                bool s2 = !(idx < 1 || (((flags_ & 0x8000) == 0 && t == 0) && idx != nSprites_ - 1));
                int commaPos = idx;
                idx = cur - 2;
                if (commaPos >= 0 && commaPos < nSprites_) {
                    if (s2) DibOf(digits_[commaPos])->Animate(0xcu, nullptr); // ','
                    else digits_[commaPos]->Show(false);
                }
            }
        }
    } else {
        // left-justified numbers.  QUIRK: with the thousands flag every cell of this branch shows ',' (0xc).
        while (cnt = cnt - 1, cnt > -1) {
            if ((flags_ & 0x4000) == 0) {
                DibOf(digits_[cnt])->Animate(u32(t % 10), nullptr);
                t = t / 10;
            } else {
                DibOf(digits_[cnt])->Animate(0xcu, nullptr);
            }
        }
        for (cnt = nd; cnt < nSprites_; ++cnt) digits_[cnt]->Show(false);
    }
}

// 0x1001afc8: counting queue = LOOP(n) { DELAY(ms); CALL CountStep } LOOPEND; CALL endCallback
void CDisplay::InitCounter(u32 step, int delayMs) {
    queue_ = new CQueueMsgArray(5, nullptr, 0);
    step_ = step < 2 ? 1 : int(step);
    msgDelay_.a = delayMs; // +0xd4
    queue_->Insert(&msgLoop_);
    queue_->Insert(&msgDelay_);
    queue_->Insert(&msgCount_);
    queue_->Insert(&msgLoopEnd_);
    queue_->Insert(&msgEnd_);
    CreateDigits();
}

// 0x1001b108
void CDisplay::CreateDigits() {
    bool grouped = false; // bVar1
    int x = x_, y = y_;
    nSprites_ = maxDigits_;
    if ((flags_ & 0x4000) != 0) nSprites_ += maxDigits_ / 3;
    if ((flags_ & 0x1000) != 0 || (flags_ & 0x2000) != 0) nSprites_ += 1;
    if (nSprites_ > kMaxSprites) nSprites_ = kMaxSprites; // (the original array holds 20)
    if ((flags_ & 0x10) == 0) {
        if ((flags_ & 0x20) != 0) x = x + -(nSprites_ * dx_) / 2;
    } else {
        x = x - nSprites_ * dx_;
    }
    if ((flags_ & 0x40) == 0) {
        if ((flags_ & 0x80) == 0) y = y + dy_ / 2;
    } else {
        y = y + -dy_ / 2;
    }
    int remaining = nSprites_; // local_14
    int groupBase = (flags_ & 0x1000) != 0 ? 2 : ((flags_ & 0x2000) != 0 ? 3 : 0); // local_18
    for (int i = 0; i < nSprites_; ++i) {
        AnimSprite* s = new AnimSprite(aaron::WidgetParent(static_cast<aaron::Widget*>(this)), resId_, x, y, z_, 0, 1);
        digits_[i] = static_cast<AnimSprite*>(aaron::Widget::InitOrDelete(s));
        remaining -= 1;
        if ((flags_ & 8) == 0) {
            x += dx_;
        } else {
            bool group = false;
            if (remaining == groupBase) group = true;
            else if ((flags_ & 0x4000) != 0 && (remaining - groupBase) % 4 == 0) group = true;
            else if (grouped) group = true;
            if (group) {
                x += 2 + dx_ / 2;
                grouped = !grouped;
            } else {
                x += dx_;
            }
        }
    }
}

// 0x1001b831: timer mode.  Layout from the flags: [hh][:][mm][:][ss][.][mmm]; some flags imply others.
void CDisplay::CreateTimeDigits() {
    if ((flags_ & 0x100) == 0) {
        if ((flags_ & 0x200) == 0) {
            if ((flags_ & 0x400) == 0) {
                if ((flags_ & 0x800) != 0) nSprites_ = 3;
            } else if ((flags_ & 0x800) == 0) {
                nSprites_ = 2;
            } else {
                nSprites_ = 6;
            }
        } else if ((flags_ & 0x800) == 0) {
            if ((flags_ & 0x400) == 0) nSprites_ = 2;
            else nSprites_ = 5;
        } else {
            nSprites_ = 9;
            flags_ |= 0x400;
        }
    } else if ((flags_ & 0x800) == 0) {
        if ((flags_ & 0x400) == 0) {
            if ((flags_ & 0x200) == 0) nSprites_ = 2;
            else nSprites_ = 5;
        } else {
            nSprites_ = 8;
            flags_ |= 0x200;
        }
    } else {
        nSprites_ = 12;
        flags_ |= 0x400;
        flags_ |= 0x200;
    }
    int x = x_, y = y_;
    if ((flags_ & 0x10) == 0) {
        if ((flags_ & 0x20) != 0) x = x + -(nSprites_ * dx_) / 2;
    } else {
        x = x - nSprites_ * dx_;
    }
    if ((flags_ & 0x40) == 0) {
        if ((flags_ & 0x80) == 0) y = y + dy_ / 2;
    } else {
        y = y + -dy_ / 2;
    }
    for (int i = 0; i < nSprites_ && i < kMaxSprites; ++i) {
        AnimSprite* s = new AnimSprite(aaron::WidgetParent(static_cast<aaron::Widget*>(this)), resId_, x + i * dx_, y, z_, 0, 1);
        digits_[i] = static_cast<AnimSprite*>(aaron::Widget::InitOrDelete(s));
    }
}

// 0x1001b488
void CDisplay::SetValue(u32 v, int p2) {
    if ((flags_ & 2) == 0) {
        if ((flags_ & 1) == 0) DisplayNumber(int(v), p2);
        else CountTo(int(v), p2);
    } else {
        DisplayTime(v);
    }
}

// 0x1001b4e5: ms -> hh:mm:ss.mmm in the cells selected by the flags (':' = frame 10, '.' = frame 11)
void CDisplay::DisplayTime(u32 ms) {
    u32 millis = ms % 1000;
    u32 sec = u32((uint64_t(ms) % 60000) / 1000);
    u32 min = u32((uint64_t(ms) % 3600000) / 60000);
    int k = 0;
    auto put = [this](int cell, u32 frame) {
        if (cell >= 0 && cell < nSprites_ && digits_[cell]) DibOf(digits_[cell])->Animate(frame, nullptr);
    };
    if ((flags_ & 0x100) != 0) {
        put(0, (ms / 3600000) / 10);
        put(1, (ms / 3600000) % 10);
        k = 2;
        if ((flags_ & 0x200) != 0) {
            k = 3;
            put(2, 10);
        }
    }
    int base = k;
    if ((flags_ & 0x200) != 0) {
        put(k, min / 10);
        put(k + 1, min % 10);
        base = k + 2;
        if ((flags_ & 0x400) != 0) {
            put(k + 2, 10);
            base = k + 3;
        }
    }
    k = base;
    if ((flags_ & 0x400) != 0) {
        put(k, sec / 10);
        put(k + 1, sec % 10);
        base = k + 2;
        if ((flags_ & 0x800) != 0) {
            put(k + 2, 0xb);
            base = k + 3;
        }
    }
    k = base;
    if ((flags_ & 0x800) != 0) {
        put(k, millis / 100);
        put(k + 1, (millis % 100) / 10);
        put(k + 2, millis % 10);
    }
}

} // namespace rapture
