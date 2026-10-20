// G1 - properties.  See Property.h.
#include "arcade/Property.h"

namespace rapture {

// ---------------------------------------------------------------------------------------------
// CPropNumeric
// Shoot.dll 0x10016ed8: Set(value + delta).  (wrap-around add as the 32-bit original)
int CPropNumeric::Add(int delta) {
    return Set(int(unsigned(value_) + unsigned(delta)));
}

// Shoot.dll 0x10016efa: Set(default); Snapshot().
int CPropNumeric::Reset() {
    Set(def_);
    return Snapshot();
}

// Shoot.dll 0x10016f22.  Transcribed literally:  if (min < v) { if (v < max) value = v; else value = max; } else value = min;
// QUIRK: when max < min (Shoot.dll's lives: min 0, max -1) every v > min ends up as `max`, every other v as `min`.
// Always notifies (even if the value did not change): Broadcast(-1, value, this).
int CPropNumeric::Set(int v) {
    if (min_ < v) {
        if (v < max_) value_ = v;
        else value_ = max_;
    } else {
        value_ = min_;
    }
    Broadcast(-1, value_, this);
    return value_;
}

// Shoot.dll 0x10016f86: sets min/max/default and then Set(value) (so observers are notified).
void CPropNumeric::Configure(int min, int max, int def, int value) {
    min_ = min;
    max_ = max;
    def_ = def;
    Set(value);
}

// Shoot.dll 0x100170e0
int CPropNumeric::Snapshot() {
    snap_ = value_;
    return snap_;
}

// ---------------------------------------------------------------------------------------------
// CPropertyList
CPropertyList::CPropertyList(int capacity) : aaron::PtrArray(capacity) {} // 0x1001c2e0

CPropertyList::~CPropertyList() { DeleteAll(); } // 0x1001c318 (three nested DeleteAll in the original: base chain)

int CPropertyList::Add(CProperty* p) { return Insert(p, -1); } // 0x1001c3ab

// 0x1001c3c7: `if (index < capacity) return PtrArray::Insert(p, index); else return 0`.
int CPropertyList::SetAt(int index, CProperty* p) {
    if (index < Capacity()) return Insert(p, index);
    return 0;
}

CProperty* CPropertyList::RemoveAt(int index) { return static_cast<CProperty*>(PtrArray::RemoveAt(index)); } // 0x1001c67f

bool CPropertyList::Remove(CProperty* p) { // 0x1001c699
    int i = IndexOf(p);
    if (i == -1) return false;
    return RemoveAt(i) != nullptr;
}

// 0x1001c3f4.  The original loops calling the virtual RemoveAt(i) with an increasing i (which skips elements) and
// then forces count = 0: the observable result is "empty list, properties not deleted".
void CPropertyList::RemoveAll() { Clear(); }

int CPropertyList::GetDefault(int i) { CProperty* p = at(i); return p ? p->GetDefault() : 0; }   // 0x1001c45a
int CPropertyList::GetMax(int i) { CProperty* p = at(i); return p ? p->GetMax() : 0; }           // 0x1001c4a9
int CPropertyList::GetMin(int i) { CProperty* p = at(i); return p ? p->GetMin() : 0; }           // 0x1001c4f8
int CPropertyList::Get(int i) { CProperty* p = at(i); return p ? p->Get() : 0; }                 // 0x1001c547
int CPropertyList::GetSnapshot(int i) { CProperty* p = at(i); return p ? p->GetSnapshot() : 0; } // 0x1001c596
CProperty* CPropertyList::GetAt(int i) { return at(i); }                                          // 0x1001c5e5

int CPropertyList::IndexOf(CProperty* p) { // 0x1001c626
    for (int i = 0; i < Count(); ++i)
        if (At(i) == p) return i;
    return -1;
}

// 0x1001c6de: -1 = every property
void CPropertyList::Reset(int i) {
    int n = Count();
    if (i == -1) {
        for (int k = 0; k < n; ++k) static_cast<CProperty*>(At(k))->Reset();
    } else if (i < n && i > -1) {
        static_cast<CProperty*>(At(i))->Reset();
    }
}

// 0x1001c768 (prop->vtbl+0x20 = Add)
int CPropertyList::AddValue(int i, int delta) {
    int n = Count();
    if (i == -1) {
        for (int k = 0; k < n; ++k) static_cast<CProperty*>(At(k))->Add(delta);
    } else if (i < n && i > -1) {
        return static_cast<CProperty*>(At(i))->Add(delta);
    }
    return 0;
}

// 0x1001c807 (prop->vtbl+0x28 = Set)
int CPropertyList::SetValue(int i, int value) {
    int n = Count();
    if (i == -1) {
        for (int k = 0; k < n; ++k) static_cast<CProperty*>(At(k))->Set(value);
    } else if (i < n && i > -1) {
        return static_cast<CProperty*>(At(i))->Set(value);
    }
    return 0;
}

// 0x1001c8a6
void CPropertyList::Configure(int i, int min, int max, int def, int value) {
    int n = Count();
    if (i == -1) {
        for (int k = 0; k < n; ++k) static_cast<CProperty*>(At(k))->Configure(min, max, def, value);
    } else if (i < n && i > -1) {
        static_cast<CProperty*>(At(i))->Configure(min, max, def, value);
    }
}

// 0x1001c950
int CPropertyList::Snapshot(int i) {
    int n = Count();
    if (i == -1) {
        for (int k = 0; k < n; ++k) static_cast<CProperty*>(At(k))->Snapshot();
    } else if (i < n && i > -1) {
        return static_cast<CProperty*>(At(i))->Snapshot();
    }
    return 0;
}

} // namespace rapture
