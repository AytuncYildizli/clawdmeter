# Clawdmeter — M5Stack Core 2 + macOS + Codex Port (Design Spec)

**Date:** 2026-05-13
**Status:** Approved (pending user review of this document)
**Upstream:** `HermannBjorgvin/Clawdmeter` (Linux + Waveshare ESP32-S3 AMOLED)
**Fork:** `AytuncYildizli/clawdmeter` (this project)

## 1. Goal

Port Clawdmeter to run on **M5Stack Core 2 hardware** with a **macOS host daemon**, and extend it to display utilization for **two coding-agent providers** side-by-side:

1. **Claude** (via Anthropic OAuth-mode rate-limit headers)
2. **Codex** (via OpenAI ChatGPT-mode usage probe — endpoint TBD)

The device's button-and-meter behavior must be **context-aware** to the host machine's active agent pane, as orchestrated through the user's existing **Superset** desktop app. The device replaces the original's "Claude Code remote with bonus usage meter" with a "Claude + Codex remote + dual usage meter" while keeping the original's spirit.

## 2. Non-goals

Explicitly out of scope for this port:

- Linux host support (original daemon remains for upstream; this fork is macOS-only)
- Fleet-companion features: real-time permission-request notifications, per-agent live status, multi-session list widget
- Per-repo token-spend accounting from `~/.claude/projects/*/history.jsonl`
- Backward-compatibility with the original's payload shape
- Hardware support for any board other than M5Stack Core 2

## 3. Hardware target

**M5Stack Core 2.**

| Subsystem | Component | Replaces (original) |
|---|---|---|
| MCU | ESP32 Xtensa LX6, 240 MHz dual-core | ESP32-S3 |
| RAM | 520 KB SRAM + 8 MB PSRAM | 8 MB PSRAM (qio_opi) |
| Display | 320×240 ILI9342C SPI LCD | 480×480 CO5300 QSPI AMOLED |
| Touch | FT6336U capacitive (3 bezel keys A/B/C + screen) | CST9220 |
| PMU | AXP192 | AXP2101 |
| IMU | MPU6886 | QMI8658 |
| USB | CP210x UART bridge (`/dev/cu.usbserial-…`) | Native S3 USB CDC |
| Power | Built-in 390 mAh LiPo + USB-C | External MX1.25 LiPo + USB-C |

**Library consolidation:** Use `M5Unified` instead of Arduino_GFX + SensorLib + XPowersLib. M5Unified handles display, touch, PMU, IMU, and buttons through a single API and is actively maintained by M5Stack.

## 4. Host target

**macOS** (current dev env: Darwin 25.4.0). The daemon will be macOS-only.

**Required permissions:**
- Bluetooth (granted at first BLE scan)
- No Accessibility permission needed (we read Superset state instead of OS focus state)

**Required host software:**
- Superset desktop app (the user's primary agent orchestrator)
- Claude Code CLI signed in (`~/.claude/.credentials.json` present)
- Codex CLI signed in via ChatGPT (`~/.codex/auth.json` with `auth_mode=ChatGPT`)
- Python 3.10+

## 5. UX/UI specification

### 5.1 Screen pager

Two horizontally swipable screens via FT6336U gesture detection. Initial screen is **Claude** (left). Swipe-left advances to Codex; swipe-right returns to Claude. The page indicator is a row of two dots rendered at screen bottom: filled = current screen.

No scroll inertia, no rubber-banding — discrete swipe → animate → land. Animation duration ~150 ms.

### 5.2 Per-screen layout (Layout C — Minimal Big Number)

Each provider screen is identical in structure, differing only in palette and provider tag:

| Region | Content | Cadence |
|---|---|---|
| Top center (10 px) | `CLAUDE · 5H` or `CLAUDE · 7D` (top label, 11 px sans, letter-spaced 4 px) | Updates on auto-rotate |
| Center (60% of height) | Big percentage — `71%` style, 96 px light-weight monospace, white digit + colored `%` glyph | Auto-rotates every **5 seconds** between 5h and 7d |
| Below center (16 px) | Reset countdown — e.g. `resets in 2h 14m` (10 px, gray) | Updates with rotated timeframe |
| Bottom subline (12 px) | The *other* timeframe as a tick — e.g. `weekly 38% · resets 4d 6h` (10 px) | Updates with rotated timeframe |
| Repo label (bottom-center, 12 px) | `rotator · +2` — focused worktree name + count of other active agent sessions | Updates whenever Superset state changes |
| Page indicator (bottom) | `● ○` or `○ ●` — 2 dots showing which screen is active | Updates on swipe |

**Palette:**
- Claude screen: warm orange `#ff8c42` accent on near-black `#000` background
- Codex screen: teal/cyan `#22c4a0` accent on near-black `#000` background

**Auto-rotation behavior:** Fixed 5-second cadence between 5h and 7d for v1. No utilization-based timing variation. Revisit if it feels jittery in user testing.

### 5.3 Splash overlay

Pressing **B** while on either screen overlays the splash mode on top of the meter. Pressing B again dismisses.

- **Claude screen splash:** Clawd pixel-art animations from upstream. Sprites must be rescaled from 480×480 to 320×240 — preserve pixel-art aesthetic (nearest-neighbor downscale, then hand-touch-up the busiest frames). Animation rotation cadence: 20 seconds (matches upstream).
- **Codex screen splash:** New custom sprite, designed for this port. Direction: abstract teal/black pulse animation that intensifies with current usage rate. (Pixel-art OpenAI character recommended *against* due to trademark / aesthetic conflict.)

**Activity coupling:** Both splashes get visually busier as usage rate climbs, replicating the upstream's "busier Clawd at higher utilization" behavior. Specific rate-bucket thresholds documented in firmware constants, default `<30% calm`, `30–70% medium`, `>70% busy`.

### 5.4 Touch interactions

- **Swipe left / right:** change provider screen (also from splash mode — splash dismisses on swipe)
- **Tap screen (not on a button zone):** toggle splash on/off (same as B)
- **A / B / C bezel keys:** see Section 6

## 6. Button mapping (Superset-aware)

The daemon reads `~/.superset/app-state.json` every 2 seconds and extracts the currently focused pane:

```python
state = json.load("~/.superset/app-state.json")
active_tab_id = list(state["tabsState"]["activeTabIds"].values())[0]  # active workspace's tab
focused_pane_id = state["tabsState"]["focusedPaneIds"][active_tab_id]
pane = state["tabsState"]["panes"][focused_pane_id]
pane_name = pane["name"]  # e.g. "⠂ Claude Code", "✳ Claude Code", "Codex", "Terminal"
pane_cwd = pane.get("cwd")  # worktree path
```

The daemon classifies the focused agent by substring match on `pane_name`:

| `pane_name` contains | Classified `focus.agent` |
|---|---|
| "Claude Code" (any prefix glyph allowed) | `"claude"` |
| "Codex" | `"codex"` |
| anything else | `"none"` |

The classification is included in every BLE payload (`focus.agent`). The firmware uses it to select the active key set:

| `focus.agent` | A (left bezel) | B (middle bezel) | C (right bezel) |
|---|---|---|---|
| `claude` | `Space` (voice mode) | Splash toggle (device-local) | `Shift+Tab` (mode cycle) |
| `codex` | `Esc` (interrupt) | Splash toggle (device-local) | `Ctrl+J` (newline-in-input) |
| `none` | inert (no HID emit, brief red flash) | Splash toggle (device-local) | inert (no HID emit, brief red flash) |

**B is device-local in all cases** — it never emits HID. It always toggles the splash overlay on the current screen.

**Inert feedback:** When A or C is pressed but focus is `none`, the firmware MUST emit a 100 ms red flash on the page-indicator row to signal "ignored" without sending a stray keystroke.

## 7. Daemon

### 7.1 Language & dependencies

- **Python 3.10+**
- Single external dependency: `bleak >= 0.21`
- Standard library: `asyncio`, `json`, `pathlib`, `urllib.request`, `time`, `logging`
- Optional: `watchdog` for file-watching Superset state (preferred over polling); fall back to 2 s polling if `watchdog` is unavailable

### 7.2 Architecture

Three concurrent `asyncio` tasks plus the BLE writer:

| Task | Cadence | Output |
|---|---|---|
| `poll_claude()` | 60 s | `claude` dict (`s`, `sr`, `w`, `wr`, `st`, `ok`) |
| `poll_codex()` | 60 s | `codex` dict (same shape; `ok=false` if probe fails) |
| `poll_superset()` | 2 s (or file-watch) | `focus` dict (`agent`, `repo`, `sessions`) |
| `ble_writer()` | wakes whenever any of the above updates, debounced 250 ms | writes combined payload to RX char |

Each poller updates a shared `asyncio.Event`. The writer awaits the event, waits 250 ms (debounce), then serializes the merged state and writes once. This avoids three writes per poll cycle when pollers happen to fire close together.

### 7.3 Claude probe (preserved from upstream)

```
POST https://api.anthropic.com/v1/messages
  Authorization: Bearer <token from ~/.claude/.credentials.json>
  anthropic-version: 2023-06-01
  anthropic-beta: oauth-2025-04-20
  User-Agent: claude-code/<current-version>
  body: {"model":"claude-haiku-4-5-20251001","max_tokens":1,"messages":[{"role":"user","content":"hi"}]}
```

Parse response headers:
- `anthropic-ratelimit-unified-5h-utilization` → `claude.s` (×100, integer percent)
- `anthropic-ratelimit-unified-5h-reset` (epoch) → `claude.sr` (minutes-until-reset)
- `anthropic-ratelimit-unified-7d-utilization` → `claude.w`
- `anthropic-ratelimit-unified-7d-reset` → `claude.wr`
- `anthropic-ratelimit-unified-5h-status` → `claude.st`

On any failure (network, 4xx, 5xx): `claude.ok = false`, prior values preserved.

### 7.4 Codex probe (**research item — empirical**)

`~/.codex/auth.json` has `auth_mode: "ChatGPT"` and an OAuth token under `tokens`. The plan stage will empirically determine which `chatgpt.com/backend-api/*` endpoint returns plan utilization. Likely candidates (ordered by guess):

1. `GET https://chatgpt.com/backend-api/conversation_limit`
2. `GET https://chatgpt.com/backend-api/models/usage`
3. `GET https://chatgpt.com/backend-api/me`

The plan must include an exploratory phase that probes each with the OAuth token from `~/.codex/auth.json` and documents the working response shape.

**Failure mode:** If no working endpoint can be found, ship Codex side as a stub (`codex.ok = false`, screen renders "—" everywhere). Device still functions as a Claude-only meter.

### 7.5 Superset state read

Watch `~/.superset/app-state.json` for changes (or poll every 2 s). On change:

1. Extract focused pane as in Section 6.
2. Classify `focus.agent` per the table in Section 6.
3. Extract `focus.repo`: derive from `pane.cwd` by taking the directory name that follows `~/.superset/worktrees/<project>/` if applicable, else the basename of `cwd`. Empty string if `cwd` is `None`.
4. Compute `focus.sessions`: count of panes in `state["tabsState"]["panes"]` where (a) `name` matches Claude Code or Codex, (b) `status != "idle"`, and (c) `id != focused_pane_id`. The focused pane is always excluded regardless of its own status, so the count represents *other* active agent panes.

## 8. BLE protocol

### 8.1 GATT service

Preserve upstream's custom UUIDs:

| Role | UUID |
|---|---|
| Service | `4c41555a-4465-7669-6365-000000000001` |
| RX (write) | `4c41555a-4465-7669-6365-000000000002` |
| REQ (notify) | `4c41555a-4465-7669-6365-000000000004` |

Advertised name: `Clawd Controller` (renamed from upstream's `Claude Controller` to reflect dual-provider scope).

### 8.2 Payload (single JSON write)

```json
{
  "claude": {
    "s":  71,      // 5h utilization percent (integer 0-100)
    "sr": 134,     // minutes until 5h reset
    "w":  38,      // 7d utilization percent
    "wr": 6240,    // minutes until 7d reset
    "st": "allow", // "allow" | "warn" | "block" | "unknown"
    "ok": true     // false if last probe failed
  },
  "codex": {
    // same shape; "ok": false if probe stub
  },
  "focus": {
    "agent": "claude",   // "claude" | "codex" | "none"
    "repo": "rotator",   // worktree directory name, "" if unknown
    "sessions": 3        // count of OTHER active agent panes
  }
}
```

Max payload size target: **400 bytes** (well within ESP32 BLE MTU after negotiation). Firmware parses with ArduinoJson, computes `JsonDocument` size of 512.

### 8.3 REQ characteristic

Behavior preserved from upstream: device writes to REQ to request an immediate poll (e.g., on boot, after manual refresh). Daemon subscribes to REQ notifications and triggers all three pollers immediately on receipt.

## 9. Firmware

### 9.1 PlatformIO env

Replace `firmware/platformio.ini` entirely with:

```ini
[env:m5stack_core2]
platform = espressif32
board = m5stack-core2
framework = arduino
upload_speed = 921600
monitor_speed = 115200

build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=3
    ; NimBLE peripheral-only single connection
    -DCONFIG_BT_NIMBLE_ROLE_BROADCASTER=1
    -DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=1
    -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=0
    -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0
    -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
    ; LVGL configuration
    -DLV_CONF_SKIP
    -DLV_COLOR_DEPTH=16
    -DLV_USE_LOG=0
    -DLV_USE_LABEL=1
    -DLV_USE_IMG=1
    -DLV_USE_OBJ=1
    -DLV_TICK_CUSTOM=1

lib_deps =
    m5stack/M5Unified@^0.2.6
    lvgl/lvgl@^9.2.0
    bblanchon/ArduinoJson@^7.0.0
    h2zero/NimBLE-Arduino@^2.1.1
```

### 9.2 Source-file changes

| File | Disposition |
|---|---|
| `firmware/src/main.cpp` | Rewrite using M5Unified `M5.begin()` / `M5.update()` loop |
| `firmware/src/display_cfg.h` | Replaced by M5Unified setup; delete |
| `firmware/src/power.{h,cpp}` | Rewrite for AXP192 via `M5.Power` |
| `firmware/src/imu.{h,cpp}` | Rewrite for MPU6886 via `M5.Imu` (or delete — IMU unused in v1) |
| `firmware/src/ui.{h,cpp}` | Rewrite for Layout C, 320×240, dual-screen pager |
| `firmware/src/splash.{h,cpp}` | Adapt for rescaled Clawd assets + new Codex sprite |
| `firmware/src/splash_animations.h` | Regenerate from rescaled assets |
| `firmware/src/ble.{h,cpp}` | Extend payload parser for dual-provider shape; rename advertised name to "Clawd Controller" |
| `firmware/src/data.h` | Extend usage struct: separate `claude` and `codex` blocks plus `focus` |
| `firmware/src/usage_rate.{h,cpp}` | Generalize to per-provider rate bucketing for splash intensity |
| `firmware/src/theme.h` | Add second palette (Codex teal) alongside Claude orange |
| `firmware/src/logo.h`, `firmware/src/icons.h` | Audit for resolution-specific assets; replace as needed |

### 9.3 LVGL bindings

M5Unified provides a `Display` object; LVGL's `lv_disp_drv_t` `flush_cb` writes the LVGL framebuffer into `M5.Display.pushImageDMA(...)`. Touch read callback queries `M5.Touch.getDetail()` for x/y/state and feeds LVGL's `lv_indev_drv_t`. Standard pattern; see M5Unified examples in the M5Stack GitHub.

## 10. Verification approach (carried into plan)

Each layer of the implementation plan must include verification:

- **Daemon Claude probe**: assert headers parsed correctly against a recorded fixture
- **Daemon Codex probe**: dependent on research phase; if endpoint found, fixture-based; if stub, assert `ok=false` propagates
- **Daemon Superset read**: fixture-based unit test against a recorded `app-state.json`
- **BLE wire**: integration test — daemon writes payload, ESP firmware (or a Python BLE client simulating ESP) reads and reports back via REQ
- **UI rendering**: visual smoke test — flash a build, run the daemon, confirm both screens render correctly on the actual device
- **Button mapping**: integration test — switch focused Superset pane, confirm device updates `focus.agent` and bezel keys send the correct HID codes

## 11. Open items carried into the implementation plan

1. **Codex usage probe endpoint research** (Section 7.4)
2. **Clawd sprite rescaling pipeline** (Section 5.3) — automated downscale + manual cleanup workflow
3. **Codex sprite design** (Section 5.3) — visual direction not yet specified
4. **B button on M5Stack Core 2 capacitive zone** — verify A/B/C touch zones are reliable enough to drive without false-triggers; if not, swap to long-press semantics or fall back to screen-area buttons

## 12. References

- Upstream: <https://github.com/HermannBjorgvin/Clawdmeter>
- M5Stack Core 2 docs: <https://docs.m5stack.com/en/core/core2>
- M5Unified library: <https://github.com/m5stack/M5Unified>
- bleak: <https://github.com/hbldh/bleak>
- Superset: <https://superset.sh/>
