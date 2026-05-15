"""Activity feed — diff consecutive Superset state snapshots into a ring of
human-readable events.

The daemon already polls `~/.superset/app-state.json` every 2 seconds for
focus. This module piggybacks: every poll, we compare the new panes dict to
the previous one and emit events for state transitions:

  - pane appeared (new agent session started)         "+ rotator/claude"
  - pane disappeared (terminal closed)                "- rotator/claude"
  - status idle -> running (started working)          "> katman/codex"
  - status running -> idle (finished or waiting)      "= rotator/claude"

Events are stored in a fixed-size ring (`maxlen=8` by default). The daemon
ships them in the BLE payload as `activity_events` so the firmware can render
a 3rd swipe page (Claude | Codex | Activity).

Why a ring buffer instead of unbounded?
  - The BLE MTU caps payload size; 8 events x ~25 bytes = 200B fits cleanly.
  - The device only has room to render ~6 lines anyway.
  - "Activity" feed is point-in-time UX, not an audit log.
"""
from __future__ import annotations
from collections import deque
from dataclasses import dataclass
from .superset_state import classify_agent, extract_repo

# Event verbs — kept to 1-char so the firmware can render them as colored
# bullets without needing a wide column. Mapping:
#   "+" = new pane appeared
#   "-" = pane closed
#   ">" = idle -> running (started)
#   "=" = running -> idle (finished/waiting)
VERB_ADDED = "+"
VERB_REMOVED = "-"
VERB_STARTED = ">"
VERB_STOPPED = "="

# Cap on events shipped over BLE. The Bluetooth Core Spec hard-limits a
# single GATT characteristic value to 512 bytes (BLE_ATT_ATTR_MAX_LEN);
# this is enforced by mynewt-nimble in esp-idf and CAN'T be bumped per-
# characteristic without a chunked protocol. With base ~430B plus 2
# accounts (~190B), we have ~50B for activity events. One event ~30-40B.
# Shipping 1 keeps writes reliable; the ring buffer holds more in memory
# for the next protocol expansion (chunked writes).
MAX_EVENTS = 1


@dataclass(frozen=True)
class ActivityEvent:
    ts_epoch: int        # wall-clock seconds, for HH:MM rendering on device
    verb: str            # one of the VERB_* constants above
    agent: str           # "claude" | "codex" | "none"
    repo: str            # short repo/project name

    def to_wire(self) -> dict:
        # Wire-compact: device parses these keys.
        # "t" = ts_epoch, "v" = verb, "a" = agent (one char: c/x/n), "r" = repo
        # Agent compressed to one char to save MTU bytes.
        return {
            "t": self.ts_epoch,
            "v": self.verb,
            "a": {"claude": "c", "codex": "x"}.get(self.agent, "n"),
            "r": self.repo[:12],  # repo name truncated for wire compactness
        }


def _summarize_panes(state: dict) -> dict[str, tuple[str, str, str]]:
    """Reduce a Superset state snapshot to {pane_id: (agent, repo, status)}.
    Drops non-agent panes (Terminal, browser tabs, etc) so they don't pollute
    the activity feed with noise."""
    tabs_state = state.get("tabsState", {}) if state else {}
    panes = tabs_state.get("panes", {}) if tabs_state else {}
    out: dict[str, tuple[str, str, str]] = {}
    for pid, pane in (panes or {}).items():
        agent = classify_agent(pane.get("name"))
        if agent == "none":
            continue  # only track agent panes
        repo = extract_repo(pane.get("cwd"))
        status = pane.get("status", "") or ""
        out[pid] = (agent, repo, status)
    return out


class ActivityTracker:
    """Snapshot-diff event generator. Call observe(state) every Superset poll;
    events() returns the most-recent MAX_EVENTS in newest-first order."""

    def __init__(self, maxlen: int = MAX_EVENTS) -> None:
        self._events: deque[ActivityEvent] = deque(maxlen=maxlen)
        self._prev: dict[str, tuple[str, str, str]] = {}

    def observe(self, state: dict, now_epoch: int) -> list[ActivityEvent]:
        """Diff `state` against the previous snapshot. Returns the events
        appended this tick (may be empty)."""
        curr = _summarize_panes(state)
        new_events: list[ActivityEvent] = []

        # Removed panes (pane id was there before, gone now)
        for pid, (agent, repo, _status) in self._prev.items():
            if pid not in curr:
                new_events.append(ActivityEvent(
                    ts_epoch=now_epoch, verb=VERB_REMOVED, agent=agent, repo=repo))

        # Added panes (new pane id)
        for pid, (agent, repo, status) in curr.items():
            if pid in self._prev:
                continue
            new_events.append(ActivityEvent(
                ts_epoch=now_epoch, verb=VERB_ADDED, agent=agent, repo=repo))
            # If the new pane is born "running", that's also a START event —
            # but conflating with ADDED keeps the feed less noisy. Skip.

        # Status transitions on persistent panes
        for pid, (agent, repo, status) in curr.items():
            prev = self._prev.get(pid)
            if not prev:
                continue
            _prev_agent, _prev_repo, prev_status = prev
            if status == prev_status:
                continue
            if prev_status == "idle" and status == "running":
                new_events.append(ActivityEvent(
                    ts_epoch=now_epoch, verb=VERB_STARTED, agent=agent, repo=repo))
            elif prev_status == "running" and status == "idle":
                new_events.append(ActivityEvent(
                    ts_epoch=now_epoch, verb=VERB_STOPPED, agent=agent, repo=repo))
            # Other transitions (e.g. status="waiting") aren't surfaced today;
            # we treat anything that isn't running as effectively idle.

        for ev in new_events:
            self._events.append(ev)
        self._prev = curr
        return new_events

    def events(self) -> list[ActivityEvent]:
        """Most-recent-first list of events for the wire payload."""
        return list(reversed(self._events))

    def events_for_wire(self) -> list[dict]:
        return [e.to_wire() for e in self.events()]
