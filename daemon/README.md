# Clawdmeter Daemon (macOS)

Reads Claude rate-limit utilization + Superset focus state, writes JSON to the
Clawdmeter device over BLE. Codex side is a stub per
`research/codex-spike/REPORT.md`.

## Setup

```bash
cd daemon
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
pip install -e .
```

## Run tests

```bash
pytest
```

## Run the daemon

```bash
python -m clawdmeter
```

First run will trigger macOS Bluetooth permission prompt — accept it.

## Auto-start

```bash
cp launchd/sh.clawdmeter.daemon.plist ~/Library/LaunchAgents/
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/sh.clawdmeter.daemon.plist
```
