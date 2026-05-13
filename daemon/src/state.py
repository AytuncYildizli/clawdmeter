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
