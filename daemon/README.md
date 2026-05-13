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

## Integration testing

Plan #2 Task 10 (bumble virtual peripheral) was skipped: bumble's virtual
transports are flaky on macOS and we have real M5Stack Core 2 hardware. End-to-end
BLE testing happens in Plan #3 Task 9 (firmware integration) against the real
device. Until firmware lands, `python -m clawdmeter` will scan, find nothing, and
sit in the reconnect loop — that's the expected pre-firmware state.
