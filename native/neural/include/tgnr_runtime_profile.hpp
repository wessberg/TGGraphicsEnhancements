// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace tgnr {
// NR-30/31/32: Select compatible community runtimes automatically. The adapter checks direct ABI support;
// hashes identify files but do not prove support.
enum class RuntimeKind { Nvidia, Community };
inline RuntimeKind runtime_kind(bool nvidiaSigned) {
    return nvidiaSigned ? RuntimeKind::Nvidia:RuntimeKind::Community;
}
inline bool community_api_supported(uint32_t apiVersion) { return apiVersion==0x13u; }
}
