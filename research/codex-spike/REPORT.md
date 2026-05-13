# Codex Probe Spike — Findings

**Date:** 2026-05-13
**Spike duration:** ~2 hours (scaffolding through live probe + report)
**Outcome:** **NO WORKING ENDPOINT FOUND — STUB FALLBACK RECOMMENDED**
**Branch:** `port/core2-macos-codex`
**Spec gated:** `docs/superpowers/specs/2026-05-13-clawdmeter-core2-port-design.md` § 7.4 (Codex probe)
**Plan executed:** `docs/superpowers/plans/2026-05-13-codex-probe-research-spike.md`

## Summary

The Codex CLI stores OAuth-flow tokens in `~/.codex/auth.json` (with `auth_mode=chatgpt`). Those tokens authenticate fine against ChatGPT's `/me` identity endpoint (proven: 200 OK with full account info), but **every candidate usage/limit endpoint either does not exist, requires a different HTTP method, or is blocked at the edge with a generic HTML 403 page**.

The structural reason is clear: `chatgpt.com/backend-api/*` is a browser-session API, secured by the `__Secure-next-auth.session-token` cookie that the ChatGPT web UI sets after login. Codex CLI's OAuth `access_token` lives in a different auth realm — it's designed for `api.openai.com/v1/*` endpoints, not `chatgpt.com/backend-api/*`. The two auth surfaces are not interchangeable.

**Recommendation:** Ship Codex side of the device as a stub. Daemon writes `codex.ok = false` in every BLE payload; firmware renders "—" on the Codex screen for utilization. The device still functions fully as a Claude-only meter, and the Codex screen still exists for swipe symmetry (just shows no-data state). The dual-screen UI design and the daemon plan can both proceed as designed; only the Codex probe path becomes a no-op.

## What was tried

| Slug | URL | Method | Status | Outcome |
|---|---|---|---|---|
| `conversation-limit` | `https://chatgpt.com/backend-api/conversation_limit` | GET | **403** | Generic branded HTML 403 page — edge-layer block (Cloudflare / OpenAI's auth gateway), not an API error. Bearer token did not substitute for session cookie. |
| `models-usage` | `https://chatgpt.com/backend-api/models/usage` | GET | **404** | FastAPI/Starlette `{"detail":"Not Found"}` — endpoint does not exist at this path. |
| `me` | `https://chatgpt.com/backend-api/me` | GET | **200** | Returned full account/identity info: `object, id, email, name, picture, created, phone_number, mfa_flag_enabled, has_payg_project_spend_limit, amr, client_id, orgs, country, region…`. **No usage/limit/quota/rate fields present.** Token validity is proven by this response. |
| `account-check` | `https://chatgpt.com/backend-api/accounts/check` | GET | **405** | "Method Not Allowed" — endpoint exists but rejects GET. Likely POST-only, not retried. |
| `account-billing` | `https://chatgpt.com/backend-api/accounts/check/v4-2023-04-27` | GET | **403** | Same HTML edge-layer 403 page as `conversation-limit`. |

## Why each candidate failed

- **`/conversation_limit` (403 HTML):** ChatGPT's edge layer expects a browser session cookie. The CLI's OAuth `access_token` does not authenticate this surface. No transformation of the token (different audience, refresh, exchange) is documented and the CLI's `auth.json` does not contain any session-cookie-shaped credential.
- **`/models/usage` (404):** Endpoint does not exist at this URL. May exist at a different path on `api.openai.com` rather than `chatgpt.com`, but that's a different auth realm again.
- **`/me` (200 — partial fit only):** Returns identity but no usage data. Includes `orgs`, `tenants`, and `chatgpt_eval_data` (large nested objects, redacted in inspection but not in the raw capture); none of those visibly carry rate-limit numbers in the captured shape.
- **`/accounts/check` (405):** Endpoint exists for some HTTP method other than GET. Could plausibly be probed with POST in a follow-up, but the surrounding endpoints (`accounts/check/v4-*`) are edge-blocked, so the chance of useful data behind a POST is low.
- **`/accounts/check/v4-2023-04-27` (403):** Same edge-layer block as `/conversation_limit`.

The HTML 403 pages all returned the same generic ChatGPT-branded "access denied" template, including stylesheets and JS error-detail blocks. The scrubber correctly redacted nothing in those bodies (no embedded tokens or cookies).

## Token validity confirmed

`/me` returned 200 OK with the user's real account information, which proves the OAuth `access_token` from `~/.codex/auth.json` is currently valid and accepted by ChatGPT's auth layer. The failure of the usage endpoints is **not a credential problem** — it's an architectural mismatch between Codex CLI auth (OAuth API-flow) and ChatGPT web app auth (session cookie).

## Hand-off to daemon plan

The macOS daemon plan should:

1. **Skip the Codex poller entirely.** Do not attempt to query `chatgpt.com/backend-api/*`.
2. **Write `codex.ok = false` in every BLE payload.** The firmware's Codex screen should render `—` for the percentage, `—` for the reset countdown, and surface "no data" or similar on the secondary tick line. Page indicator dots still show ●○ / ○● for swipe symmetry.
3. **Reuse the auth-loader and redact modules** from this spike (`research/codex-spike/src/auth.py`, `research/codex-spike/src/redact.py`). They're well-tested (28 tests passing at the time of writing) and handle the lowercase-`chatgpt` auth_mode found on real Codex installs. Copy them into the daemon source tree adapting import paths.
4. **Update spec § 7.4** to reflect the empirical result. The "research item — empirical" framing is now closed: there is no available endpoint to probe.

## Follow-up options (NOT in scope for this port)

Documented for future reference if the user wants to revisit Codex tracking later:

- **`POST /accounts/check`** instead of GET. The 405 on `/accounts/check` (without the `v4-*` suffix) means the endpoint exists for some other method. Worth one POST probe with an empty body to see if it returns plan info. Low confidence given the surrounding 403s.
- **Browser-session cookie exchange.** If the user is willing to manually paste their `__Secure-next-auth.session-token` cookie from a logged-in ChatGPT browser tab, the daemon could probe `chatgpt.com/backend-api/conversation_limit` with that cookie. Operationally fragile (cookies rotate, copy-paste workflow is bad UX) but would close the data gap.
- **`api.openai.com/v1/responses` headers.** Send a minimal Responses-API request (max_tokens=1, model=gpt-5.5) and parse `x-ratelimit-*` headers similarly to the Claude probe. Requires an OpenAI-API-key (not ChatGPT-OAuth) credential; the user would need to set `OPENAI_API_KEY` explicitly. Useful for users on the API-billing plan, useless for Codex CLI's ChatGPT-mode users.
- **Watch Codex CLI's own network traffic** with `mitmproxy` or `proxyman` to discover the endpoint Codex itself uses to display usage (if any — Codex CLI may not display usage to the user at all today).
- **Inspect Codex CLI source.** Codex CLI is closed-source per Codex CLI's distribution, but the on-disk binary at `/Users/aytuncyildizli/.superset/bin/codex` and SQLite log at `~/.codex/logs_2.sqlite` may reveal what endpoints the CLI itself reaches. The `logs_2.sqlite` already exists; a follow-up could query it for usage queries.
- **Codex CLI's `--json` mode.** `superset` and `codex` both expose `--json` output. `codex agents list --json` or similar may surface session quota data without requiring a network probe at all.

## Captures

Live captures from the 5 probed endpoints were saved scrubbed to `research/codex-spike/fixtures/responses/`. Because the `/me` response contains PII (email, user ID, name, phone, region, country) that the secret-scrubber correctly does not touch, **`fixtures/responses/` is now gitignored entirely** — the captures stay local for inspection, only this report ships to the branch.

To re-create the captures locally:

```bash
cd research/codex-spike
source .venv/bin/activate
python -m src.probe --capture
```

The probe is idempotent and safe to re-run — each invocation overwrites the previous capture and re-scrubs.

## Reproducing this spike

```bash
cd research/codex-spike
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
pytest                              # 30 tests pass (17 redact + 11 auth + 6 probe)
python -m src.probe --capture       # re-run live probe (writes scrubbed captures locally)
```

## Tests

| Module | Tests | Notes |
|---|---|---|
| `test_redact.py` | 17 | Token redaction (prefix/suffix, boundary), `scrub_dict` (case-insensitive keys, nested, list-of-dicts), `scrub_text` (regex masks for Bearer/JWT/OpenAI/cookies), `scrub_any` (dict/list/str dispatch) |
| `test_auth.py` | 11 | ChatGPT-mode gating (case-insensitive, whitespace-tolerant), error paths (missing file, malformed JSON, ApiKey rejection, null mode), edge cases (missing tokens dict, empty tokens dict), repr safety (all three token fields) |
| `test_probe.py` | 6 | `probe_one` (JSON body, non-JSON body, network error), `save_capture` (scrubs headers, scrubs raw_body via regex, scrubs list-shaped body) |
| **Total** | **34** | All passing on Python 3.14.4 / pytest 9.0.3 |

(Note: counts above reflect the final committed state. Test count grew through TDD review loops: redact 13→17, auth 7→11 after the lowercase-auth_mode finding, probe 4→6 after the raw_body scrub gap was closed.)
