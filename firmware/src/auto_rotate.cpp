#include "auto_rotate.h"

namespace rotate {

constexpr uint32_t PERIOD_MS = 5000;

void reset(State& st, uint32_t now_ms) {
    st.epoch_ms = now_ms;
}

Frame current(const State& st, uint32_t now_ms) {
    uint32_t elapsed = now_ms - st.epoch_ms;  // overflow-safe via unsigned arithmetic
    uint32_t cycle_pos = (elapsed / PERIOD_MS) % 2;
    return (cycle_pos == 0) ? Frame::FiveHour : Frame::SevenDay;
}

}  // namespace rotate
