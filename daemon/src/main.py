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
    """
    try:
        result = subprocess.run(
            ["security", "find-generic-password", "-s", CLAUDE_KEYCHAIN_SERVICE, "-w"],
            capture_output=True, text=True, timeout=5,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None
    if result.returncode != 0 or not result.stdout:
        return None
    try:
        return json.loads(result.stdout.strip())
    except Exception:
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

    def _on_refresh(self) -> None:
        # Called from BleWriter notify thread (asyncio loop's call_soon_threadsafe is the
        # canonical way; for bleak's macOS backend the callback runs on the loop already.)
        self._refresh.set()

    async def _claude_loop(self) -> None:
        token = load_claude_token()
        if token:
            log.info("claude token loaded (%d chars)", len(token))
        else:
            log.warning("no Claude token found — claude.ok stays False. Set "
                        "~/.claude/.credentials.json or grant Python keychain "
                        "access to 'Claude Code-credentials'.")
        while not self._stopped.is_set():
            # Probe FIRST, then wait — so the first poll happens before any
            # external stop() can race the loop.
            if token:
                try:
                    block = probe_claude(token)
                    log.info("claude probe: ok=%s s=%s%% w=%s%% st=%s",
                             block.get("ok"), block.get("s"),
                             block.get("w"), block.get("st"))
                    self.state.update_claude(block)
                    self.state.update_codex(codex_stub())
                    self._dirty.set()
                except Exception as e:
                    log.warning("claude probe failed: %s", e)
            else:
                log.debug("no claude token; skipping probe")
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
