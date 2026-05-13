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
