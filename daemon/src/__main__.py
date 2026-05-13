"""Entrypoint: `python -m clawdmeter`. The real orchestrator lives in main.py."""
from .main import run

if __name__ == "__main__":
    raise SystemExit(run())
