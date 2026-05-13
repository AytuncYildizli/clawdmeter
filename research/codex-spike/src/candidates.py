"""Candidate endpoints to probe for ChatGPT-mode usage data.

Each entry: (slug, method, url, notes).

Slugs are lowercase-kebab and become fixture filenames: e.g.
'conversation-limit' -> fixtures/responses/conversation-limit.json
"""
from __future__ import annotations
from dataclasses import dataclass


@dataclass(frozen=True)
class Candidate:
    slug: str
    method: str
    url: str
    notes: str


CANDIDATES: list[Candidate] = [
    Candidate(
        slug="conversation-limit",
        method="GET",
        url="https://chatgpt.com/backend-api/conversation_limit",
        notes="Top guess: name implies plan limit / usage state",
    ),
    Candidate(
        slug="models-usage",
        method="GET",
        url="https://chatgpt.com/backend-api/models/usage",
        notes="Plausible: matches OpenAI's per-model usage shape",
    ),
    Candidate(
        slug="me",
        method="GET",
        url="https://chatgpt.com/backend-api/me",
        notes="Account info — may include plan + limits",
    ),
    Candidate(
        slug="account-check",
        method="GET",
        url="https://chatgpt.com/backend-api/accounts/check",
        notes="Known account-status endpoint; may carry plan tier",
    ),
    Candidate(
        slug="account-billing",
        method="GET",
        url="https://chatgpt.com/backend-api/accounts/check/v4-2023-04-27",
        notes="Versioned account check — known to return subscription info",
    ),
]
