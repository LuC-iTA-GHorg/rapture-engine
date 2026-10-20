// G1 - observable properties (score, lives, level, difficulty ...) and their list.
// Shoot.dll 0x10016e10-0x100170e0 (CProperty / CPropNumeric / CPropScore), 0x1001c2e0-0x1001c950 (CPropertyList).
// No Shoot.exe twin of these (the exe never links the player classes).
#pragma once

#include "aaron/RObject.h"

#include <climits>
#include <cstdint>

namespace rapture {

// ---------------------------------------------------------------------------------------------
// CProperty: abstract base (vtbl 0x100242bc, every slot is _purecall).  It IS an Aaron Broadcaster
// (Shoot.dll 0x10016e10: PtrArray::PtrArray(1) = the listener list); observers (CDisplay, CPlayer) subscribe with
// AddListener and receive OnAdvise(code=-1, value, this) on every Set.
class CProperty : public aaron::Broadcaster {
public:
    CProperty() = default;                                 // Shoot.dll 0x10016e10
    ~CProperty() override = default;                       // 0x10017020
    virtual int GetMin() = 0;                              // vtbl +0x0c
    virtual int GetMax() = 0;                              // +0x10
    virtual int GetDefault() = 0;                          // +0x14
    virtual int GetSnapshot() = 0;                         // +0x18
    virtual int Get() = 0;                                 // +0x1c
    virtual int Add(int delta) = 0;                        // +0x20  = Set(value + delta)
    virtual int Reset() = 0;                               // +0x24  = Set(default) then Snapshot()
    virtual int Set(int value) = 0;                        // +0x28  clamps, then Broadcast(-1, value, this)
    virtual void Configure(int min, int max, int def, int value) = 0; // +0x2c
    virtual int Snapshot() = 0;                            // +0x30  snapshot = value
};

// CPropNumeric (Shoot.dll 0x10016e7d, 0x2c bytes, vtbl 0x100242fc).
// Layout: +0x18 value, +0x1c min, +0x20 max, +0x24 default, +0x28 snapshot.
class CPropNumeric : public CProperty {
public:
    // Shoot.dll 0x10016e7d. NOTE the argument order: (min, max, default, value).
    CPropNumeric(int min, int max, int def, int value) : value_(value), min_(min), max_(max), def_(def) {}
    int GetMin() override { return min_; }                 // 0x10017040
    int GetMax() override { return max_; }                 // 0x10017060
    int GetDefault() override { return def_; }             // 0x10017080
    int GetSnapshot() override { return snap_; }           // 0x100170a0
    int Get() override { return value_; }                  // 0x100170c0
    int Add(int delta) override;                           // 0x10016ed8
    int Reset() override;                                  // 0x10016efa
    int Set(int value) override;                           // 0x10016f22
    void Configure(int min, int max, int def, int value) override; // 0x10016f86
    int Snapshot() override;                               // 0x100170e0

private:
    int value_ = 0, min_ = 0, max_ = 0, def_ = 0, snap_ = 0;
};

// CPropScore (Shoot.dll 0x10016e3e): a CPropNumeric with min 0, max 0x7fffffff, default 0, value 0
// (the original shares the CPropNumeric vtable 0x100242fc).
class CPropScore : public CPropNumeric {
public:
    CPropScore() : CPropNumeric(0, INT_MAX, 0, 0) {}       // 0x10016e3e
};

// ---------------------------------------------------------------------------------------------
// CPropertyList (Shoot.dll 0x1001c2e0, vtbl 0x100244dc, 24 slots): an owning array of CProperty*.
// Operations by index; Reset/Add/Set/Configure/Snapshot also accept -1 = apply to every property.
//
// NAMING FIX: names_shoot_dll_b.tsv / shoot_dll_b.md call 0x1001c768 "Set" and 0x1001c807 "AddValue".  The vtable of
// CPropNumeric shows the opposite: 0x1001c768 calls prop->vtbl+0x20 = Add(delta) and 0x1001c807 calls prop->vtbl+0x28
// = Set(value).  They are named here after what they do (AddValue / SetValue).
class CPropertyList : public aaron::PtrArray {
public:
    explicit CPropertyList(int capacity);                  // 0x1001c2e0 (the argument is PtrArray's grow size / capacity)
    ~CPropertyList() override;                             // 0x1001c318 (DeleteAll: owns the properties)

    int Add(CProperty* p);                                 // 0x1001c3ab  PtrArray::Insert(p, -1)
    int SetAt(int index, CProperty* p);                    // 0x1001c3c7  only if index < capacity (see README: doubt)
    bool Remove(CProperty* p);                             // 0x1001c699  IndexOf + RemoveAt; does NOT delete
    CProperty* RemoveAt(int index);                        // 0x1001c67f
    void RemoveAll();                                      // 0x1001c3f4  empties the list, does NOT delete the properties
    int GetDefault(int i);                                 // 0x1001c45a  (0 when i >= count)
    int GetMax(int i);                                     // 0x1001c4a9
    int GetMin(int i);                                     // 0x1001c4f8
    int GetSnapshot(int i);                                // 0x1001c596   (vtbl +0x3c: used by the Settings/GBP save!)
    int Get(int i);                                        // 0x1001c547
    CProperty* GetAt(int i);                               // 0x1001c5e5
    int IndexOf(CProperty* p);                             // 0x1001c626  -1 if absent
    void Reset(int i);                                     // 0x1001c6de
    int AddValue(int i, int delta);                        // 0x1001c768 (named "Set" in the notes: calls prop->Add)
    int SetValue(int i, int value);                        // 0x1001c807 (named "AddValue" in the notes: calls prop->Set)
    void Configure(int i, int min, int max, int def, int value); // 0x1001c8a6
    int Snapshot(int i);                                   // 0x1001c950

    int Size() const { return Count(); }

private:
    CProperty* at(int i) const { return (i >= 0 && i < Count()) ? static_cast<CProperty*>(At(i)) : nullptr; }
};

} // namespace rapture
