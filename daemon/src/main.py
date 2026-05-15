"""Asyncio orchestrator. Three pollers + writer + REQ refresh handler.

Cadence (production):
- Claude probe: every 60s
- Codex stub: same tick as Claude (~free)
- Superset state: every 2s
- Writer: debounce 250ms; fires when any source updates state

On `on_refresh` from the device, Claude is polled immediately (regardless
of the 60s interval).
"""
from __future__ import annotations
import asyncio
import json
import logging
import subprocess
from pathlib import Path
from .ble_writer import BleWriter
from .claude_probe import probe_claude
from .codex_stub import codex_stub
from .codex_probe import probe_codex, CODEX_LOG_PATH
from .account_pool import AccountPool, Account
from .activity import ActivityTracker

try:
    from watchdog.observers import Observer  # type: ignore
    from watchdog.events import FileSystemEventHandler  # type: ignore
    _HAS_WATCHDOG = True
except ImportError:
    Observer = None  # type: ignore
    FileSystemEventHandler = object  # type: ignore
    _HAS_WATCHDOG = False
from .superset_state import read_focus, FocusInfo
from .state import State

log = logging.getLogger("clawdmeter.main")

CLAUDE_CREDS_PATH = Path.home() / ".claude" / ".credentials.json"
CLAUDE_KEYCHAIN_SERVICE = "Claude Code-credentials"
CLAUDE_PROJECTS_DIR = Path.home() / ".claude" / "projects"
SUPERSET_STATE_PATH = Path.home() / ".superset" / "app-state.json"


def _load_keychain_credentials() -> dict | None:
    """Read Claude Code's OAuth credentials from macOS Keychain.

    Claude Code on macOS stores its tokens in a generic-password keychain item
    named "Claude Code-credentials" instead of writing a credentials file. The
    payload is a JSON blob like {"claudeAiOauth": {"accessToken": "..."}}.

    Multiple Keychain entries can exist under the same service name (e.g. an
    older entry with acct="unknown" left over from a prior version, plus the
    current one with acct=<username>). `security -w` without `-a` returns the
    first match in keychain order, which is usually the stale one. We probe
    the current username first, then fall back to a generic match.
    """
    import os
    candidates = [os.environ.get("USER")] if os.environ.get("USER") else []
    # Empty string at the end = no -a filter (fallback for non-username entries)
    candidates.append(None)
    for acct in candidates:
        cmd = ["security", "find-generic-password", "-s", CLAUDE_KEYCHAIN_SERVICE, "-w"]
        if acct:
            cmd[2:2] = ["-a", acct]
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        if result.returncode != 0 or not result.stdout:
            continue
        try:
            return json.loads(result.stdout.strip())
        except Exception:
            continue
    return None


def load_claude_token() -> str | None:
    """Resolve the Anthropic OAuth access token from (in order):
    1. ~/.claude/.credentials.json    — upstream Linux path
    2. macOS Keychain "Claude Code-credentials" generic password
    """
    if CLAUDE_CREDS_PATH.exists():
        try:
            data = json.loads(CLAUDE_CREDS_PATH.read_text())
            token = data.get("accessToken") or data.get("access_token")
            if token:
                return token
        except Exception:
            pass
    kc = _load_keychain_credentials()
    if kc:
        oauth = kc.get("claudeAiOauth") or {}
        token = oauth.get("accessToken") or kc.get("accessToken") or kc.get("access_token")
        if token:
            return token
    return None


def load_superset_state() -> dict | None:
    if not SUPERSET_STATE_PATH.exists():
        return None
    try:
        return json.loads(SUPERSET_STATE_PATH.read_text())
    except Exception:
        return None


class Orchestrator:
    def __init__(self, writer: BleWriter, poll_interval: float = 60.0,
                 superset_interval: float = 2.0, debounce: float = 0.25) -> None:
        self.writer = writer
        self.state = State()
        self.poll_interval = poll_interval
        self.superset_interval = superset_interval
        self.debounce = debounce
        self._dirty = asyncio.Event()
        self._refresh = asyncio.Event()
        self._stopped = asyncio.Event()
        self._codex_dirty = asyncio.Event()  # fsevents-driven; see _codex_watcher_loop
        self._codex_observer = None  # watchdog Observer (or None if unavailable)
        self._claude_observer = None  # watchdog Observer for ~/.claude/projects
        self._activity = ActivityTracker()  # diffs Superset state into events

    def _on_connected(self) -> None:
        # Flush current state to the freshly-connected device. Writes attempted
        # before the BLE link came up were silently skipped; this catches them.
        self._dirty.set()

    def _on_refresh(self) -> None:
        # Called from BleWriter notify thread (asyncio loop's call_soon_threadsafe is the
        # canonical way; for bleak's macOS backend the callback runs on the loop already.)
        self._refresh.set()

    async def _claude_loop(self) -> None:
        # Multi-account aggregator. Each tick:
        #   1. Snapshot current Keychain entry into the persistent pool — picks
        #      up new accounts as the user rotates Claude Code logins.
        #   2. Probe every non-expired account in the pool for fresh rate
        #      limits. Expired tokens are kept in the pool but reported as
        #      stale (ok=False, last-known data preserved).
        #   3. Build a compact account roll-up (top 3 by recency) for the BLE
        #      payload and set claude.* to the active account for back-compat.
        pool = AccountPool()
        per_account_cache: dict[str, dict] = {}  # acct_id -> last probe block
        loop = asyncio.get_running_loop()

        while not self._stopped.is_set():
            now_epoch = int(asyncio.get_running_loop().time())  # monotonic; only diffs matter
            wall_epoch = int(__import__("time").time())
            active = pool.snapshot_current(wall_epoch)
            accounts = pool.list_accounts()
            if not accounts:
                log.warning("no Claude accounts in pool — no Keychain entry found")
            else:
                # Probe each account whose token is still valid. Run in a thread
                # pool so the urllib calls don't block the asyncio loop.
                async def _probe(acct: Account) -> tuple[Account, dict]:
                    if acct.is_token_expired(wall_epoch * 1000):
                        return acct, {"s": 0, "sr": 0, "w": 0, "wr": 0,
                                       "st": "expired", "ok": False}
                    block = await loop.run_in_executor(
                        None, probe_claude, acct.access_token, wall_epoch
                    )
                    return acct, block

                results = await asyncio.gather(
                    *[_probe(a) for a in accounts], return_exceptions=True
                )
                wire_rows = []
                for r in results:
                    if isinstance(r, BaseException):
                        log.warning("account probe error: %s", r)
                        continue
                    acct, block = r
                    per_account_cache[acct.id] = block
                    wire_rows.append({
                        "n": acct.display_name(),
                        "s": int(block.get("s") or 0),
                        "sr": int(block.get("sr") or 0),
                        "w": int(block.get("w") or 0),
                        "wr": int(block.get("wr") or 0),
                        "ok": bool(block.get("ok")),
                        "a": acct.id == pool.active_id,
                    })

                # Active account block remains in `claude` for back-compat.
                if active and active.id in per_account_cache:
                    active_block = per_account_cache[active.id]
                    log.info("claude probe (active=%s): ok=%s s=%s%% w=%s%%",
                             active.display_name(), active_block.get("ok"),
                             active_block.get("s"), active_block.get("w"))
                    self.state.update_claude(active_block)
                self.state.update_claude_accounts(wire_rows)
                self.state.update_codex(probe_codex())
                self._dirty.set()
            # Wait for next interval OR a refresh request
            try:
                await asyncio.wait_for(self._refresh.wait(), timeout=self.poll_interval)
                self._refresh.clear()
            except asyncio.TimeoutError:
                pass

    async def _codex_watcher_loop(self) -> None:
        """Real-time Codex updates via FSEvents.

        Every Codex CLI websocket turn appends a row to ~/.codex/logs_2.sqlite.
        Polling at 60s would lag a Codex prompt's rate-limit update by up to
        59 seconds; watchdog gives us file-change notification in ~100ms.

        Watchdog's Observer runs on a background thread — to wake this asyncio
        coroutine safely we route the modify event through
        loop.call_soon_threadsafe(self._codex_dirty.set), then debounce 200ms
        (SQLite writes typically arrive as 2-3 close-spaced events per turn:
        a journal write + the actual commit). After the debounce we re-probe
        and push the result through the same _dirty path as the 60s poller.
        """
        if not _HAS_WATCHDOG:
            log.info("watchdog not available; Codex updates run at 60s cadence only")
            return
        if not CODEX_LOG_PATH.exists():
            log.info("codex log %s missing; fsevents watcher disabled", CODEX_LOG_PATH)
            return

        loop = asyncio.get_running_loop()

        class _Handler(FileSystemEventHandler):  # type: ignore[misc]
            def __init__(self, set_dirty):
                self._set_dirty = set_dirty
            def on_modified(self, event):
                # SQLite rotates between -journal/-wal files alongside the .sqlite —
                # any of them firing is a sign of new activity.
                self._set_dirty()
            on_created = on_modified

        def _trigger():
            # Called from watchdog's thread; bounce onto the asyncio loop.
            loop.call_soon_threadsafe(self._codex_dirty.set)

        handler = _Handler(_trigger)
        self._codex_observer = Observer()
        # Watch the directory (not the file) — SQLite writes go to companion
        # files (-journal, -wal, -shm) and atomic-rename swaps; watching the
        # parent dir catches them all.
        self._codex_observer.schedule(handler, str(CODEX_LOG_PATH.parent),
                                       recursive=False)
        self._codex_observer.start()
        log.info("codex fsevents watcher armed on %s", CODEX_LOG_PATH.parent)

        try:
            while not self._stopped.is_set():
                try:
                    await asyncio.wait_for(self._codex_dirty.wait(), timeout=10.0)
                except asyncio.TimeoutError:
                    continue
                self._codex_dirty.clear()
                # Debounce: SQLite typically writes the journal then commits;
                # waiting 200ms collapses ~3 events into one probe.
                await asyncio.sleep(0.2)
                self._codex_dirty.clear()
                try:
                    block = probe_codex()
                    if block.get("ok"):
                        # Only push when something actually changed. Codex CLI
                        # writes many non-rate-limit log rows; the SQLite file
                        # mtime ticks on each one. Probing every time is fine
                        # (cheap), but pushing 487B over BLE every second is
                        # waste. Compare to the last-pushed block.
                        prev = self.state.codex
                        changed = (
                            block.get("s") != prev.get("s")
                            or block.get("w") != prev.get("w")
                            or block.get("sr") != prev.get("sr")
                            or block.get("wr") != prev.get("wr")
                            or block.get("ok") != prev.get("ok")
                        )
                        if changed:
                            log.info("codex fsevents probe (changed): s=%s%% w=%s%%",
                                     block.get("s"), block.get("w"))
                            self.state.update_codex(block)
                            self._dirty.set()
                except Exception as e:
                    log.warning("codex fsevents probe failed: %s", e)
        finally:
            try:
                self._codex_observer.stop()
                self._codex_observer.join(timeout=2.0)
            except Exception:
                pass

    async def _claude_fs_watcher_loop(self) -> None:
        """Real-time Claude updates via FSEvents on ~/.claude/projects.

        Symmetric to the Codex watcher. Claude Code appends a JSONL row to
        `~/.claude/projects/<uuid>/conversation-*.jsonl` after every turn —
        any modify event in that tree is a strong signal that a Claude API
        round-trip just completed and the rate-limit headers have shifted.

        Instead of running our own probe loop, we just fire self._refresh.set()
        which wakes the existing _claude_loop's wait_for() and forces an
        immediate poll of all pool accounts. Reuses the BLE-REQ refresh path.

        Debounce 300ms to collapse the burst of writes during a single turn
        (Claude Code rewrites the same JSONL file multiple times per turn).
        """
        if not _HAS_WATCHDOG:
            return
        if not CLAUDE_PROJECTS_DIR.exists():
            log.info("claude projects dir %s missing; fsevents watcher disabled",
                     CLAUDE_PROJECTS_DIR)
            return

        loop = asyncio.get_running_loop()
        wake = asyncio.Event()

        class _Handler(FileSystemEventHandler):  # type: ignore[misc]
            def on_modified(self, event):
                loop.call_soon_threadsafe(wake.set)
            on_created = on_modified

        self._claude_observer = Observer()
        # recursive=True: each project has its own subdir with conversation JSONL.
        self._claude_observer.schedule(_Handler(), str(CLAUDE_PROJECTS_DIR),
                                        recursive=True)
        self._claude_observer.start()
        log.info("claude fsevents watcher armed on %s", CLAUDE_PROJECTS_DIR)

        try:
            while not self._stopped.is_set():
                try:
                    await asyncio.wait_for(wake.wait(), timeout=10.0)
                except asyncio.TimeoutError:
                    continue
                wake.clear()
                # Debounce: a single Claude turn rewrites the JSONL ~5 times
                # in quick succession. 300ms collapses the burst.
                await asyncio.sleep(0.3)
                wake.clear()
                log.info("claude fsevents -> forcing immediate probe")
                self._refresh.set()
        finally:
            try:
                self._claude_observer.stop()
                self._claude_observer.join(timeout=2.0)
            except Exception:
                pass

    async def _superset_loop(self) -> None:
        while not self._stopped.is_set():
            # Read FIRST, then sleep — keeps tests deterministic at small intervals.
            try:
                state_data = load_superset_state()
                if state_data is not None:
                    focus = read_focus(state_data)
                    focus_changed = focus != self.state.focus
                    if focus_changed:
                        self.state.update_focus(focus)
                    # Diff against last snapshot; any new transitions get
                    # appended to the activity ring buffer.
                    # Use LOCAL epoch (UTC + tz_offset) so the device's HH:MM
                    # renders in the user's wall-clock time — the firmware
                    # has no RTC sync and can't do the offset itself.
                    _t = __import__("time")
                    local_epoch = int(_t.time()) + _t.localtime().tm_gmtoff
                    new_events = self._activity.observe(state_data, local_epoch)
                    if new_events:
                        self.state.update_activity_events(
                            self._activity.events_for_wire())
                    if focus_changed or new_events:
                        self._dirty.set()
            except Exception as e:
                log.warning("superset read failed: %s", e)
            try:
                await asyncio.wait_for(self._stopped.wait(), timeout=self.superset_interval)
            except asyncio.TimeoutError:
                pass

    async def _writer_loop(self) -> None:
        while not self._stopped.is_set():
            await self._dirty.wait()
            if self._stopped.is_set():
                break
            await asyncio.sleep(self.debounce)
            self._dirty.clear()
            payload = self.state.to_payload()
            try:
                await self.writer.write_payload(payload)
            except Exception as e:
                log.warning("write_payload failed: %s", e)

    async def run(self) -> None:
        self.writer.on_refresh = self._on_refresh
        self.writer.on_connected = self._on_connected
        await asyncio.gather(
            self._claude_loop(),
            self._superset_loop(),
            self._writer_loop(),
            self._codex_watcher_loop(),
            self._claude_fs_watcher_loop(),
            self.writer.run(),
            return_exceptions=True,
        )

    def stop(self) -> None:
        self._stopped.set()
        self._dirty.set()
        self._refresh.set()
        self.writer.stop()


def run() -> int:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(levelname)s %(message)s")
    writer = BleWriter()
    orch = Orchestrator(writer=writer)
    try:
        asyncio.run(orch.run())
    except KeyboardInterrupt:
        orch.stop()
    return 0
