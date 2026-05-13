#pragma once
#include <cstdint>

namespace theme {

constexpr uint32_t BG = 0x000000;
constexpr uint32_t CLAUDE_ACCENT = 0xFF8C42;  // warm orange
constexpr uint32_t CODEX_ACCENT  = 0x22C4A0;  // teal
constexpr uint32_t TEXT_PRIMARY  = 0xFFFFFF;
constexpr uint32_t TEXT_SECONDARY= 0x888888;
constexpr uint32_t DOT_ACTIVE_CLAUDE = CLAUDE_ACCENT;
constexpr uint32_t DOT_ACTIVE_CODEX  = CODEX_ACCENT;
constexpr uint32_t DOT_INACTIVE  = 0x333333;

// LVGL color helpers — defined in ui_meter.cpp once LVGL is integrated
}  // namespace theme
