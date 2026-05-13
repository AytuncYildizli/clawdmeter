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
