#pragma once
#include <stdint.h>

// Tunables of the open-loop PRBS experiment (plan D3, D7). Change values here; the logic does not depend on them.
namespace prbscfg {
constexpr uint32_t kMaxValues = 32000;     // longest sequence accepted (A6)
constexpr uint32_t kLoadTimeoutMs = 5000;  // max gap between accepted value lines while loading (A13)
constexpr uint32_t kLoadTailMs = 100;      // quiet window after the last value: a character in it means "more values"
constexpr uint8_t kLineMax = 32;           // load line buffer (31 characters + terminator)
constexpr float kLimitC = 75.0f;           // uncorrected load temperature that aborts a run (A4)
static_assert(kMaxValues % 8 == 0, "the bit-packed sequence needs a multiple of 8 values");
}  // namespace prbscfg
