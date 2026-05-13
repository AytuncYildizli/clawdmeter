"""Multi-account Claude pool.

Claude Code stores ONE OAuth token at a time in macOS Keychain under service
"Claude Code-credentials". When the user logs out + logs in to a different
account, that single entry is overwritten. So enumerating concurrent accounts
from the Keychain is impossible — only the active one is ever there.

This module solves it by snapshotting tokens as the user rotates between
accounts. State is persisted to ~/.clawdmeter/accounts.json with:
    {
      "<account_id>": {
        "id": "<sha256(refresh_token)[:8]>",
        "access_token": "...",
        "refresh_token": "...",
        "expires_at": <epoch_ms>,
        "subscription_type": "enterprise" | "max" | "pro" | "free",
        "rate_limit_tier": "default_claude_max_5x" | ...,
        "first_seen_epoch": ...,
        "last_seen_epoch": ...,
        "label": "optional human-friendly name"
      }, ...
    }

`account_id` = sha256(refresh_token)[:8]. Stable across access-token rotations
within one account, changes when user re-auths from scratch. Doesn't expose
the secret.

On each Keychain re-read:
1. Compute current entry's account_id
2. If new: add to pool
3. If existing: update access_token/expires_at (token rotation within account)
4. The CURRENT keychain entry's account_id is the "active" account

Probing:
- An account's access_token is good for ~12h (per Claude Code's expires_at).
- We probe the rate-limit endpoint per-account; stale tokens return 401, and
  we mark that account as expired (still in pool, but probe.ok=False).
- Future: OAuth refresh against api.anthropic.com to keep expired tokens alive
  without forcing the user to re-login. Out of scope for v1.
"""
from __future__ import annotations
import hashlib
import json
import logging
import os
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path

log = logging.getLogger("clawdmeter.pool")

POOL_PATH = Path.home() / ".clawdmeter" / "accounts.json"
KEYCHAIN_SERVICE = "Claude Code-credentials"


def _account_id(refresh_token: str) -> str:
    """Stable per-account identifier — first 8 hex chars of sha256(refresh).
    Doesn't reveal the secret; changes only when the user re-auths from scratch."""
    return hashlib.sha256(refresh_token.encode("utf-8")).hexdigest()[:8]


def _keychain_payload(account: str | None = None) -> dict | None:
    """Read one entry from Claude Code-credentials Keychain. If account is None,
    return whichever entry `security` finds first (system-default)."""
    cmd = ["security", "find-generic-password", "-s", KEYCHAIN_SERVICE, "-w"]
    if account:
        cmd[2:2] = ["-a", account]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None
    if result.returncode != 0 or not result.stdout:
        return None
    try:
        return json.loads(result.stdout.strip())
    except json.JSONDecodeError:
        return None


def _current_user_account() -> dict | None:
    """Read the entry tagged with acct=$USER (the live Claude Code account)."""
    user = os.environ.get("USER")
    if user:
        payload = _keychain_payload(user)
        if payload:
            return payload
    return _keychain_payload(None)


@dataclass
class Account:
    id: str
    access_token: str
    refresh_token: str
    expires_at_ms: int
    subscription_type: str = ""
    rate_limit_tier: str = ""
    first_seen_epoch: int = 0
    last_seen_epoch: int = 0
    label: str = ""  # human-friendly, user-editable

    def is_token_expired(self, now_ms: int) -> bool:
        # Treat tokens as expired 60s before their stated expiry to avoid
        # spending a probe on a token that'll be rejected mid-flight.
        return now_ms >= (self.expires_at_ms - 60_000)

    def display_name(self) -> str:
        return self.label or f"acct-{self.id[:4]}"

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "access_token": self.access_token,
            "refresh_token": self.refresh_token,
            "expires_at_ms": self.expires_at_ms,
            "subscription_type": self.subscription_type,
            "rate_limit_tier": self.rate_limit_tier,
            "first_seen_epoch": self.first_seen_epoch,
            "last_seen_epoch": self.last_seen_epoch,
            "label": self.label,
        }

    @classmethod
    def from_dict(cls, d: dict) -> Account:
        return cls(
            id=d["id"],
            access_token=d.get("access_token", ""),
            refresh_token=d.get("refresh_token", ""),
            expires_at_ms=int(d.get("expires_at_ms", 0)),
            subscription_type=d.get("subscription_type", ""),
            rate_limit_tier=d.get("rate_limit_tier", ""),
            first_seen_epoch=int(d.get("first_seen_epoch", 0)),
            last_seen_epoch=int(d.get("last_seen_epoch", 0)),
            label=d.get("label", ""),
        )


def _parse_keychain_payload(payload: dict, now_epoch: int) -> Account | None:
    """Build an Account from a Keychain JSON payload."""
    oauth = payload.get("claudeAiOauth") or {}
    refresh = oauth.get("refreshToken")
    access = oauth.get("accessToken")
    if not refresh or not access:
        return None
    return Account(
        id=_account_id(refresh),
        access_token=access,
        refresh_token=refresh,
        expires_at_ms=int(oauth.get("expiresAt", 0)),
        subscription_type=oauth.get("subscriptionType", ""),
        rate_limit_tier=oauth.get("rateLimitTier", ""),
        first_seen_epoch=now_epoch,
        last_seen_epoch=now_epoch,
        label="",
    )


class AccountPool:
    """Persisted pool of Claude OAuth accounts. Snapshots from Keychain as the
    user rotates between accounts; tracks which is currently active."""

    def __init__(self, path: Path = POOL_PATH) -> None:
        self.path = path
        self.accounts: dict[str, Account] = {}
        self.active_id: str | None = None
        self._load()

    def _load(self) -> None:
        if not self.path.exists():
            return
        try:
            data = json.loads(self.path.read_text())
        except (OSError, json.JSONDecodeError) as e:
            log.warning("pool load failed: %s; starting fresh", e)
            return
        for acct_id, acct_data in (data.get("accounts") or {}).items():
            try:
                self.accounts[acct_id] = Account.from_dict(acct_data)
            except (KeyError, ValueError) as e:
                log.warning("skipping malformed account %s: %s", acct_id, e)
        self.active_id = data.get("active_id")

    def _save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "accounts": {aid: a.to_dict() for aid, a in self.accounts.items()},
            "active_id": self.active_id,
        }
        # Atomic-ish write to avoid corruption on crash mid-write.
        tmp = self.path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(payload, indent=2))
        os.replace(tmp, self.path)
        # Pool file contains tokens; restrict to 0600.
        try:
            os.chmod(self.path, 0o600)
        except OSError:
            pass

    def snapshot_current(self, now_epoch: int | None = None) -> Account | None:
        """Read the currently-active Keychain entry, ingest it into the pool,
        and mark it active. Returns the Account or None if Keychain unreadable."""
        if now_epoch is None:
            now_epoch = int(time.time())
        payload = _current_user_account()
        if not payload:
            return None
        fresh = _parse_keychain_payload(payload, now_epoch)
        if not fresh:
            return None

        existing = self.accounts.get(fresh.id)
        if existing is None:
            # New account — preserve first_seen
            fresh.first_seen_epoch = now_epoch
            self.accounts[fresh.id] = fresh
            log.info("pool: new account %s (subscription=%s)",
                     fresh.display_name(), fresh.subscription_type)
        else:
            # Existing — refresh the access_token / expires_at; keep first_seen + label
            existing.access_token = fresh.access_token
            existing.refresh_token = fresh.refresh_token
            existing.expires_at_ms = fresh.expires_at_ms
            existing.subscription_type = fresh.subscription_type
            existing.rate_limit_tier = fresh.rate_limit_tier
            existing.last_seen_epoch = now_epoch
            fresh = existing

        if self.active_id != fresh.id:
            log.info("pool: active account changed -> %s", fresh.display_name())
            self.active_id = fresh.id

        self._save()
        return fresh

    def get_active(self) -> Account | None:
        if self.active_id is None:
            return None
        return self.accounts.get(self.active_id)

    def list_accounts(self) -> list[Account]:
        """All accounts, with the active one (if any) first."""
        active = self.get_active()
        others = [a for a in self.accounts.values() if a.id != self.active_id]
        # Stable ordering of non-active accounts: by last_seen_epoch desc so the
        # most-recently-active backup account appears first.
        others.sort(key=lambda a: a.last_seen_epoch, reverse=True)
        return ([active] if active else []) + others

    def label(self, acct_id: str, label: str) -> bool:
        if acct_id not in self.accounts:
            return False
        self.accounts[acct_id].label = label
        self._save()
        return True
