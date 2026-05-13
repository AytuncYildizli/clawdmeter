#include "buttons.h"

#include <M5Unified.h>
#include "ble_hid.h"

namespace buttons {

static bool g_splash_pending = false;

void tick(data::AgentKind focus) {
    // BtnA — "primary" action, agent-specific.
    if (M5.BtnA.wasPressed()) {
        switch (focus) {
            case data::AgentKind::Claude:
                ble_hid::send_key(ble_hid::Modifier::None, ble_hid::Key::Space);
                break;
            case data::AgentKind::Codex:
                ble_hid::send_key(ble_hid::Modifier::None, ble_hid::Key::Escape);
                break;
            case data::AgentKind::None:
            default:
                // no-op when no agent is focused
                break;
        }
    }

    // BtnB — splash toggle (no HID, UI-only). Consumed by main loop.
    if (M5.BtnB.wasPressed()) {
        g_splash_pending = true;
    }

    // BtnC — "secondary" action, agent-specific.
    if (M5.BtnC.wasPressed()) {
        switch (focus) {
            case data::AgentKind::Claude:
                // Shift+Tab — previous pane in Claude Code's terminal UI.
                ble_hid::send_key(ble_hid::Modifier::LShift, ble_hid::Key::Tab);
                break;
            case data::AgentKind::Codex:
                // Ctrl+Enter (a.k.a. Ctrl+J — newline in terminal contexts).
                ble_hid::send_key(ble_hid::Modifier::LCtrl, ble_hid::Key::Enter);
                break;
            case data::AgentKind::None:
            default:
                break;
        }
    }
}

bool consume_splash_toggle() {
    if (g_splash_pending) {
        g_splash_pending = false;
        return true;
    }
    return false;
}

}  // namespace buttons
