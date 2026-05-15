"""Tests for the activity tracker — diff-based event generation."""
from clawdmeter.activity import (
    ActivityTracker,
    ActivityEvent,
    VERB_ADDED,
    VERB_REMOVED,
    VERB_STARTED,
    VERB_STOPPED,
)


def _state_with_panes(panes: dict) -> dict:
    return {"tabsState": {"panes": panes}}


def _pane(name: str, status: str, cwd: str = "/Users/x/.superset/worktrees/rotator/main") -> dict:
    return {"name": name, "status": status, "cwd": cwd}


def test_empty_state_emits_nothing():
    t = ActivityTracker()
    assert t.observe(_state_with_panes({}), now_epoch=1000) == []
    assert t.events() == []


def test_new_agent_pane_emits_added():
    t = ActivityTracker()
    t.observe(_state_with_panes({}), 1000)
    out = t.observe(_state_with_panes({"p1": _pane("Claude Code", "idle")}), 1010)
    assert len(out) == 1
    assert out[0].verb == VERB_ADDED
    assert out[0].agent == "claude"
    assert out[0].repo == "rotator"
    assert out[0].ts_epoch == 1010


def test_pane_status_idle_to_running_emits_started():
    t = ActivityTracker()
    t.observe(_state_with_panes({"p1": _pane("Claude Code", "idle")}), 1000)
    out = t.observe(_state_with_panes({"p1": _pane("Claude Code", "running")}), 1010)
    assert len(out) == 1
    assert out[0].verb == VERB_STARTED


def test_pane_status_running_to_idle_emits_stopped():
    t = ActivityTracker()
    t.observe(_state_with_panes({"p1": _pane("Claude Code", "running")}), 1000)
    out = t.observe(_state_with_panes({"p1": _pane("Claude Code", "idle")}), 1010)
    assert len(out) == 1
    assert out[0].verb == VERB_STOPPED


def test_pane_disappears_emits_removed():
    t = ActivityTracker()
    t.observe(_state_with_panes({"p1": _pane("Claude Code", "running")}), 1000)
    out = t.observe(_state_with_panes({}), 1010)
    assert len(out) == 1
    assert out[0].verb == VERB_REMOVED


def test_non_agent_panes_ignored():
    """Terminal/Browser panes shouldn't generate activity feed noise."""
    t = ActivityTracker()
    out = t.observe(_state_with_panes({
        "p1": _pane("Terminal", "running"),
        "p2": {"name": "Some Browser", "status": "idle", "cwd": "/"},
    }), 1000)
    assert out == []


def test_ring_buffer_caps_at_maxlen():
    t = ActivityTracker(maxlen=3)
    # Fire 5 transitions; only last 3 survive.
    snaps = [
        {"p1": _pane("Claude Code", "idle")},          # start: add p1
        {"p1": _pane("Claude Code", "running")},       # idle -> running
        {"p1": _pane("Claude Code", "idle")},          # running -> idle
        {"p1": _pane("Claude Code", "running")},       # idle -> running again
        {"p1": _pane("Claude Code", "idle")},          # running -> idle again
    ]
    for i, s in enumerate(snaps):
        t.observe(_state_with_panes(s), 1000 + i)
    evs = t.events()
    assert len(evs) == 3
    # Newest-first ordering
    assert evs[0].ts_epoch == 1004
    assert evs[1].ts_epoch == 1003
    assert evs[2].ts_epoch == 1002


def test_wire_format_is_compact():
    """Each event serializes to 4 short keys for MTU efficiency."""
    ev = ActivityEvent(ts_epoch=1700000000, verb=VERB_STARTED, agent="claude", repo="rotator")
    wire = ev.to_wire()
    assert wire == {"t": 1700000000, "v": ">", "a": "c", "r": "rotator"}


def test_wire_truncates_long_repo_names():
    ev = ActivityEvent(ts_epoch=1, verb=VERB_ADDED, agent="codex",
                       repo="some-very-long-project-name-here")
    wire = ev.to_wire()
    assert len(wire["r"]) <= 12


def test_status_unchanged_emits_nothing():
    t = ActivityTracker()
    t.observe(_state_with_panes({"p1": _pane("Claude Code", "running")}), 1000)
    out = t.observe(_state_with_panes({"p1": _pane("Claude Code", "running")}), 1010)
    assert out == []


def test_multiple_panes_independent_diffs():
    t = ActivityTracker()
    t.observe(_state_with_panes({
        "p1": _pane("Claude Code", "idle", cwd="/Users/x/.superset/worktrees/rotator/main"),
        "p2": _pane("Codex", "running", cwd="/Users/x/.superset/worktrees/katman/main"),
    }), 1000)
    out = t.observe(_state_with_panes({
        "p1": _pane("Claude Code", "running", cwd="/Users/x/.superset/worktrees/rotator/main"),
        "p2": _pane("Codex", "idle", cwd="/Users/x/.superset/worktrees/katman/main"),
    }), 1010)
    # Two transitions, one per pane
    assert len(out) == 2
    verbs = {(e.agent, e.verb) for e in out}
    assert ("claude", VERB_STARTED) in verbs
    assert ("codex", VERB_STOPPED) in verbs
