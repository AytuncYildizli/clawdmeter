# macOS Daemon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the macOS Clawdmeter daemon (Python + bleak) that reads Claude rate-limit utilization, reads Superset focus state, stubs Codex per the spike findings, and writes a single JSON payload to the M5Stack Core 2 device over BLE GATT.

**Architecture:** Three concurrent `asyncio` pollers (Claude probe, Superset state, BLE connection-keeper) feed a shared `State` object. A debounced writer task serializes the merged state to JSON and writes it to the device's RX characteristic on every change. Reuses `redact.py` and `auth.py` from `research/codex-spike/` (lifted into `daemon/`). The daemon is a BLE *central* — it scans for, connects to, and writes to the device advertised as `Clawd Controller`.

**Tech Stack:** Python 3.10+, `bleak >= 0.21`, `watchdog >= 4.0` (optional file-watcher with polling fallback), standard library `asyncio` / `urllib.request` / `json` / `pathlib`.

---

## Spec references

- Source spec: `docs/superpowers/specs/2026-05-13-clawdmeter-core2-port-design.md`
  - § 4 Host target (macOS, Python 3.10+)
  - § 6 Button mapping rules (Superset-state classification → `focus.agent`)
  - § 7 Daemon (the section this plan implements)
  - § 8 BLE protocol (GATT UUIDs, payload shape)
- Prior plan: `docs/superpowers/plans/2026-05-13-codex-probe-research-spike.md` (delivered the auth + redact modules + the Codex-stub finding)
- Spike outcome: `research/codex-spike/REPORT.md` — Codex side ships as `codex.ok=false`, no probe

## Working assumptions (verify in Task 1)

- `~/.claude/.credentials.json` exists with an `accessToken` for the user's Anthropic account
- `~/.codex/auth.json` exists with `auth_mode: chatgpt` and a valid `access_token` — daemon reads this only for the `focus` block (to confirm Codex is signed in), never sends it to a probe
- `~/.superset/app-state.json` exists with `tabsState.{activeTabIds, focusedPaneIds, panes}`
- The M5Stack Core 2 is reachable over BLE and advertises `Clawd Controller` (will be true after firmware plan ships; for this plan, a Python BLE-peripheral mock stands in)
- macOS Bluetooth permission has been granted to the Python binary (Terminal or iTerm) — required before first `bleak` scan succeeds

## File structure

```
daemon/
├── README.md
├── requirements.txt
├── pyproject.toml                       # for editable install + pytest discovery
├── src/
│   ├── __init__.py
│   ├── __main__.py                      # python -m clawdmeter entrypoint
│   ├── redact.py                        # COPIED from research/codex-spike/src/redact.py
│   ├── auth.py                          # COPIED from research/codex-spike/src/auth.py
│   ├── claude_probe.py                  # Anthropic rate-limit probe (port of upstream shell)
│   ├── codex_stub.py                    # always returns codex.ok=false
│   ├── superset_state.py                # reads app-state.json, classifies focused pane
│   ├── state.py                         # State dataclass + payload serializer
│   ├── ble_writer.py                    # bleak central: scan, connect, write RX, subscribe REQ
│   └── main.py                          # asyncio orchestrator (three pollers + writer)
├── fixtures/
│   ├── claude-credentials-sample.json   # synthetic ~/.claude/.credentials.json
│   ├── anthropic-headers-sample.txt     # captured rate-limit header response
│   └── superset-app-state-sample.json   # sanitized snapshot of real app-state.json
├── tests/
│   ├── __init__.py
│   ├── test_claude_probe.py
│   ├── test_codex_stub.py
│   ├── test_superset_state.py
│   ├── test_state.py
│   └── test_ble_writer.py
└── launchd/
    └── sh.clawdmeter.daemon.plist       # auto-start on login
```

## Security rules (carried forward from spike)

1. Never log full OAuth tokens — route through `redact_token` / `__repr__`
2. Captured fixtures must be scrubbed via `scrub_any` / `scrub_text` before commit
3. The daemon never writes to disk anything from network responses — only the BLE payload (no fixture saving in production mode)

---

### Task 1: Daemon workspace setup

**Files:**
- Create: `daemon/README.md`
- Create: `daemon/requirements.txt`
- Create: `daemon/pyproject.toml`
- Create: `daemon/src/__init__.py`
- Create: `daemon/src/__main__.py`
- Create: `daemon/tests/__init__.py`
- Modify: `.gitignore` (add daemon venv and __pycache__)

- [ ] **Step 1: Create directory tree**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
mkdir -p daemon/src daemon/tests daemon/fixtures daemon/launchd
touch daemon/src/__init__.py daemon/tests/__init__.py
```

- [ ] **Step 2: Write `daemon/requirements.txt`**

```
bleak>=0.21
watchdog>=4.0
pytest>=8.0
```

- [ ] **Step 3: Write `daemon/pyproject.toml`**

```toml
[project]
name = "clawdmeter"
version = "0.1.0"
description = "macOS daemon that pushes Claude rate-limit and Superset focus state to a Clawdmeter BLE device"
requires-python = ">=3.10"

[tool.pytest.ini_options]
testpaths = ["tests"]
python_files = ["test_*.py"]
addopts = "-ra -q"

[tool.setuptools.packages.find]
where = ["src"]
```

- [ ] **Step 4: Write a minimal `daemon/src/__main__.py`**

```python
"""Entrypoint: `python -m clawdmeter`. The real orchestrator lives in main.py."""
from .main import run

if __name__ == "__main__":
    raise SystemExit(run())
```

(`main.run` doesn't exist yet — it lands in Task 9. For now this file just declares the entry shape.)

- [ ] **Step 5: Append to project-root `.gitignore`**

```
# Daemon
daemon/.venv/
daemon/__pycache__/
daemon/**/__pycache__/
daemon/*.egg-info/
```

- [ ] **Step 6: Write `daemon/README.md`** (operational doc):

```markdown
# Clawdmeter Daemon (macOS)

Reads Claude rate-limit utilization + Superset focus state, writes JSON to the
Clawdmeter device over BLE. Codex side is a stub per
`research/codex-spike/REPORT.md`.

## Setup

​```bash
cd daemon
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
pip install -e .
​```

## Run tests

​```bash
pytest
​```

## Run the daemon

​```bash
python -m clawdmeter
​```

First run will trigger macOS Bluetooth permission prompt — accept it.

## Auto-start

​```bash
cp launchd/sh.clawdmeter.daemon.plist ~/Library/LaunchAgents/
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/sh.clawdmeter.daemon.plist
​```
```

(Triple-backticks in the README content use a zero-width-space when embedded in this plan to avoid parser confusion; write real triple-backticks in the file itself.)

- [ ] **Step 7: Verify pytest discovers the empty test dir**

```bash
cd daemon && python3 -m pytest --collect-only
```

Expected: exit code 5 ("no tests collected"), no errors.

- [ ] **Step 8: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add daemon/ .gitignore
git commit -m "daemon: scaffold macOS daemon workspace"
```

---

### Task 2: Port redact + auth from the spike

**Files:**
- Create: `daemon/src/redact.py` (copy from `research/codex-spike/src/redact.py`)
- Create: `daemon/src/auth.py` (copy from `research/codex-spike/src/auth.py`)
- Create: `daemon/fixtures/auth-sample.json` (copy from spike)
- Create: `daemon/tests/test_redact.py` (copy from spike, adapt imports)
- Create: `daemon/tests/test_auth.py` (copy from spike, adapt imports)

- [ ] **Step 1: Copy redact.py and auth.py verbatim**

```bash
cp research/codex-spike/src/redact.py daemon/src/redact.py
cp research/codex-spike/src/auth.py daemon/src/auth.py
cp research/codex-spike/fixtures/auth-sample.json daemon/fixtures/auth-sample.json
cp research/codex-spike/tests/test_redact.py daemon/tests/test_redact.py
cp research/codex-spike/tests/test_auth.py daemon/tests/test_auth.py
```

- [ ] **Step 2: Adapt imports**

Tests import `from src.redact import ...` and `from src.auth import ...`. In the daemon they should import `from clawdmeter.redact import ...` and `from clawdmeter.auth import ...` (matching the `pyproject.toml` package name).

```bash
cd daemon
sed -i '' 's|from src\.redact|from clawdmeter.redact|g' tests/test_redact.py tests/test_auth.py src/auth.py
sed -i '' 's|from src\.auth|from clawdmeter.auth|g' tests/test_auth.py
sed -i '' 's|from \.redact|from clawdmeter.redact|g' src/auth.py
```

(After this, `daemon/src/auth.py` imports `from clawdmeter.redact import redact_token` instead of `from .redact`.)

- [ ] **Step 3: Install the package in editable mode**

```bash
cd daemon && source .venv/bin/activate && pip install -e .
```

Expected: `Successfully installed clawdmeter-0.1.0`.

- [ ] **Step 4: Run the tests**

```bash
cd daemon && python -m pytest -v
```

Expected: 28 passed (17 redact + 11 auth).

If any fail because of import paths, fix them — sed may have missed an edge case.

- [ ] **Step 5: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add daemon/src/redact.py daemon/src/auth.py daemon/fixtures/auth-sample.json daemon/tests/test_redact.py daemon/tests/test_auth.py
git commit -m "daemon: port redact + auth from spike (28 tests pass)"
```

---

### Task 3: Claude probe (Python port of upstream shell)

**Files:**
- Create: `daemon/src/claude_probe.py`
- Create: `daemon/fixtures/anthropic-200-headers.json` (captured headers, no body needed)
- Create: `daemon/tests/test_claude_probe.py`

Upstream shell daemon parses `anthropic-ratelimit-unified-{5h,7d}-{utilization,reset,status}` headers. We replicate that in Python with a clean parse function, mock the network in tests.

- [ ] **Step 1: Capture a synthetic header fixture**

Write `daemon/fixtures/anthropic-200-headers.json`:

```json
{
  "anthropic-ratelimit-unified-5h-utilization": "0.71",
  "anthropic-ratelimit-unified-5h-reset": "1747144800",
  "anthropic-ratelimit-unified-7d-utilization": "0.38",
  "anthropic-ratelimit-unified-7d-reset": "1747625400",
  "anthropic-ratelimit-unified-5h-status": "allow",
  "content-type": "application/json"
}
```

- [ ] **Step 2: Write the failing test**

Write `daemon/tests/test_claude_probe.py`:

```python
import json
from pathlib import Path
from unittest.mock import patch
import pytest
from clawdmeter.claude_probe import (
    parse_anthropic_headers,
    probe_claude,
    ClaudeResult,
    ClaudeProbeError,
)

FIXTURES = Path(__file__).parent.parent / "fixtures"


def test_parse_anthropic_headers_extracts_utilization():
    headers = json.loads((FIXTURES / "anthropic-200-headers.json").read_text())
    # Treat "now" as fixed for deterministic reset-minute computation
    out = parse_anthropic_headers(headers, now_epoch=1747140000)
    assert out["s"] == 71  # 0.71 * 100
    assert out["w"] == 38
    # 5h reset is at 1747144800, now is 1747140000 -> 4800s -> 80 min
    assert out["sr"] == 80
    # 7d reset is at 1747625400 -> 485400s -> 8090 min
    assert out["wr"] == 8090
    assert out["st"] == "allow"
    assert out["ok"] is True


def test_parse_anthropic_headers_handles_negative_reset():
    """Reset times in the past should clamp to 0 minutes."""
    headers = {
        "anthropic-ratelimit-unified-5h-utilization": "0.5",
        "anthropic-ratelimit-unified-5h-reset": "1000",      # very old
        "anthropic-ratelimit-unified-7d-utilization": "0.2",
        "anthropic-ratelimit-unified-7d-reset": "1000",
        "anthropic-ratelimit-unified-5h-status": "allow",
    }
    out = parse_anthropic_headers(headers, now_epoch=2000)
    assert out["sr"] == 0
    assert out["wr"] == 0


def test_parse_anthropic_headers_missing_fields_returns_zeros():
    out = parse_anthropic_headers({}, now_epoch=1747140000)
    assert out["s"] == 0
    assert out["sr"] == 0
    assert out["w"] == 0
    assert out["wr"] == 0
    assert out["st"] == "unknown"
    assert out["ok"] is False


def test_probe_claude_returns_ok_result():
    headers = json.loads((FIXTURES / "anthropic-200-headers.json").read_text())

    def fake_request(method, url, headers_in, body):
        assert method == "POST"
        assert url == "https://api.anthropic.com/v1/messages"
        assert headers_in["Authorization"].startswith("Bearer ")
        assert "anthropic-beta" in headers_in
        return (200, headers, b'{"id":"msg_x"}')

    with patch("clawdmeter.claude_probe._do_request", fake_request):
        result = probe_claude(token="fake-anthropic-token", now_epoch=1747140000)

    assert result["ok"] is True
    assert result["s"] == 71


def test_probe_claude_network_error_returns_ok_false():
    def boom(m, u, h, b):
        raise OSError("network down")

    with patch("clawdmeter.claude_probe._do_request", boom):
        result = probe_claude(token="fake", now_epoch=1747140000)

    assert result["ok"] is False
    assert result["st"] == "unknown"
```

- [ ] **Step 3: Run the test, verify it fails**

```bash
cd daemon && python -m pytest tests/test_claude_probe.py -v
```

Expected: `ModuleNotFoundError: No module named 'clawdmeter.claude_probe'`.

- [ ] **Step 4: Implement `daemon/src/claude_probe.py`**

```python
"""Claude rate-limit probe — Python port of upstream Linux shell daemon.

Issues a single POST to api.anthropic.com/v1/messages with max_tokens=1,
parses anthropic-ratelimit-unified-{5h,7d}-* headers, returns the payload
shape required by the BLE protocol spec.
"""
from __future__ import annotations
import json
import time
import urllib.request
import urllib.error
from typing import Any


class ClaudeProbeError(Exception):
    pass


class ClaudeResult(dict):
    """Type alias for the {s, sr, w, wr, st, ok} dict; subclassing dict
    avoids a separate dataclass while keeping the type signal."""


def _coerce_pct(value: str | None) -> int:
    if value is None:
        return 0
    try:
        return int(round(float(value) * 100))
    except (ValueError, TypeError):
        return 0


def _coerce_reset_minutes(reset_epoch: str | None, now_epoch: int) -> int:
    if reset_epoch is None:
        return 0
    try:
        delta = int(reset_epoch) - now_epoch
        return max(0, delta // 60)
    except (ValueError, TypeError):
        return 0


def parse_anthropic_headers(headers: dict[str, str], now_epoch: int) -> ClaudeResult:
    """Parse rate-limit headers into the BLE payload shape."""
    # Headers may arrive case-mixed depending on HTTP lib; normalize once.
    norm = {k.lower(): v for k, v in headers.items()}

    s5_util = norm.get("anthropic-ratelimit-unified-5h-utilization")
    s5_reset = norm.get("anthropic-ratelimit-unified-5h-reset")
    s7_util = norm.get("anthropic-ratelimit-unified-7d-utilization")
    s7_reset = norm.get("anthropic-ratelimit-unified-7d-reset")
    status = norm.get("anthropic-ratelimit-unified-5h-status", "unknown")

    # If neither timeframe header is present, the probe is "unknown"
    has_any = any([s5_util, s5_reset, s7_util, s7_reset])

    return ClaudeResult({
        "s": _coerce_pct(s5_util),
        "sr": _coerce_reset_minutes(s5_reset, now_epoch),
        "w": _coerce_pct(s7_util),
        "wr": _coerce_reset_minutes(s7_reset, now_epoch),
        "st": status if has_any else "unknown",
        "ok": has_any,
    })


def _do_request(method: str, url: str, headers: dict[str, str], body: bytes) -> tuple[int, dict[str, str], bytes]:
    """Network round-trip. Patched out in tests."""
    req = urllib.request.Request(url, method=method, headers=headers, data=body)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.status, dict(resp.headers), resp.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def probe_claude(token: str, now_epoch: int | None = None) -> ClaudeResult:
    """One probe round-trip. Returns the BLE payload shape regardless of error."""
    if now_epoch is None:
        now_epoch = int(time.time())

    headers = {
        "Authorization": f"Bearer {token}",
        "anthropic-version": "2023-06-01",
        "anthropic-beta": "oauth-2025-04-20",
        "Content-Type": "application/json",
        "User-Agent": "claude-code/2.1.5",
    }
    body = json.dumps({
        "model": "claude-haiku-4-5-20251001",
        "max_tokens": 1,
        "messages": [{"role": "user", "content": "hi"}],
    }).encode("utf-8")

    try:
        status, resp_headers, _raw = _do_request("POST", "https://api.anthropic.com/v1/messages", headers, body)
    except Exception:
        return ClaudeResult({"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unknown", "ok": False})

    if status >= 400:
        return ClaudeResult({"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unknown", "ok": False})

    return parse_anthropic_headers(resp_headers, now_epoch)
```

- [ ] **Step 5: Run tests, verify 5 pass**

```bash
cd daemon && python -m pytest tests/test_claude_probe.py -v
```

Expected: 5 passed.

- [ ] **Step 6: Run full suite**

```bash
cd daemon && python -m pytest -v
```

Expected: 33 passed (17 redact + 11 auth + 5 claude_probe).

- [ ] **Step 7: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add daemon/src/claude_probe.py daemon/tests/test_claude_probe.py daemon/fixtures/anthropic-200-headers.json
git commit -m "daemon: Claude rate-limit probe (Python port of upstream shell)"
```

---

### Task 4: Codex stub

**Files:**
- Create: `daemon/src/codex_stub.py`
- Create: `daemon/tests/test_codex_stub.py`

Trivial per the spike's REPORT.md — Codex always returns `ok=false`. Encoded as a function so the orchestrator can call it like the Claude probe.

- [ ] **Step 1: Write the failing test**

Write `daemon/tests/test_codex_stub.py`:

```python
from clawdmeter.codex_stub import codex_stub


def test_codex_stub_always_returns_ok_false():
    result = codex_stub()
    assert result == {"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unavailable", "ok": False}


def test_codex_stub_is_idempotent():
    """Stub takes no args, returns the same value on each call."""
    assert codex_stub() == codex_stub()
```

- [ ] **Step 2: Run the test, verify it fails**

```bash
cd daemon && python -m pytest tests/test_codex_stub.py -v
```

Expected: `ModuleNotFoundError: No module named 'clawdmeter.codex_stub'`.

- [ ] **Step 3: Implement**

Write `daemon/src/codex_stub.py`:

```python
"""Codex stub. Per research/codex-spike/REPORT.md (2026-05-13), there is no
ChatGPT-mode endpoint that exposes plan utilization to a CLI-auth token.
Daemon returns ok=false on every call; firmware renders '—' on the
Codex screen for utilization.
"""
from __future__ import annotations


def codex_stub() -> dict:
    return {
        "s": 0,
        "sr": 0,
        "w": 0,
        "wr": 0,
        "st": "unavailable",
        "ok": False,
    }
```

- [ ] **Step 4: Run tests, verify 2 pass**

```bash
cd daemon && python -m pytest tests/test_codex_stub.py -v
```

- [ ] **Step 5: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add daemon/src/codex_stub.py daemon/tests/test_codex_stub.py
git commit -m "daemon: Codex stub (no probe per spike findings)"
```

---

### Task 5: Superset state reader

**Files:**
- Create: `daemon/src/superset_state.py`
- Create: `daemon/fixtures/superset-app-state-sample.json`
- Create: `daemon/tests/test_superset_state.py`

Reads `~/.superset/app-state.json`, finds the focused pane, classifies the agent, extracts the repo, counts other active agent sessions.

- [ ] **Step 1: Write the synthetic Superset state fixture**

Write `daemon/fixtures/superset-app-state-sample.json`:

```json
{
  "tabsState": {
    "activeTabIds": {
      "ws-1": "tab-claude-rotator",
      "ws-2": "tab-codex-katman"
    },
    "focusedPaneIds": {
      "tab-claude-rotator": "pane-claude-rotator",
      "tab-codex-katman": "pane-codex-katman",
      "tab-misc": "pane-terminal"
    },
    "panes": {
      "pane-claude-rotator": {
        "id": "pane-claude-rotator",
        "tabId": "tab-claude-rotator",
        "type": "terminal",
        "name": "⠂ Claude Code",
        "status": "running",
        "cwd": "/Users/test/.superset/worktrees/rotator/main",
        "cwdConfirmed": true
      },
      "pane-codex-katman": {
        "id": "pane-codex-katman",
        "tabId": "tab-codex-katman",
        "type": "terminal",
        "name": "Codex",
        "status": "running",
        "cwd": "/Users/test/.superset/worktrees/katman/feature",
        "cwdConfirmed": true
      },
      "pane-terminal": {
        "id": "pane-terminal",
        "tabId": "tab-misc",
        "type": "terminal",
        "name": "Terminal",
        "status": "idle",
        "cwd": "/Users/test/projects/other",
        "cwdConfirmed": true
      },
      "pane-claude-idle": {
        "id": "pane-claude-idle",
        "tabId": "tab-misc",
        "type": "terminal",
        "name": "✳ Claude Code",
        "status": "idle",
        "cwd": "/Users/test/.superset/worktrees/whip/feature",
        "cwdConfirmed": true
      },
      "pane-claude-busy": {
        "id": "pane-claude-busy",
        "tabId": "tab-misc",
        "type": "terminal",
        "name": "⠂ Claude Code",
        "status": "running",
        "cwd": "/Users/test/.superset/worktrees/atlas-book/main",
        "cwdConfirmed": true
      }
    }
  }
}
```

- [ ] **Step 2: Write the failing test**

Write `daemon/tests/test_superset_state.py`:

```python
import json
from pathlib import Path
import pytest
from clawdmeter.superset_state import (
    FocusInfo,
    classify_agent,
    extract_repo,
    read_focus,
)

FIXTURES = Path(__file__).parent.parent / "fixtures"


def test_classify_agent_recognizes_claude():
    assert classify_agent("⠂ Claude Code") == "claude"
    assert classify_agent("✳ Claude Code") == "claude"
    assert classify_agent("Claude Code") == "claude"


def test_classify_agent_recognizes_codex():
    assert classify_agent("Codex") == "codex"
    assert classify_agent("⠂ Codex") == "codex"
    assert classify_agent("codex") == "codex"  # case-insensitive


def test_classify_agent_other_is_none():
    assert classify_agent("Terminal") == "none"
    assert classify_agent("") == "none"
    assert classify_agent(None) == "none"


def test_extract_repo_from_worktree_path():
    # ~/.superset/worktrees/<project>/<branch> → "<project>"
    assert extract_repo("/Users/x/.superset/worktrees/rotator/main") == "rotator"
    assert extract_repo("/Users/x/.superset/worktrees/katman/feature/sub") == "katman"


def test_extract_repo_outside_worktrees_uses_basename():
    assert extract_repo("/Users/x/projects/other") == "other"
    assert extract_repo("/") == ""
    assert extract_repo(None) == ""


def test_read_focus_picks_first_active_tab():
    state = json.loads((FIXTURES / "superset-app-state-sample.json").read_text())
    focus = read_focus(state)
    # First activeTabIds entry is ws-1 -> tab-claude-rotator
    # Whose focused pane is pane-claude-rotator (Claude Code, rotator worktree)
    assert focus.agent == "claude"
    assert focus.repo == "rotator"
    # Other active agent panes: pane-codex-katman (running) + pane-claude-busy (running)
    # pane-claude-idle is idle so excluded. Total other = 2.
    assert focus.sessions == 2


def test_read_focus_empty_state_returns_none():
    focus = read_focus({"tabsState": {"activeTabIds": {}, "focusedPaneIds": {}, "panes": {}}})
    assert focus.agent == "none"
    assert focus.repo == ""
    assert focus.sessions == 0


def test_focus_info_to_dict():
    f = FocusInfo(agent="claude", repo="rotator", sessions=2)
    assert f.to_dict() == {"agent": "claude", "repo": "rotator", "sessions": 2}
```

- [ ] **Step 3: Run, verify it fails**

```bash
cd daemon && python -m pytest tests/test_superset_state.py -v
```

Expected: `ModuleNotFoundError: No module named 'clawdmeter.superset_state'`.

- [ ] **Step 4: Implement**

Write `daemon/src/superset_state.py`:

```python
"""Reader for ~/.superset/app-state.json. Classifies the focused pane's agent,
extracts its worktree-derived repo name, and counts other active agent panes.
"""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
from typing import Literal

AgentKind = Literal["claude", "codex", "none"]


@dataclass(frozen=True)
class FocusInfo:
    agent: AgentKind
    repo: str
    sessions: int

    def to_dict(self) -> dict:
        return {"agent": self.agent, "repo": self.repo, "sessions": self.sessions}


def classify_agent(pane_name: str | None) -> AgentKind:
    if not isinstance(pane_name, str):
        return "none"
    n = pane_name.lower()
    if "claude code" in n:
        return "claude"
    if "codex" in n:
        return "codex"
    return "none"


def extract_repo(cwd: str | None) -> str:
    if not isinstance(cwd, str) or not cwd:
        return ""
    p = Path(cwd)
    parts = p.parts
    # Look for ".superset/worktrees/<project>/..." anywhere in the path
    for i, part in enumerate(parts):
        if part == "worktrees" and i + 1 < len(parts):
            # The next part after "worktrees" is the project name
            return parts[i + 1]
    # Fall back to basename
    return p.name


def read_focus(app_state: dict) -> FocusInfo:
    tabs_state = app_state.get("tabsState", {})
    active_tab_ids = tabs_state.get("activeTabIds", {})
    focused_pane_ids = tabs_state.get("focusedPaneIds", {})
    panes = tabs_state.get("panes", {})

    # Pick the first active tab. (User's app may have multiple workspaces;
    # the daemon follows whichever workspace is first in insertion order.
    # If multi-workspace becomes a real concern, the daemon can take a flag
    # to filter by workspace ID; not a v1 problem.)
    if not active_tab_ids:
        return FocusInfo(agent="none", repo="", sessions=0)

    first_tab_id = next(iter(active_tab_ids.values()))
    focused_pane_id = focused_pane_ids.get(first_tab_id)
    if not focused_pane_id or focused_pane_id not in panes:
        return FocusInfo(agent="none", repo="", sessions=0)

    pane = panes[focused_pane_id]
    agent = classify_agent(pane.get("name"))
    repo = extract_repo(pane.get("cwd"))

    # Count OTHER agent panes that are not idle and not the focused one
    sessions = sum(
        1
        for pid, p in panes.items()
        if pid != focused_pane_id
        and classify_agent(p.get("name")) in ("claude", "codex")
        and p.get("status") != "idle"
    )

    return FocusInfo(agent=agent, repo=repo, sessions=sessions)
```

- [ ] **Step 5: Run, verify 7 tests pass**

```bash
cd daemon && python -m pytest tests/test_superset_state.py -v
```

- [ ] **Step 6: Full suite**

```bash
cd daemon && python -m pytest -v
```

Expected: 42 passed (17 + 11 + 5 + 2 + 7).

- [ ] **Step 7: Commit**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
git add daemon/src/superset_state.py daemon/tests/test_superset_state.py daemon/fixtures/superset-app-state-sample.json
git commit -m "daemon: Superset focus reader"
```

---

### Task 6: State aggregator + payload serializer

**Files:**
- Create: `daemon/src/state.py`
- Create: `daemon/tests/test_state.py`

Combines the three sources (Claude, Codex, focus) into a single BLE payload dict. Provides a `State` container with `update_claude / update_codex / update_focus` methods and a `to_payload()` serializer matching spec § 8.2.

- [ ] **Step 1: Write the failing test**

Write `daemon/tests/test_state.py`:

```python
import json
import pytest
from clawdmeter.state import State
from clawdmeter.superset_state import FocusInfo


def test_state_starts_with_safe_defaults():
    s = State()
    payload = s.to_payload()
    assert payload["claude"]["ok"] is False
    assert payload["codex"]["ok"] is False
    assert payload["focus"] == {"agent": "none", "repo": "", "sessions": 0}


def test_state_update_claude_replaces_block():
    s = State()
    s.update_claude({"s": 71, "sr": 80, "w": 38, "wr": 8090, "st": "allow", "ok": True})
    payload = s.to_payload()
    assert payload["claude"]["s"] == 71
    assert payload["claude"]["ok"] is True


def test_state_update_codex_replaces_block():
    s = State()
    s.update_codex({"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unavailable", "ok": False})
    payload = s.to_payload()
    assert payload["codex"]["st"] == "unavailable"


def test_state_update_focus_replaces_block():
    s = State()
    s.update_focus(FocusInfo(agent="claude", repo="rotator", sessions=2))
    payload = s.to_payload()
    assert payload["focus"] == {"agent": "claude", "repo": "rotator", "sessions": 2}


def test_payload_serializes_under_400_bytes():
    """BLE MTU target — the payload must fit comfortably."""
    s = State()
    s.update_claude({"s": 71, "sr": 80, "w": 38, "wr": 8090, "st": "allow", "ok": True})
    s.update_codex({"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unavailable", "ok": False})
    s.update_focus(FocusInfo(agent="claude", repo="rotator-with-a-fairly-long-name", sessions=5))
    encoded = json.dumps(s.to_payload()).encode("utf-8")
    assert len(encoded) < 400


def test_payload_keys_match_spec():
    s = State()
    payload = s.to_payload()
    assert set(payload.keys()) == {"claude", "codex", "focus"}
    assert set(payload["claude"].keys()) == {"s", "sr", "w", "wr", "st", "ok"}
    assert set(payload["codex"].keys()) == {"s", "sr", "w", "wr", "st", "ok"}
    assert set(payload["focus"].keys()) == {"agent", "repo", "sessions"}
```

- [ ] **Step 2: Run, verify it fails**

```bash
cd daemon && python -m pytest tests/test_state.py -v
```

Expected: `ModuleNotFoundError: No module named 'clawdmeter.state'`.

- [ ] **Step 3: Implement**

Write `daemon/src/state.py`:

```python
"""Shared State container. Each poller writes its block; the writer task
serializes to JSON and sends over BLE.

Thread-safety: caller-owned. In the asyncio orchestrator, all updates and
reads happen on the same event loop, so no locking is needed. If this ever
crosses threads, wrap mutations in a Lock.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from .superset_state import FocusInfo


_CLAUDE_DEFAULT = {"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unknown", "ok": False}
_CODEX_DEFAULT = {"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unavailable", "ok": False}
_FOCUS_DEFAULT = FocusInfo(agent="none", repo="", sessions=0)


@dataclass
class State:
    claude: dict = field(default_factory=lambda: dict(_CLAUDE_DEFAULT))
    codex: dict = field(default_factory=lambda: dict(_CODEX_DEFAULT))
    focus: FocusInfo = field(default_factory=lambda: _FOCUS_DEFAULT)

    def update_claude(self, block: dict) -> None:
        self.claude = dict(block)

    def update_codex(self, block: dict) -> None:
        self.codex = dict(block)

    def update_focus(self, focus: FocusInfo) -> None:
        self.focus = focus

    def to_payload(self) -> dict:
        return {
            "claude": dict(self.claude),
            "codex": dict(self.codex),
            "focus": self.focus.to_dict(),
        }
```

- [ ] **Step 4: Run, verify 6 tests pass**

```bash
cd daemon && python -m pytest tests/test_state.py -v
```

- [ ] **Step 5: Full suite**

Expected: 48 passed.

- [ ] **Step 6: Commit**

```bash
git add daemon/src/state.py daemon/tests/test_state.py
git commit -m "daemon: state container + payload serializer"
```

---

### Task 7: BLE writer (bleak central)

**Files:**
- Create: `daemon/src/ble_writer.py`
- Create: `daemon/tests/test_ble_writer.py`

This is the trickiest task. The daemon is a BLE central: scans for `Clawd Controller`, connects, writes JSON payloads to the RX characteristic (`4c41555a-4465-7669-6365-000000000002`), subscribes to REQ characteristic notifications (`4c41555a-4465-7669-6365-000000000004`) to receive refresh requests from the device.

Tests use `unittest.mock.AsyncMock` to fake the bleak interface; no real BLE during testing. The integration test in Task 11 exercises real BLE against a Python BLE peripheral simulator.

- [ ] **Step 1: Write the failing test**

Write `daemon/tests/test_ble_writer.py`:

```python
import asyncio
import json
from unittest.mock import AsyncMock, MagicMock, patch
import pytest
from clawdmeter.ble_writer import (
    BleWriter,
    SERVICE_UUID,
    RX_CHAR_UUID,
    REQ_CHAR_UUID,
    DEVICE_NAME,
)


def test_ble_uuids_match_spec():
    """Spec § 8.1 — these UUIDs are the public contract with the firmware."""
    assert SERVICE_UUID == "4c41555a-4465-7669-6365-000000000001"
    assert RX_CHAR_UUID == "4c41555a-4465-7669-6365-000000000002"
    assert REQ_CHAR_UUID == "4c41555a-4465-7669-6365-000000000004"
    assert DEVICE_NAME == "Clawd Controller"


@pytest.mark.asyncio
async def test_writer_serializes_and_writes_payload():
    """The writer must JSON-encode the payload and call write_gatt_char on RX."""
    mock_client = MagicMock()
    mock_client.write_gatt_char = AsyncMock()
    mock_client.is_connected = True

    writer = BleWriter()
    writer._client = mock_client  # injected for testing

    payload = {"claude": {"s": 71, "ok": True}, "codex": {"ok": False}, "focus": {"agent": "claude"}}
    await writer.write_payload(payload)

    mock_client.write_gatt_char.assert_awaited_once()
    args, kwargs = mock_client.write_gatt_char.call_args
    # First positional or kwarg: the characteristic UUID
    char_arg = args[0] if args else kwargs.get("char_specifier")
    data_arg = args[1] if len(args) > 1 else kwargs.get("data")
    assert str(char_arg) == RX_CHAR_UUID
    assert json.loads(data_arg.decode("utf-8")) == payload


@pytest.mark.asyncio
async def test_writer_skips_when_disconnected():
    """If no client is connected, write_payload must not raise."""
    writer = BleWriter()
    writer._client = None
    # Should silently no-op
    await writer.write_payload({"any": "payload"})


@pytest.mark.asyncio
async def test_writer_refresh_callback_invoked_on_req_notify():
    """When the device writes to REQ characteristic, our callback fires."""
    writer = BleWriter()
    callback_called = asyncio.Event()
    writer.on_refresh = lambda: callback_called.set()

    # Simulate bleak invoking the notify handler
    writer._handle_req_notify(sender=MagicMock(), data=b"refresh")

    # Give event loop a tick to process
    await asyncio.sleep(0)
    assert callback_called.is_set()
```

The test file requires `pytest-asyncio` — add it to `daemon/requirements.txt`.

- [ ] **Step 2: Add pytest-asyncio**

Append to `daemon/requirements.txt`:

```
pytest-asyncio>=0.23
```

Add to `daemon/pyproject.toml` under `[tool.pytest.ini_options]`:

```toml
asyncio_mode = "auto"
```

Install: `pip install -r requirements.txt`.

- [ ] **Step 3: Run test, verify it fails**

```bash
cd daemon && python -m pytest tests/test_ble_writer.py -v
```

Expected: `ModuleNotFoundError`.

- [ ] **Step 4: Implement `daemon/src/ble_writer.py`**

```python
"""BLE central — scans for, connects to, and writes payloads to the
Clawdmeter device. Subscribes to REQ characteristic for refresh requests.

The class is designed for asyncio orchestration: `run()` is a long-running
coroutine that handles scan/connect/keep-alive, while `write_payload()`
is called by the orchestrator's debounced writer task.
"""
from __future__ import annotations
import asyncio
import json
import logging
from typing import Callable
from bleak import BleakClient, BleakScanner

# Spec § 8.1 — public contract with firmware. Do not change without firmware coordination.
SERVICE_UUID = "4c41555a-4465-7669-6365-000000000001"
RX_CHAR_UUID = "4c41555a-4465-7669-6365-000000000002"
REQ_CHAR_UUID = "4c41555a-4465-7669-6365-000000000004"
DEVICE_NAME = "Clawd Controller"

log = logging.getLogger("clawdmeter.ble")


class BleWriter:
    def __init__(self) -> None:
        self._client: BleakClient | None = None
        self._stopped = asyncio.Event()
        self.on_refresh: Callable[[], None] | None = None

    async def write_payload(self, payload: dict) -> None:
        """Write the JSON-encoded payload to the RX characteristic.
        Silently no-ops if not connected — the orchestrator's connection loop
        will reconnect and the next write will go through."""
        client = self._client
        if client is None or not client.is_connected:
            return
        data = json.dumps(payload).encode("utf-8")
        try:
            await client.write_gatt_char(RX_CHAR_UUID, data)
        except Exception as e:
            log.warning("write failed: %s", e)
            # Force a reconnect on next loop iteration
            await self._disconnect()

    def _handle_req_notify(self, sender, data: bytes) -> None:
        """Called by bleak when the device writes to REQ characteristic.
        Fires `on_refresh` callback so the orchestrator can trigger pollers."""
        if self.on_refresh is not None:
            self.on_refresh()

    async def _disconnect(self) -> None:
        if self._client is not None:
            try:
                await self._client.disconnect()
            except Exception:
                pass
            self._client = None

    async def _scan_and_connect(self) -> bool:
        """Returns True if connected, False if scan timed out / no device."""
        log.info("scanning for %r...", DEVICE_NAME)
        device = await BleakScanner.find_device_by_filter(
            lambda d, ad: d.name == DEVICE_NAME, timeout=15
        )
        if device is None:
            return False

        log.info("connecting to %s", device.address)
        client = BleakClient(device)
        await client.connect()
        await client.start_notify(REQ_CHAR_UUID, self._handle_req_notify)
        self._client = client
        log.info("connected")
        return True

    async def run(self) -> None:
        """Long-running task. Maintains connection; reconnects on drop."""
        backoff = 1
        while not self._stopped.is_set():
            try:
                if self._client is None or not self._client.is_connected:
                    ok = await self._scan_and_connect()
                    if not ok:
                        await asyncio.sleep(backoff)
                        backoff = min(60, backoff * 2)
                        continue
                    backoff = 1
                # Heartbeat — sleep a bit, then check still connected
                await asyncio.sleep(5)
            except Exception as e:
                log.warning("ble loop error: %s", e)
                await self._disconnect()
                await asyncio.sleep(backoff)
                backoff = min(60, backoff * 2)

    def stop(self) -> None:
        self._stopped.set()
```

- [ ] **Step 5: Run, verify 4 tests pass**

```bash
cd daemon && python -m pytest tests/test_ble_writer.py -v
```

If `pytest-asyncio` complains about `asyncio_mode`, double-check `pyproject.toml` has the setting under `[tool.pytest.ini_options]`.

- [ ] **Step 6: Full suite**

Expected: 52 passed.

- [ ] **Step 7: Commit**

```bash
git add daemon/src/ble_writer.py daemon/tests/test_ble_writer.py daemon/requirements.txt daemon/pyproject.toml
git commit -m "daemon: BLE central (bleak) with reconnect + REQ subscription"
```

---

### Task 8: Main asyncio orchestrator

**Files:**
- Create: `daemon/src/main.py`
- Create: `daemon/tests/test_main.py`

Wires three poller tasks (Claude every 60s, Codex stub on every Claude tick, Superset every 2s) to a shared `State`, debounces writes through `BleWriter`. Listens for REQ refresh requests via `BleWriter.on_refresh` and triggers an immediate Claude probe.

Test scope: orchestration logic only (intervals, debounce, refresh trigger). Real network and real BLE are mocked.

- [ ] **Step 1: Write the failing test**

Write `daemon/tests/test_main.py`:

```python
import asyncio
import json
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch
import pytest
from clawdmeter.main import Orchestrator
from clawdmeter.state import State


@pytest.mark.asyncio
async def test_orchestrator_initial_payload_contains_all_blocks():
    """After one tick of each poller, the BleWriter must receive a payload
    with claude/codex/focus blocks populated."""
    sent_payloads = []

    async def fake_write(payload):
        sent_payloads.append(payload)

    writer = MagicMock()
    writer.write_payload = AsyncMock(side_effect=fake_write)
    writer.run = AsyncMock()  # never connects in this test
    writer.on_refresh = None
    writer.stop = MagicMock()

    fake_claude_block = {"s": 50, "sr": 60, "w": 25, "wr": 4000, "st": "allow", "ok": True}
    fake_focus = MagicMock()
    fake_focus.to_dict.return_value = {"agent": "claude", "repo": "rotator", "sessions": 1}

    with patch("clawdmeter.main.probe_claude", return_value=fake_claude_block), \
         patch("clawdmeter.main.read_focus", return_value=fake_focus), \
         patch("clawdmeter.main.load_claude_token", return_value="fake-token"):
        orch = Orchestrator(writer=writer, poll_interval=0.01, debounce=0.01)
        # Run for ~30ms — long enough for one full poll + write
        task = asyncio.create_task(orch.run())
        await asyncio.sleep(0.05)
        orch.stop()
        await asyncio.sleep(0.02)
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass

    assert len(sent_payloads) >= 1
    payload = sent_payloads[-1]
    assert payload["claude"]["s"] == 50
    assert payload["codex"]["ok"] is False
    assert payload["focus"]["agent"] == "claude"


@pytest.mark.asyncio
async def test_refresh_callback_triggers_immediate_poll():
    """When BleWriter fires on_refresh, the orchestrator polls Claude again."""
    poll_count = 0

    def counting_probe(token, now_epoch=None):
        nonlocal poll_count
        poll_count += 1
        return {"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unknown", "ok": False}

    writer = MagicMock()
    writer.write_payload = AsyncMock()
    writer.run = AsyncMock()
    writer.stop = MagicMock()
    writer.on_refresh = None  # orchestrator will set this

    with patch("clawdmeter.main.probe_claude", side_effect=counting_probe), \
         patch("clawdmeter.main.read_focus", return_value=MagicMock(to_dict=lambda: {"agent": "none", "repo": "", "sessions": 0})), \
         patch("clawdmeter.main.load_claude_token", return_value="t"):
        orch = Orchestrator(writer=writer, poll_interval=10.0, debounce=0.01)
        task = asyncio.create_task(orch.run())
        await asyncio.sleep(0.05)
        # poll_count should be 1 (initial)
        baseline = poll_count
        # Trigger refresh
        writer.on_refresh()
        await asyncio.sleep(0.05)
        # poll_count should now be at least baseline + 1
        assert poll_count > baseline
        orch.stop()
        await asyncio.sleep(0.02)
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass
```

- [ ] **Step 2: Run, verify it fails**

```bash
cd daemon && python -m pytest tests/test_main.py -v
```

Expected: `ModuleNotFoundError`.

- [ ] **Step 3: Implement `daemon/src/main.py`**

```python
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
from pathlib import Path
from .ble_writer import BleWriter
from .claude_probe import probe_claude
from .codex_stub import codex_stub
from .superset_state import read_focus, FocusInfo
from .state import State

log = logging.getLogger("clawdmeter.main")

CLAUDE_CREDS_PATH = Path.home() / ".claude" / ".credentials.json"
SUPERSET_STATE_PATH = Path.home() / ".superset" / "app-state.json"


def load_claude_token() -> str | None:
    if not CLAUDE_CREDS_PATH.exists():
        return None
    try:
        data = json.loads(CLAUDE_CREDS_PATH.read_text())
    except Exception:
        return None
    return data.get("accessToken") or data.get("access_token")


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
        while not self._stopped.is_set():
            if token:
                block = probe_claude(token)
                self.state.update_claude(block)
                self.state.update_codex(codex_stub())
                self._dirty.set()
            # Wait for next interval OR a refresh request
            try:
                await asyncio.wait_for(self._refresh.wait(), timeout=self.poll_interval)
                self._refresh.clear()
            except asyncio.TimeoutError:
                pass

    async def _superset_loop(self) -> None:
        while not self._stopped.is_set():
            state_data = load_superset_state()
            if state_data is not None:
                focus = read_focus(state_data)
                if focus != self.state.focus:
                    self.state.update_focus(focus)
                    self._dirty.set()
            try:
                await asyncio.wait_for(self._stopped.wait(), timeout=self.superset_interval)
            except asyncio.TimeoutError:
                pass

    async def _writer_loop(self) -> None:
        while not self._stopped.is_set():
            await self._dirty.wait()
            await asyncio.sleep(self.debounce)
            self._dirty.clear()
            payload = self.state.to_payload()
            await self.writer.write_payload(payload)

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
```

- [ ] **Step 4: Run, verify 2 tests pass**

```bash
cd daemon && python -m pytest tests/test_main.py -v
```

- [ ] **Step 5: Full suite**

Expected: 54 passed.

- [ ] **Step 6: Smoke-run the daemon (no BLE peer)**

```bash
cd daemon && timeout 10 python -m clawdmeter 2>&1 | head -20
```

Expected output: log lines showing "scanning for 'Clawd Controller'" and Claude probe attempts. Will not connect (no peer); will not crash. Use Ctrl-C if `timeout` doesn't kill it.

- [ ] **Step 7: Commit**

```bash
git add daemon/src/main.py daemon/tests/test_main.py
git commit -m "daemon: asyncio orchestrator (pollers + writer + refresh handler)"
```

---

### Task 9: launchd plist

**Files:**
- Create: `daemon/launchd/sh.clawdmeter.daemon.plist`

For auto-start on login. Out of scope: codesigning, notarization.

- [ ] **Step 1: Write the plist**

Write `daemon/launchd/sh.clawdmeter.daemon.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>sh.clawdmeter.daemon</string>
    <key>ProgramArguments</key>
    <array>
        <string>/bin/sh</string>
        <string>-c</string>
        <string>cd /Users/aytuncyildizli/.superset/projects/clawdmeter/daemon &amp;&amp; ./.venv/bin/python -m clawdmeter</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/tmp/clawdmeter.out.log</string>
    <key>StandardErrorPath</key>
    <string>/tmp/clawdmeter.err.log</string>
</dict>
</plist>
```

(The hardcoded user path is acceptable for a v1 personal daemon. Future: generate from a template.)

- [ ] **Step 2: Validate plist syntax**

```bash
plutil -lint daemon/launchd/sh.clawdmeter.daemon.plist
```

Expected: `daemon/launchd/sh.clawdmeter.daemon.plist: OK`.

- [ ] **Step 3: Commit**

```bash
git add daemon/launchd/sh.clawdmeter.daemon.plist
git commit -m "daemon: launchd plist for auto-start on login"
```

---

### Task 10: Integration smoke — real BLE peripheral simulator

**Files:**
- Create: `daemon/tests/integration/__init__.py`
- Create: `daemon/tests/integration/peripheral_sim.py`
- Create: `daemon/tests/integration/test_e2e.py`

End-to-end test using a Python BLE peripheral simulator (the `bumble` library or simple `bleak`-compatible counterpart) that advertises as `Clawd Controller` on a virtual adapter. Validates the full stack: scan, connect, write, receive REQ notify.

**This task is optional for v1.** If `bumble` setup proves complicated on macOS, defer to firmware-side integration testing when the device exists.

- [ ] **Step 1: Decision point**

Run `pip install bumble` and verify it can advertise on macOS:

```bash
pip install bumble && python -c "from bumble.device import Device; print('ok')"
```

If installation succeeds AND a quick local test (1-line advertise) works without a real BLE adapter (bumble has a virtual transport): proceed to Step 2.

If installation fails, or virtual-BLE proves flaky on macOS: skip this task and rely on firmware-side integration in the firmware plan. Document the skip in `daemon/README.md`.

- [ ] **Step 2: Write the peripheral simulator**

(Implementer-discretion. The simulator's job is to advertise as `Clawd Controller`, expose the RX + REQ characteristics, log every write it receives, and optionally fire a REQ notify on demand. Use `bumble.host` + `bumble.transport` + a virtual transport. Reference: https://github.com/google/bumble.)

- [ ] **Step 3: Write the e2e test**

(Implementer-discretion. The test starts the simulator, starts the daemon Orchestrator, asserts the simulator receives at least one write within 10 seconds, asserts the write is valid JSON with the expected keys, then teardown.)

- [ ] **Step 4: Commit**

```bash
git add daemon/tests/integration/
git commit -m "daemon: e2e integration smoke against bumble peripheral simulator"
```

---

### Task 11: Final verification + push

- [ ] **Step 1: Full test suite green**

```bash
cd daemon && python -m pytest -v
```

Expected: 54 passed (or 54 + integration count if Task 10 landed).

- [ ] **Step 2: Smoke-run daemon with real Superset + Claude state**

```bash
cd daemon && python -m clawdmeter 2>&1 | head -30
```

Expected: log lines showing Claude probe success, Superset focus updates, BLE scan attempts (will fail to find device — that's fine; firmware doesn't exist yet).

- [ ] **Step 3: Secret leak grep**

```bash
cd /Users/aytuncyildizli/.superset/projects/clawdmeter
grep -rE "eyJ[A-Za-z0-9_-]{20,}" daemon/ --include='*.py' --include='*.json' --include='*.md' | grep -v "test_\|fixtures/auth-sample.json" || echo "CLEAN"
```

Expected: `CLEAN` (the only `eyJ`-shaped strings are synthetic test tokens).

- [ ] **Step 4: Push the branch**

```bash
git push origin port/core2-macos-codex
```

- [ ] **Step 5: Report**

Read `daemon/README.md` and confirm the setup instructions are accurate. Hand back to user with the branch state.

---

## Self-review checklist

- [x] Every task has explicit file paths
- [x] Every code step shows full code
- [x] Every test step shows test code + expected outcome
- [x] Every command has the exact invocation + expected output
- [x] Security guardrails carried from spike (redact, scrub_any)
- [x] Codex stub matches REPORT.md recommendation
- [x] BLE UUIDs match spec § 8.1
- [x] Payload shape matches spec § 8.2
- [x] Hand-off to firmware plan: BLE contract is fully specified

## Out of scope for this plan

- Firmware (Plan #3)
- launchd codesigning / notarization
- Per-user installation script (the plist hardcodes a path; a Makefile target could template it later)
- Token refresh for Claude or Codex (both probes use the current token; refresh is a separate concern if it ever bites)
- Linux portability (spec § 4 declares macOS-only)
- Asynchronous Anthropic HTTP via `httpx` — `urllib.request` in a thread executor is sufficient for one POST every 60s
