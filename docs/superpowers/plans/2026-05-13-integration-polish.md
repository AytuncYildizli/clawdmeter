# Integration & Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land Plans #2 + #3 (daemon + firmware) as a working end-to-end system on real hardware, fix the visual issues uncovered during T12 smoke, replace the procedural Codex placeholder with real sprite art, smooth the pager animation, set up launchd auto-start, and ship a fork-ready README + demo.

**Architecture:** No new components. Bug-fixing, integration testing, asset authoring, ops hardening. Each task is independently verifiable; failures stay local.

**Tech Stack:** Same as Plans #2 + #3 — Python (daemon), C++17/PlatformIO (firmware), M5Unified/LVGL/NimBLE. Asset pipeline uses Pillow + Aseprite (or any pixel-art tool) for the Codex sprite.

---

## Spec references

- Source spec: `docs/superpowers/specs/2026-05-13-clawdmeter-core2-port-design.md`
  - § 5.3 Splash (Clawd for Claude, Codex art deferred — this plan resolves the deferral)
  - § 5 Display & UX (Layout C polish: dot positions, repo label truncation)
  - § 7.4 Codex stub (confirmed by spike; firmware renders "—" — this plan adds the dot/badge polish)
- Predecessor plans:
  - `2026-05-13-codex-probe-research-spike.md` (executed — REPORT.md ships at `research/codex-spike/`)
  - `2026-05-13-macos-daemon.md` (executed — 55 tests, 10 commits)
  - `2026-05-13-firmware-core2.md` (executed — 20 native tests, 12 firmware commits, flashed to device)

## State at start of this plan

- Branch: `port/core2-macos-codex` at commit `9bf07e8` (firmware: drop upload_speed to 115200)
- Daemon: 55 tests passing, scans for `Clawd Controller` cleanly when run via `python -m clawdmeter`
- Firmware: 19.3% flash, 95.82% IRAM, 2.6% RAM. Flashes successfully at 115200 baud.
- Device boot state: meter screen renders, auto-rotate cycles 5h/7d correctly (numbers alternate as expected), but **text is garbled — letters have missing pixels / broken rendering**. Color and layout details unverified pending display fix.

## Risks

- Display fix may require multiple iterations against the device — every iteration is `pio run -t upload` (~70 seconds at 115200). Budget ~5 round trips.
- Codex sprite art is taste-dependent. Out-of-scope: agonising over pixel-perfect design. In-scope: a recognisable, on-brand 80×80 pixel-art sprite that scales cleanly to 160×160.
- BLE HID pairing on macOS can be flaky (the device advertises as both GATT + HID; some macOS versions get confused). Fallback: skip HID and ship the device as a GATT-only meter; bezel buttons drive on-device-only actions like splash toggle.

## File structure (changes only — no new directories)

```
firmware/src/main.cpp                   # display rotation + byte-swap fix (Task 1)
firmware/src/ui_pager.cpp               # smooth pager animation (Task 5)
firmware/src/assets/codex_sprite.h      # NEW — real Codex pixel art (Task 4)
firmware/tools/codex_sprite_src.png     # NEW — source 80×80 PNG (Task 4)
firmware/tools/rescale_sprites.py       # extend to handle both sprites (Task 4)
firmware/src/splash.cpp                 # wire real codex sprite (Task 4)
firmware/src/buttons.cpp                # splash B-toggle visible state via consume_splash_toggle (already done) — verify (Task 3)
daemon/launchd/sh.clawdmeter.daemon.plist  # verify auto-start (Task 6)
daemon/                                 # delete old Linux artifacts (Task 7)
README.md                               # rewrite for fork (Task 8)
research/upstream-assets/               # purge if unused after Tasks 4 + 8
```

---

### Task 1: Fix display garbled rendering

**Files:**
- Modify: `firmware/src/main.cpp`

The T12 smoke flashed firmware successfully and 71/38 auto-rotates correctly — but letters render with missing pixels. The issue is LVGL ↔ M5.Display buffer handling. Three independent suspects, tackle in order; stop at the first fix.

- [ ] **Step 1: Add display rotation**

The M5Stack Core 2 panel's native orientation is 240×320 portrait. We render in 320×240 landscape. Without `setRotation(1)`, LVGL writes a 320-pixel-wide buffer into a 240-pixel-wide window — every line wraps in the framebuffer and the panel reads garbage.

Edit `firmware/src/main.cpp`, in `setup()` immediately after `M5.begin(cfg)`:

```cpp
M5.Display.setRotation(1);  // landscape: 320 wide × 240 tall
```

Build + flash + observe:

```bash
cd firmware && pio run -e m5stack-core2 -t upload
```

If letters now render correctly → done. Commit and skip to Task 2.

- [ ] **Step 2 (if Step 1 didn't fix it): Toggle DMA byte-swap**

LVGL's `lv_color_t` for `LV_COLOR_DEPTH=16` is 16-bit little-endian RGB565. M5GFX's `writePixelsDMA(uint16_t*, count, swap)` last arg controls byte-swap. Try the opposite:

In `disp_flush_cb`, swap `false` → `true` (or vice versa):

```cpp
M5.Display.writePixelsDMA((uint16_t*)px_map, w * h, true);
```

Build + flash. If letters render → done.

- [ ] **Step 3 (if neither works): Increase LVGL buffer size**

The current `lv_color_t g_buf1[320 * 10]` is 10 rows. Bump to 20 or 40 rows. If LVGL is starving DMA mid-row, more buffer reduces tearing:

```cpp
static lv_color_t g_buf1[320 * 40];
```

Build + flash.

- [ ] **Step 4: Commit whichever fix landed**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add firmware/src/main.cpp
git commit -m "firmware: fix LVGL display rendering (rotation + byte-swap)"
```

(Adjust commit message to match what actually fixed it.)

- [ ] **Step 5: Photo the working screen**

Take a phone photo of the meter screen with readable text. This is the "before BLE pairing" reference shot for the README.

---

### Task 2: BLE pairing — daemon ↔ device end-to-end

**Files:**
- None (verification only; bug fixes if discovered land in `daemon/src/ble_writer.py` or `firmware/src/ble_peer.cpp`)

- [ ] **Step 1: Start the daemon**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter/daemon
source .venv/bin/activate
python -m clawdmeter
```

Expected log (within 15s of device boot):
- `scanning for 'Clawd Controller'...`
- `connecting to b0:b2:1c:50:d2:ec` (device MAC, observed during T12 flash)
- `connected`

If macOS shows a Bluetooth permission prompt, accept it.

- [ ] **Step 2: Verify the screen updates with real data**

The device should now display the user's actual Anthropic rate-limit numbers (not the `71%`/`38%` stub). Take a photo for comparison with Step 5 of Task 1.

- [ ] **Step 3: Verify focus tracking**

In a Superset window, focus a Claude Code pane in any repo. The device's repo label should change within ~2 seconds (Superset polling interval).

Focus a Codex pane → device shows `agent: codex` (you can verify with: swipe right to the Codex screen — the Codex screen's repo label should match Codex's focused pane).

- [ ] **Step 4: Verify REQ refresh round-trip**

Cold-boot the device (unplug + replug, or M5.Power.reset()). The firmware's `ble_peer::request_refresh()` fires once on boot. Confirm daemon log shows the orchestrator's `on_refresh` callback firing → immediate Claude probe.

- [ ] **Step 5: Capture a 10-second video**

Show the screen during normal operation: auto-rotate, swipe between screens, daemon log scrolling in a terminal. Save to `docs/demo/daemon-pairing.mp4` (or .gif). This is the integration-proof artifact.

- [ ] **Step 6: Commit any fixes**

If Task 2 surfaced bugs (parser edge cases, BLE reconnect glitches), fix them. Each fix lands as its own commit with TDD where the surface is host-testable, manual verification otherwise.

---

### Task 3: BLE HID pairing as keyboard

**Files:**
- Possibly modify: `firmware/src/ble_hid.cpp` (if pairing fails)

- [ ] **Step 1: Pair the device on macOS**

System Settings → Bluetooth. The device should appear as `Clawd Controller`. Click pair. macOS may prompt for a PIN — accept default (no PIN needed for boot keyboards) or use `0000`.

- [ ] **Step 2: Verify Claude Code Space toggle**

Focus a Claude Code terminal. Press the M5Stack's bezel A button (left of screen). Claude Code should toggle voice mode (`shift+tab` actually; correct mapping per spec § 6: A=Space, C=Shift+Tab. Voice-mode trigger is Esc+Backtick — but Space toggles voice when Claude Code is in voice-listening mode).

Test the actual key emitted by opening a text editor and pressing bezel A. Expected: a space character is typed.

- [ ] **Step 3: Verify per-agent mapping switch**

Force the device into "Codex focused" state (the daemon writes `focus.agent = "codex"` when a Codex pane is focused). Press bezel A — expected: `Escape` (no space).

- [ ] **Step 4: If pairing fails**

Two recovery paths:
1. **Remove + repair**: Bluetooth Settings → Remove `Clawd Controller` → flash device fresh → repair.
2. **Disable HID, GATT only**: If macOS keeps choking on the dual-role advertisement, comment out `ble_hid::begin()` in `main.cpp`. Device ships as GATT-only meter; bezel buttons drive on-device-only actions. Document the regression in `firmware/src/ble_hid.cpp`'s top comment.

- [ ] **Step 5: Commit & photo**

If everything works, commit any debug tweaks (none expected). Photo bezel-A press → terminal shows a space character. Save to `docs/demo/hid-keystroke.jpg`.

---

### Task 4: Real Codex splash sprite

**Files:**
- Create: `firmware/tools/codex_sprite_src.png` (80×80 pixel art)
- Modify: `firmware/tools/rescale_sprites.py` (handle multiple sources)
- Create: `firmware/src/assets/codex_sprite.h` (generated)
- Modify: `firmware/src/splash.cpp` (use real sprite instead of procedural label)

Currently the Codex splash is a teal "CODEX" text label on the dark overlay (procedural placeholder from Plan #3 Task 11). Replace with real pixel art.

- [ ] **Step 1: Author the sprite**

In any pixel-art editor (Aseprite, Piskel, Pixilart, …) create an 80×80 RGBA PNG that captures Codex's visual identity. The spec § 5.3 lists the Codex screen accent as teal `#22C4A0`. Suggestions:
- A stylized "{}" or "</>" code-block monogram
- An abstract teal pulse/wave (matches the "Codex generates code" theme)
- The OpenAI swirl in teal

Save to `firmware/tools/codex_sprite_src.png`. Verify: `file firmware/tools/codex_sprite_src.png` reports `80 x 80, 8-bit/color RGBA`.

- [ ] **Step 2: Extend the rescale pipeline**

The current `rescale_sprites.py` accepts `--source <filename>`. Add a second invocation path or a `--sources clawd:logo_80.png,codex:codex_sprite_src.png` flag.

For simplicity, run it twice:

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
python3 firmware/tools/rescale_sprites.py --in assets --out firmware/src/assets --target 160x160 --source logo_80.png --name clawd
python3 firmware/tools/rescale_sprites.py --in firmware/tools --out firmware/src/assets --target 160x160 --source codex_sprite_src.png --name codex
```

(If the script doesn't accept `--name`, add it: a one-line change to use that as the namespace prefix in the emitted header.)

Verify `firmware/src/assets/codex_sprite.h` exists with `assets::CODEX_W = 160`, `assets::CODEX_H = 160`, `assets::CODEX_DATA[]` ~51200 bytes.

- [ ] **Step 3: Wire the real sprite into splash.cpp**

Edit `firmware/src/splash.cpp`. In `init()`, build a second `lv_img_dsc_t` for codex similar to clawd:

```cpp
static lv_img_dsc_t g_codex_dsc;
g_codex_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
g_codex_dsc.header.w = assets::CODEX_W;
g_codex_dsc.header.h = assets::CODEX_H;
g_codex_dsc.data = assets::CODEX_DATA;
g_codex_dsc.data_size = sizeof(assets::CODEX_DATA);
```

In `show(bool codex_palette)`: swap `lv_image_set_src(g_img, codex_palette ? &g_codex_dsc : &g_clawd_dsc)` instead of toggling label visibility. Delete the procedural label code.

- [ ] **Step 4: Build + flash + visually verify**

```bash
cd firmware && pio run -e m5stack-core2 -t upload
```

Press bezel B on Claude screen — Clawd appears. Swipe to Codex, press B — Codex sprite appears.

- [ ] **Step 5: Commit**

```bash
git add firmware/tools/codex_sprite_src.png firmware/tools/rescale_sprites.py firmware/src/assets/codex_sprite.h firmware/src/splash.cpp
git commit -m "firmware: real Codex splash sprite (replaces procedural placeholder)"
```

---

### Task 5: Smooth pager animation

**Files:**
- Modify: `firmware/src/ui_pager.cpp`

Currently swiping between Claude and Codex screens hard-toggles `LV_OBJ_FLAG_HIDDEN`. A horizontal slide animation makes the swipe feel intentional.

- [ ] **Step 1: Add LVGL animation in on_swipe_left/right**

LVGL 9's animation API: `lv_anim_t`, `lv_anim_set_*`, `lv_anim_start`. Animate `lv_obj_set_x` from 0 → -320 (slide out left) for the outgoing screen, and from 320 → 0 (slide in from right) for the incoming.

Sketch:

```cpp
static void slide_screen(lv_obj_t* screen, int32_t from_x, int32_t to_x) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, screen);
    lv_anim_set_values(&a, from_x, to_x);
    lv_anim_set_time(&a, 250);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void on_swipe_left(Pager& p) {
    if (p.current == Page::Claude) {
        lv_obj_clear_flag(p.screens[1], LV_OBJ_FLAG_HIDDEN);  // codex visible during slide
        slide_screen(p.screens[0], 0, -320);
        slide_screen(p.screens[1], 320, 0);
        p.current = Page::Codex;
        // Schedule hiding the outgoing screen after animation completes
        // (use lv_anim_set_completed_cb to hide it)
    }
}
```

Mirror for `on_swipe_right`.

- [ ] **Step 2: Build + flash + verify**

Swipe should feel smooth (~250ms ease-in-out). Both screens visible briefly during transition.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/ui_pager.cpp
git commit -m "firmware: smooth slide animation between Claude/Codex pages"
```

---

### Task 6: launchd auto-start verification

**Files:**
- Possibly modify: `daemon/launchd/sh.clawdmeter.daemon.plist` (path adjustments if needed)

- [ ] **Step 1: Install the plist**

```bash
cp /Users/aytuncyildizli/.superset/projects/clawdmeter/daemon/launchd/sh.clawdmeter.daemon.plist \
   ~/Library/LaunchAgents/
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/sh.clawdmeter.daemon.plist
```

- [ ] **Step 2: Verify it's running**

```bash
launchctl print gui/$(id -u)/sh.clawdmeter.daemon | head -20
tail -f /tmp/clawdmeter.out.log
```

Expected: `state = running`, log shows BLE scan attempts. Within 15s of device-on, log shows `connected`.

- [ ] **Step 3: Reboot test**

Log out and back in (or full restart). Confirm daemon comes up on its own (check `/tmp/clawdmeter.out.log` timestamps).

- [ ] **Step 4: Document quirks in daemon/README.md**

If macOS prompts for Bluetooth permission on first auto-start (it likely will), document the recovery: re-run `python -m clawdmeter` interactively once, accept the prompt, then `launchctl kickstart -k gui/$(id -u)/sh.clawdmeter.daemon` to retry.

- [ ] **Step 5: Commit**

```bash
git add daemon/README.md  # if updated
git commit -m "daemon: document launchd Bluetooth-permission gotcha"
```

(No commit needed if no changes were required.)

---

### Task 7: Purge upstream Linux daemon artifacts

**Files:**
- Delete: `daemon/claude-usage-daemon.service`
- Delete: `daemon/claude-usage-daemon.sh`

These are upstream's Linux systemd daemon files. They've been sitting in `daemon/` since the fork. The macOS daemon lives at `daemon/src/` with `daemon/launchd/` for the macOS launchd plist — the Linux files are dead code.

- [ ] **Step 1: Verify nothing references them**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git grep -l "claude-usage-daemon" -- ':!daemon/claude-usage-daemon*' || echo "no references — safe to delete"
```

- [ ] **Step 2: Delete and commit**

```bash
git rm daemon/claude-usage-daemon.service daemon/claude-usage-daemon.sh
git commit -m "daemon: remove upstream Linux systemd artifacts (macOS launchd is in launchd/)"
```

---

### Task 8: Fork README rewrite

**Files:**
- Modify (replace): `README.md` (project root)

Upstream's README documents the Waveshare ESP32-S3 + Linux build. The fork is M5Stack Core 2 + macOS + Codex tracking. Rewrite top-to-bottom.

- [ ] **Step 1: Draft the new README**

Structure:

```markdown
# Clawdmeter — M5Stack Core 2 + macOS Port

[![Build](badge if CI exists)](link)

Desk dashboard for tracking Claude Code + Codex CLI usage on macOS, running on
an M5Stack Core 2 over BLE. Fork of [HermannBjorgvin/Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter)
— credits to @HermannBjorgvin for the original Waveshare + Linux design.

[demo gif here]

## What it does

- Auto-rotates Claude's 5-hour and 7-day rate-limit utilization on a 320×240 LCD
- Swipe between Claude and Codex screens (Codex shows `—` per spike findings — see `research/codex-spike/REPORT.md`)
- Repo label tracks your currently-focused Superset pane
- Bezel buttons emit agent-aware keystrokes over BLE HID

## Hardware

- M5Stack Core 2 (ESP32 Xtensa LX6, 320×240 ILI9342C LCD, FT6336U touch, 3 bezel buttons)
- USB-C cable to your Mac (for power + flashing)

## Quick start

[port instructions: flash firmware, install daemon, pair Bluetooth]

## Architecture

[brief diagram — daemon polls Claude API + Superset state, writes JSON to GATT, device renders]

## Credits

Original Clawdmeter design and Linux daemon: [@HermannBjorgvin](https://github.com/HermannBjorgvin).
M5Stack Core 2 + macOS port + Codex tracking + Superset integration: [@AytuncYildizli](https://github.com/AytuncYildizli).
```

- [ ] **Step 2: Include the demo GIF**

Drop the Task 2 video (converted to GIF) at `docs/demo/clawdmeter.gif`. Reference it in the README.

```bash
# If you captured an mp4 in Task 2:
ffmpeg -i docs/demo/daemon-pairing.mp4 -vf "fps=10,scale=480:-1:flags=lanczos" docs/demo/clawdmeter.gif
```

- [ ] **Step 3: Commit**

```bash
git add README.md docs/demo/
git commit -m "docs: README rewrite for M5Stack Core 2 + macOS fork"
```

---

### Task 9: Open the PR

**Files:**
- None (GitHub operations only)

- [ ] **Step 1: Push the branch one final time**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git push origin port/core2-macos-codex
```

- [ ] **Step 2: Open the draft PR**

```bash
gh pr create --draft \
  --base main \
  --title "Port: M5Stack Core 2 + macOS + Codex tracking" \
  --body "$(cat <<'EOF'
## Summary

- Ports upstream Clawdmeter (Waveshare ESP32-S3 + Linux daemon) to M5Stack Core 2 + macOS
- Adds second usage source for Codex CLI (currently stubbed per `research/codex-spike/REPORT.md`)
- Replaces single-meter UI with swipeable dual-provider Layout C (auto-rotating 5h/7d big number)
- Replaces Arduino_GFX/AXP2101/SensorLib with M5Unified; adds NimBLE GATT + HID dual role

## How it was built

Four sub-plans, executed sequentially:

1. **Codex probe spike** (`research/codex-spike/`) — verified that Codex CLI's ChatGPT OAuth tokens don't authenticate any usage endpoint. Codex side ships as a stub.
2. **macOS daemon** (`daemon/`) — Python + bleak. 55 tests pass.
3. **Firmware** (`firmware/`) — C++17/PlatformIO/M5Unified/LVGL/NimBLE. 20 native tests pass; flashes and runs on real hardware.
4. **Integration & polish** (this plan) — display fixes, real Codex sprite, smooth pager, launchd, README.

## Test plan

- [ ] Daemon: `cd daemon && source .venv/bin/activate && pytest` — 55 passing
- [ ] Firmware: `cd firmware && pio test -e native` — 20 passing
- [ ] Firmware: `cd firmware && pio run -e m5stack-core2 -t upload` succeeds at 115200 baud
- [ ] Device boots cleanly with M5 splash → meter
- [ ] Daemon connects within 15s; device shows real values
- [ ] Swipe pager between Claude / Codex screens works
- [ ] Bezel A in Claude Code emits Space; in Codex emits Esc
- [ ] launchd auto-starts daemon on login

## Out of scope

- OTA firmware updates
- Animated splash sprites (busy-state variations)
- Power management tuning (LCD dimming, sleep)
EOF
)"
```

- [ ] **Step 3: Mark ready for review**

After self-testing the PR description's checklist:

```bash
gh pr ready
```

(Or leave as draft if there are open polish items.)

---

## Self-review checklist

- [x] Every task lists exact files
- [x] Every task has independent verification steps
- [x] Tasks ordered by criticality — Task 1 (display fix) blocks everything else
- [x] No task assumes hardware that isn't already physically connected
- [x] Out-of-scope items are explicitly listed; no scope creep into Plan #5

## Out of scope (deferred)

- OTA firmware updates
- LCD dimming / sleep on inactivity
- Multi-device support (one device per daemon for v1)
- Linux portability of the daemon (macOS-only per spec)
- Custom heavier display fonts (Inter-Bold etc — bigger flash footprint, defer)
- BLE pairing on iOS/iPadOS — only macOS supported in v1

---

## Plan #4.5 — Visual parity with upstream

After comparing to upstream's screenshots (https://github.com/HermannBjorgvin/Clawdmeter — Splash + Usage + Bluetooth pages), our v1 ships text-on-black; upstream has animated Aseprite sprites, rounded cards, battery indicator, and a Bluetooth status page. These tasks close the gap. They depend on Tasks 1-3 above being done (so we have a working baseline before iterating on look-and-feel).

### Task 10: Animated Clawd sprite frames

**Files:**
- Create: `firmware/tools/clawd_frames/clawd_{idle,blink,wave,sleep}_80.png` (4 frames, 80×80)
- Modify: `firmware/tools/rescale_sprites.py` (handle multi-frame directory)
- Create: `firmware/src/assets/clawd_animation.h` (4-frame strip)
- Modify: `firmware/src/splash.cpp` (cycle frames at ~5 FPS)

Pixel-art four-frame animation: idle (open eyes), blink (closed eyes), wave (arm up), sleep (zZz). Each frame 80×80, upscale 2× to 160×160. The animation cycles: idle 0.6s → blink 0.1s → idle 0.6s → wave 0.4s → idle 0.6s → sleep 0.4s. Total cycle ~2.5s.

Use [Aseprite](https://aseprite.org) or [Piskel](https://www.piskelapp.com) (free, browser) to author. Reference upstream's `@amaanbuilds` sprite style: simple silhouette, two square eyes, four limbs.

LVGL animation: `lv_image_set_src(g_img, &g_frames[frame_idx])` on a timer every 100ms; advance `frame_idx` based on cycle state.

Out-of-scope here: codex animation. Codex keeps the procedural label (Task 4 of this plan replaces with static sprite; animation is Plan #5).

### Task 11: Bluetooth status page (third swipe page)

**Files:**
- Create: `firmware/src/ui_ble_page.h`
- Create: `firmware/src/ui_ble_page.cpp`
- Modify: `firmware/src/ui_pager.h` (add third page)
- Modify: `firmware/src/ble_peer.cpp` (expose connection state)
- Modify: `firmware/src/main.cpp` (wire pager → ble_page)

Third swipe page (Claude ← → Codex ← → Bluetooth). Shows:
- Connection state: "Connected" (green) or "Disconnected" (gray)
- Peer device address (the daemon's MAC) once paired
- "Built by @hermannbjorgvin / Ported by @AytuncYildizli / Codex tracking spike" credits at bottom (small)
- A "RESET BOND" button (LVGL `lv_button_create`) wired to `NimBLEDevice::deleteAllBonds()`

NimBLE connection-state hook: `NimBLEServerCallbacks::onConnect`/`onDisconnect`. Update a global flag the UI polls in refresh.

### Task 12: Battery indicator + progress bars

**Files:**
- Modify: `firmware/src/ui_meter.cpp`

Two simple polish items on every meter page:

**Battery indicator (top-right):** `M5.Power.getBatteryLevel()` returns 0-100. Render as a small 24×12 icon (rectangle outline + filled portion) plus the percentage. Use 4 charge-state colors: red <20%, yellow <50%, green ≥50%. Update once a second (no need to poll faster).

**Progress bar under the big number:** `lv_bar_create` set to the same value as the big number. Rounded corners (10px radius), 200×8, accent color (Claude orange or Codex teal depending on which screen). The big-number `71%` text stays; the bar adds a visual aid for the eye.

### Task 13: Rounded card layout

**Files:**
- Modify: `firmware/src/ui_meter.cpp`

Currently labels float on a solid black background. Wrap the central content (provider tag + big number + reset label + secondary tick + repo label) in an `lv_obj_t` "card":
- Width 280, height 180, rounded corners (15px radius)
- Background `theme::BG` (still near-black but distinct from screen background)
- Border 1px, color `theme::TEXT_SECONDARY` at 30% opacity OR no border with subtle inner shadow
- Padding 12px

Then the screen background can be a slightly different color (e.g. `0x080808` near-pure-black with a subtle gradient via `lv_obj_set_style_bg_grad_color`). Upstream's screenshot uses this exact card-on-darker-bg pattern.

### Task 14: Status footer ("Baking…", "Synced", "Stale")

**Files:**
- Modify: `firmware/src/ui_meter.cpp`
- Modify: `firmware/src/main.cpp` (track last-write time)

Below the repo label, add a small status line that surfaces the daemon's connection state to the user:

| State | Label | Color |
|---|---|---|
| Never received payload yet | "Waiting…" | gray |
| Last payload < 5 min ago | "Synced" | green dot + gray text |
| Last payload 5-60 min ago | "Stale" | yellow dot |
| Last payload > 60 min ago | "Offline" | red dot |
| Claude probe in flight (REQ fired) | "Baking…" | orange dot pulse |

Hook: `main.cpp` tracks `last_payload_millis` updated in the `ble_peer::begin` payload callback. ui_meter::refresh reads it and computes the freshness bucket.

---

## Self-review checklist (updated)

- [x] Display fix is Task 1 (P0, blocks everything else)
- [x] Visual parity items (Tasks 10-14) come AFTER the integration items (Tasks 1-9) so we have a working baseline before chasing pixel-perfect
- [x] No task assumes hardware that isn't already connected
- [x] All credits flow back to @hermannbjorgvin and @amaanbuilds where their work is referenced
