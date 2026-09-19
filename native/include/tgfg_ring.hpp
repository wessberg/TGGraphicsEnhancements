// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <array>
#include <cstdint>
namespace tgfg {
// Caller serializes access. Occupied slots are NEVER silently overwritten.
// NVIDIA frame-token lifetime is bounded; use at most six outstanding tokens.
template<class T, size_t N=6> struct FrameRing {
    struct Slot { uint32_t id=0; bool occupied=false; T value{}; };
    std::array<Slot,N> slots{};
    T* find(uint32_t id) { auto& s=slots[id%N]; return s.occupied && s.id==id ? &s.value : nullptr; }
    T* acquire(uint32_t id) {
        auto& s=slots[id%N];
        if(s.occupied) return s.id==id ? &s.value : nullptr;
        s.id=id; s.occupied=true; s.value=T{}; return &s.value;
    }
    void release(uint32_t id) { auto& s=slots[id%N]; if(s.occupied && s.id==id) { s.occupied=false; s.value=T{}; } }
};
} // namespace tgfg
