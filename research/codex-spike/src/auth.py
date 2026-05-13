"""Loader for ~/.codex/auth.json. Refuses to operate on non-ChatGPT modes
(the daemon's spec only covers ChatGPT-mode probing)."""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import json
from .redact import redact_token


class AuthError(Exception):
    """Raised when auth state is missing, malformed, or unsupported."""


@dataclass
class CodexAuth:
    mode: str
    access_token: str | None
    refresh_token: str | None
    id_token: str | None
    last_refresh: str | None

    def __repr__(self) -> str:
        return (
            f"CodexAuth(mode={self.mode!r}, "
            f"access_token={redact_token(self.access_token)}, "
            f"refresh_token={redact_token(self.refresh_token)}, "
            f"id_token={redact_token(self.id_token)}, "
            f"last_refresh={self.last_refresh!r})"
        )


def load_auth(path: Path) -> CodexAuth:
    if not path.exists():
        raise AuthError(f"auth file not found: {path}")
    try:
        data = json.loads(path.read_text())
    except json.JSONDecodeError as e:
        raise AuthError(f"failed to parse auth file: {e}") from e

    mode = data.get("auth_mode")
    if mode != "ChatGPT":
        raise AuthError(f"unsupported auth_mode={mode!r}; this spike only handles ChatGPT")

    tokens = data.get("tokens") or {}
    return CodexAuth(
        mode=mode,
        access_token=tokens.get("access_token"),
        refresh_token=tokens.get("refresh_token"),
        id_token=tokens.get("id_token"),
        last_refresh=data.get("last_refresh"),
    )
