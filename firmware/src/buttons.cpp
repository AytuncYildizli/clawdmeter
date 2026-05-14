#include "buttons.h"

#include <M5Unified.h>
#include "ble_hid.h"

namespace buttons {

static bool g_splash_pending = false;
static int g_swipe_pending = 0;  // -1 = left, +1 = right, 0 = none

void tick(data::AgentKind focus) {
    (void)focus;  // focused_agent kept in signature for future HID re-wiring

    // BtnA -> page swipe left (Claude -> Codex)
    if (M5.BtnA.wasPressed()) {
        g_swipe_pending = -1;
    }

    // BtnB -> splash toggle (UI-only). Consumed by main loop.
    if (M5.BtnB.wasPressed()) {
        g_splash_pending = true;
    }

    // BtnC -> page swipe right (Codex -> Claude)
    if (M5.BtnC.wasPressed()) {
        g_swipe_pending = +1;
    }
}

bool consume_splash_toggle() {
    if (g_splash_pending) {
        g_splash_pending = false;
        return true;
    }
    return false;
}

int consume_swipe_intent() {
    int v = g_swipe_pending;
    g_swipe_pending = 0;
    return v;
}

}  // namespace buttons
