// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace tgnr {
// Owner, 2026-10-09 (NR-30/31/32): automatic selection, no per-file community allowlist.
// Direct ABI compatibility is checked inside the adapter; a hash records identity, not support.
enum class RuntimeKind { Nvidia, Community };
inline RuntimeKind runtime_kind(bool nvidiaSigned) {
    return nvidiaSigned ? RuntimeKind::Nvidia:RuntimeKind::Community;
}
inline bool community_api_supported(uint32_t apiVersion) { return apiVersion==0x13u; }
}
