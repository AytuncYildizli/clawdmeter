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
from .codex_probe import probe_codex
from .account_pool import AccountPool, Account
from .superset_state import read_focus, FocusInfo
from .state import State

log = logging.getLogger("clawdmeter.main")

CLAUDE_CREDS_PATH = Path.home() / ".claude" / ".credentials.json"
CLAUDE_KEYCHAIN_SERVICE = "Claude Code-credentials"
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

    async def _superset_loop(self) -> None:
        while not self._stopped.is_set():
            # Read FIRST, then sleep — keeps tests deterministic at small intervals.
            try:
                state_data = load_superset_state()
                if state_data is not None:
                    focus = read_focus(state_data)
                    if focus != self.state.focus:
                        self.state.update_focus(focus)
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
