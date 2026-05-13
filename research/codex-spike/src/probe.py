"""Probe runner. Two responsibilities:
1. `probe_one(candidate, token)` — issue one request, return structured result
2. `save_capture(result, dir)` — write a SCRUBBED capture to fixtures/

CLI: `python -m src.probe --capture` iterates all CANDIDATES against the
live ~/.codex/auth.json and saves captures for manual inspection.
"""
from __future__ import annotations
import argparse
import json
import sys
import urllib.request
import urllib.error
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any
from .auth import load_auth
from .candidates import CANDIDATES, Candidate
from .redact import scrub_any, scrub_text


@dataclass
class ProbeResult:
    slug: str
    status: int | None
    headers: dict[str, str] = field(default_factory=dict)
    body: dict | None = None
    raw_body: str | None = None
    error: str | None = None


def _do_request(method: str, url: str, headers: dict[str, str]) -> tuple[int, dict[str, str], bytes]:
    """Network round-trip. Patched out in tests."""
    req = urllib.request.Request(url, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.status, dict(resp.headers), resp.read()
    except urllib.error.HTTPError as e:
        # HTTPError has the same interface as the response
        return e.code, dict(e.headers), e.read()


def probe_one(candidate: Candidate, token: str) -> ProbeResult:
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/json",
        "User-Agent": "clawdmeter-spike/0.1 (research)",
    }
    try:
        status, resp_headers, raw = _do_request(candidate.method, candidate.url, headers)
    except Exception as e:
        return ProbeResult(slug=candidate.slug, status=None, error=str(e))

    body: dict | None = None
    raw_body: str | None = None
    try:
        body = json.loads(raw.decode("utf-8"))
    except Exception:
        raw_body = raw.decode("utf-8", errors="replace")[:4000]

    return ProbeResult(
        slug=candidate.slug,
        status=status,
        headers={k: v for k, v in resp_headers.items()},
        body=body,
        raw_body=raw_body,
        error=None,
    )


def save_capture(result: ProbeResult, out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{result.slug}.json"
    payload = asdict(result)
    # Scrub before writing — every field that can carry secrets.
    # - headers: dict, may contain Authorization etc.
    # - body: arbitrary JSON shape (dict, list, scalar)
    # - raw_body: freeform text (HTML, plain-text 401/403). Token-shaped
    #   substrings get regex-masked. The 4000-char cap on raw_body is a
    #   readability limit, NOT a security control — JWTs fit in <4000.
    payload["headers"] = scrub_any(payload["headers"])
    payload["body"] = scrub_any(payload["body"])
    if payload["raw_body"] is not None:
        payload["raw_body"] = scrub_text(payload["raw_body"])
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True))
    return out_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", action="store_true", help="Run live and save fixtures")
    parser.add_argument("--auth", default=str(Path.home() / ".codex" / "auth.json"))
    parser.add_argument("--out", default=str(Path(__file__).parent.parent / "fixtures" / "responses"))
    args = parser.parse_args()

    if not args.capture:
        print("--capture required for live probe", file=sys.stderr)
        return 2

    auth = load_auth(Path(args.auth))
    print(f"Loaded auth: {auth!r}")
    if not auth.access_token:
        print("No access_token in auth.json — cannot probe", file=sys.stderr)
        return 1

    out_dir = Path(args.out)
    for c in CANDIDATES:
        print(f"\n→ {c.slug}  {c.method} {c.url}")
        result = probe_one(c, auth.access_token)
        path = save_capture(result, out_dir)
        if result.error:
            print(f"  error: {result.error}")
        else:
            print(f"  status={result.status}  body_keys={list(result.body.keys()) if isinstance(result.body, dict) else 'non-json'}")
        print(f"  capture: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
