// G1 - HUD number display: Shoot.dll 0x1001a500-0x1001bc85 (CDisplay), 0x1001d940-0x1001d9e5 (CHSDisplay).
// Shoot.exe twin: CDisplay 0x40ba1f (shoot_exe.md sec. 6).
//
// A CDisplay is a Node whose children are up to 20 "digit" AnimSprites (frames of a group resource: 0-9 digits, 10 ':',
// 11 '.', 12 ','); it is a Subscriber, so it can observe a CProperty (OnAdvise code -1 -> SetValue).
//
// flags (DisplayStyle::flags):
//   1 animated counting (CountTo)      2 timer mode (hh:mm:ss.mmm)         8 spacing by groups
//   0x10 right aligned / 0x20 centred  0x40 / 0x80 vertical alignment (0x40 top, 0x80 bottom, none = middle)
//   0x100 left-justified digits (numbers) / "with hours" (timer)   0x200 minutes  0x400 seconds  0x800 milliseconds
//   0x1000 / 0x2000 one extra leading glyph   0x4000 thousands separator   0x8000 zero padding
#pragma once

#include "aaron/RObject.h"
#include "aaron/Sprites.h"
#include "aaron/Widget.h"
#include "arcade/ArcadeTypes.h"
#include "arcade/Events.h"

#include <cstdint>

namespace rapture {

class CDisplay : public aaron::Subscriber, public aaron::Node {
public:
    static constexpr int kMaxSprites = 20;

    // Shoot.dll 0x1001a5cf (animated = false) / 0x1001a72f (animated = true: the counting messages are initialised).
    // `parent` = the widget the digits hang from (the scene's root widget or a container node).
    CDisplay(aaron::WidgetParent parent, u32 resId, const DisplayStyle& style, bool animated);
    ~CDisplay() override;                                       // 0x1001a936

    // Subscriber: observer of a CProperty.  0x1001bbaa
    void OnAdvise(int code, intptr_t value, aaron::Broadcaster* src) override;

    void SetPosition(int x, int y, int relative);               // 0x1001a9ed
    void CountTo(int target, int p2);                           // 0x1001ab5f
    void DisplayNumber(int n, int p2);                          // 0x1001acb0
    void SetCallback(moses::QMSG_CALLBACK fn, uintptr_t ctx, uintptr_t arg); // 0x1001b426: end-of-counting callback
    void ResetCallback();                                       // 0x1001b457
    void SetValue(u32 v, int p2);                               // 0x1001b488
    void DisplayTime(u32 ms);                                   // 0x1001b4e5
    int Value() const { return value_; }                        // +0xdc
    int Target() const { return target_; }                      // +0xe4
    int NumSprites() const { return nSprites_; }                // +0xf0
    u32 Flags() const { return flags_; }
    aaron::AnimSprite* Digit(int i) const { return (i >= 0 && i < nSprites_) ? digits_[i] : nullptr; }

    // QM_CALL of the counting queue (0x1001a500)
    static void CountStep(moses::CAnimObj* obj, uintptr_t ctx, uintptr_t unused);

protected:
    void InitCounter(u32 step, int delayMs);                    // 0x1001afc8
    void CreateDigits();                                        // 0x1001b108
    void CreateTimeDigits();                                    // 0x1001b831
    void SetDigitPosition(aaron::AnimSprite* s, int x, int y, int rel);

    aaron::AnimSprite* digits_[kMaxSprites] = {};               // +0x50
    CQueueMsgArray* queue_ = nullptr;                           // +0xa0
    moses::QUEUEMSG msgLoop_;                                   // +0xa4 {QM_LOOP, count at +0xa8}
    moses::QUEUEMSG msgLoopEnd_;                                // +0xac {QM_LOOPEND}
    moses::QUEUEMSG msgCount_;                                  // +0xb0 {QM_CALL CountStep, this}
    moses::QUEUEMSG msgEnd_;                                    // +0xc0 {QM_CALL user callback}
    moses::QUEUEMSG msgDelay_;                                  // +0xd0 {QM_DELAY ms}
    u32 flags_ = 0;                                             // +0xd8
    int value_ = 0;                                             // +0xdc
    int value2_ = 0;                                            // +0xe0
    int target_ = 0;                                            // +0xe4
    int target2_ = 0;                                           // +0xe8
    u32 resId_ = 0;                                             // +0xec
    int nSprites_ = 0;                                          // +0xf0
    int x_ = 0, y_ = 0;                                         // +0xf4 +0xf8
    int dx_ = 0, dy_ = 0;                                       // +0xfc +0x100
    int z_ = 0;                                                 // +0x104
    int step_ = 0;                                              // +0x108 (>= 1 in counting mode)
    int maxDigits_ = 0;                                         // +0x10c
};

// CHSDisplay (0x1001d940 / 0x1001d99f): shows the best score - only ever grows.
class CHSDisplay : public CDisplay {
public:
    CHSDisplay(aaron::WidgetParent parent, u32 resId, const DisplayStyle& style, bool animated)
        : CDisplay(parent, resId, style, animated) {}
    void OnAdvise(int code, intptr_t value, aaron::Broadcaster* src) override; // 0x1001d9e5
};

} // namespace rapture
