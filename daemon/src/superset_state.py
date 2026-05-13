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
