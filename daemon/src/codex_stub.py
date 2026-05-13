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
