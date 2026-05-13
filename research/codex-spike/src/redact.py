"""Token redaction. Security-critical; do not modify without re-running tests."""
from __future__ import annotations
from copy import deepcopy
from typing import Any

SECRET_KEYS = {
    "access_token", "refresh_token", "id_token", "OPENAI_API_KEY",
    "api_key", "token", "tokens", "Authorization",
}


def redact_token(value: str | None) -> str:
    """Mask a token, keeping enough to identify it without exposing it."""
    if value is None:
        return "<none>"
    if not isinstance(value, str):
        return "<non-str>"
    if len(value) < 12:
        return "***"
    return f"{value[:6]}…{value[-4:]}"


def _scrub_value(key: str, value: Any) -> Any:
    if isinstance(value, dict):
        return scrub_dict(value)
    if isinstance(value, list):
        return [_scrub_value(key, v) for v in value]
    if key in SECRET_KEYS and isinstance(value, str):
        # Authorization header gets special treatment: preserve scheme
        if key == "Authorization" and value.startswith("Bearer "):
            return f"Bearer {redact_token(value[len('Bearer '):])}"
        return redact_token(value)
    return value


def scrub_dict(data: dict) -> dict:
    """Return a copy of `data` with secrets redacted. Recurses into nested dicts/lists."""
    result: dict = {}
    for k, v in data.items():
        result[k] = _scrub_value(k, v)
    return result
