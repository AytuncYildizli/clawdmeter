"""Token redaction. Security-critical; do not modify without re-running tests.

Policy (case-insensitive on both keys and the Bearer scheme):
- Keys whose lowered form is in SECRET_KEYS get their string values masked
- Values matching the Bearer scheme (any case) have only their credential redacted
- Non-secret keys pass through unchanged
- Input is never mutated; a new dict is returned
"""
from __future__ import annotations
from typing import Any

# Use lowercased keys — membership tests lowercase the candidate key first.
SECRET_KEYS = {
    "access_token", "refresh_token", "id_token",
    "openai_api_key", "api_key", "token", "tokens", "authorization",
    "cookie", "set-cookie", "password", "client_secret",
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


def _is_secret_key(key: str) -> bool:
    return isinstance(key, str) and key.lower() in SECRET_KEYS


def _redact_bearer(value: str) -> str:
    """If `value` looks like an HTTP auth scheme + credential, redact only the credential.
    Returns the value unchanged if no recognizable scheme is found."""
    if not isinstance(value, str):
        return value
    parts = value.split(" ", 1)
    if len(parts) == 2 and parts[0].lower() == "bearer":
        return f"{parts[0]} {redact_token(parts[1])}"
    # Unknown scheme — redact the whole thing rather than leak it
    return redact_token(value)


def _scrub_value(key: str, value: Any) -> Any:
    if isinstance(value, dict):
        return scrub_dict(value)
    if isinstance(value, list):
        return [_scrub_value(key, v) for v in value]
    if _is_secret_key(key) and isinstance(value, str):
        # authorization header gets scheme-preserving treatment
        if key.lower() == "authorization":
            return _redact_bearer(value)
        return redact_token(value)
    return value


def scrub_dict(data: dict) -> dict:
    """Return a copy of `data` with secrets redacted. Recurses into nested dicts/lists."""
    result: dict = {}
    for k, v in data.items():
        result[k] = _scrub_value(k, v)
    return result
