"""Codex rate-limit probe — workaround discovered 2026-05-13 after spike.

The original spike (research/codex-spike/REPORT.md) concluded that Codex CLI's
ChatGPT OAuth tokens don't authenticate against any usage endpoint we could
find. That's still true for live API probing.

BUT: Codex CLI itself receives `codex.rate_limits` events over its websocket
session and writes them to `~/.codex/logs_2.sqlite`. Each Codex turn emits a
log entry containing the EXACT rate-limit shape we want:

    {"type":"codex.rate_limits","plan_type":"pro","rate_limits":{
        "allowed": true,
        "primary":   {"used_percent": 10, "window_minutes": 300,   "reset_at": ...},
        "secondary": {"used_percent": 18, "window_minutes": 10080, "reset_at": ...}
    }}

`primary` is the 5-hour window, `secondary` is the 7-day window — symmetric
with Anthropic's unified rate-limit headers. This module tails the SQLite log
and returns the most-recent event (or ok=False if no recent data).

Caveats:
- Data is only fresh after the user actively uses Codex. Stale entries return
  ok=False after the freshness window (default 24h).
- We open the SQLite in read-only mode to avoid contention with the Codex CLI
  process if it's running.
- No network access — pure local file read.
"""
from __future__ import annotations
import json
import logging
import re
import sqlite3
import time
from pathlib import Path

log = logging.getLogger("clawdmeter.codex")

CODEX_LOG_PATH = Path.home() / ".codex" / "logs_2.sqlite"
DEFAULT_FRESHNESS_SECONDS = 24 * 3600  # 24h


_RATE_LIMIT_ANCHOR = '{"type":"codex.rate_limits"'


def _extract_rate_limit_json(body: str) -> dict | None:
    """Pull the codex.rate_limits JSON object out of a tracing log line.

    The log line is a long tracing string with the event JSON embedded mid-text.
    We anchor on the type field and use json.JSONDecoder.raw_decode to find the
    balanced-brace end automatically (regex can't match nested braces correctly).
    """
    start = body.find(_RATE_LIMIT_ANCHOR)
    if start < 0:
        return None
    try:
        obj, _ = json.JSONDecoder().raw_decode(body[start:])
        return obj
    except json.JSONDecodeError:
        return None


def _to_payload_block(event: dict, now_epoch: int) -> dict:
    """Map a codex.rate_limits event to the BLE payload shape {s,sr,w,wr,st,ok}."""
    rate_limits = event.get("rate_limits") or {}
    primary = rate_limits.get("primary") or {}
    secondary = rate_limits.get("secondary") or {}
    allowed = rate_limits.get("allowed", False)

    def _reset_minutes(block: dict) -> int:
        # Prefer absolute reset_at (epoch seconds); fall back to reset_after_seconds.
        reset_at = block.get("reset_at")
        if isinstance(reset_at, (int, float)):
            return max(0, int((reset_at - now_epoch) // 60))
        delta = block.get("reset_after_seconds")
        if isinstance(delta, (int, float)):
            return max(0, int(delta // 60))
        return 0

    return {
        "s": int(primary.get("used_percent") or 0),
        "sr": _reset_minutes(primary),
        "w": int(secondary.get("used_percent") or 0),
        "wr": _reset_minutes(secondary),
        "st": "allowed" if allowed else "limited",
        "ok": True,
    }


def probe_codex(now_epoch: int | None = None,
                freshness_seconds: int = DEFAULT_FRESHNESS_SECONDS) -> dict:
    """Read the most-recent codex.rate_limits event from Codex CLI's local SQLite.

    Returns the BLE payload shape. ok=True if a fresh event was found, else
    ok=False with st="unavailable" (mirrors the stub fallback).
    """
    if now_epoch is None:
        now_epoch = int(time.time())

    unavailable = {"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unavailable", "ok": False}

    if not CODEX_LOG_PATH.exists():
        log.debug("codex log not found at %s", CODEX_LOG_PATH)
        return unavailable

    # Open read-only; URI form prevents lock acquisition.
    uri = f"file:{CODEX_LOG_PATH}?mode=ro"
    try:
        conn = sqlite3.connect(uri, uri=True, timeout=2.0)
    except sqlite3.OperationalError as e:
        log.warning("codex log open failed: %s", e)
        return unavailable

    try:
        # Codex CLI emits TWO kinds of rate-limit log rows:
        # (1) the actual websocket event with the JSON payload —
        #     contains literal `"type":"codex.rate_limits"`
        # (2) a meta telemetry row with `event.kind=codex.rate_limits`
        #     and no JSON body.
        # We want (1). Match on the JSON-shaped substring.
        cur = conn.execute(
            "SELECT ts, feedback_log_body FROM logs "
            "WHERE feedback_log_body LIKE '%\"type\":\"codex.rate_limits\"%' "
            "ORDER BY ts DESC LIMIT 1"
        )
        row = cur.fetchone()
    except sqlite3.OperationalError as e:
        log.warning("codex log query failed: %s", e)
        return unavailable
    finally:
        conn.close()

    if not row:
        log.debug("no codex.rate_limits events in log")
        return unavailable

    ts, body = row
    age_seconds = now_epoch - int(ts // 1000) if ts > 10**12 else now_epoch - int(ts)
    if age_seconds > freshness_seconds:
        log.info("codex rate-limit data stale (%ds old, threshold %ds)",
                 age_seconds, freshness_seconds)
        return unavailable

    event = _extract_rate_limit_json(body)
    if event is None:
        log.warning("could not extract rate-limit JSON from log body")
        return unavailable

    block = _to_payload_block(event, now_epoch)
    log.info("codex probe: ok=%s s=%s%% w=%s%% st=%s age=%ds",
             block["ok"], block["s"], block["w"], block["st"], age_seconds)
    return block
