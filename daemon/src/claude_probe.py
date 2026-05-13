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
