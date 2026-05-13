#pragma once
#include <cstdint>

namespace rotate {

enum class Frame : uint8_t { FiveHour, SevenDay };

struct State {
    uint32_t epoch_ms = 0;  // ms-of-day anchor for the cycle
};

void reset(State& st, uint32_t now_ms);
Frame current(const State& st, uint32_t now_ms);

}  // namespace rotate
