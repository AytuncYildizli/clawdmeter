# Codex Probe Research Spike

Determines whether Codex usage can be read from a ChatGPT-mode authenticated
session. Output is `REPORT.md` plus a tested parser (if a working endpoint
was found) or a stub-fallback recommendation.

## Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Run tests

```bash
pytest
```

## Run a live probe (uses real ~/.codex/auth.json)

```bash
python -m src.probe --capture
```

This writes raw responses to `fixtures/responses/<endpoint-slug>.json`.

## Security

This spike never logs or commits raw OAuth tokens. Captured fixtures are
scrubbed automatically by `src/redact.py` before being written to disk.
If you ever see a fixture file containing `eyJ` or a long Bearer-looking
string, do not commit it — re-run with capture mode and report the bug.
