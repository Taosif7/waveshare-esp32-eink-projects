# Claude desk

Live Claude Code sessions on the **Waveshare ESP32-C6-ePaper-1.54** (200×200). The panel is a second view of a question. The terminal dialog stays up. Whichever side answers first wins.

## Screens

A status row is always on top: `LINK` or `DOWN`, and the cell percent (`--` when the battery node reads open).

1. **Session list** — one row per live session. `R` running, `Q` needs you, `E` failed, `I` idle. BOOT moves the highlight. PWR does nothing here.
2. **Question** — replaces the list when Claude asks something. BOOT moves the options. PWR types the highlighted option into the Claude terminal. A long press of PWR silences the chime and returns to the list. It does not answer Claude.

If several sessions are waiting, a `+N` mark shows how many more are queued. The glass stays on the question you are answering, then jumps to the next.

## Controls

| Button | Action |
|--------|--------|
| **BOOT** | Next session, or next option |
| **PWR** | Send the highlighted option |
| **PWR** held | Silence the chime and show the list |

Both buttons are polled on a background task, so a refresh does not eat a press.

## Sounds

The onboard ES8311 plays three tones. A question repeats until you send it or hold PWR. A failure and a finished session each play once and do not change the screen. Connect a speaker to the board header if one is not already attached.

## Battery

Voltage comes from the Waveshare ADC example: ADC1 channel 0, pin millivolts times 2, empty at 3.0 V, full at 4.12 V. The percent is sampled about every 30 seconds.

## BLE

The panel advertises as `EinkSessions` and accepts one computer. A second connection is refused. The host script uses `bluetoothctl` on this machine. It turns the adapter on, remembers the MAC in `~/.local/share/claude-desk/device`, and writes text lines to the Nordic UART receive characteristic.

```text
@3
S R Add README
S Q Fix tests
T Mode
Q Which mode should I use?
O Agent
O Plan
!
BEEP Q
```

The panel answers `H` when notifications are on, `P <index>` when you confirm, and `K` on a long press.

## Host

Claude Code is not required to build the firmware. The scripts need `jq`, BlueZ, and `xxd`.

```bash
chmod +x host/*.sh
./host/install-hook.sh
./host/sessions.sh
```

`install-hook.sh` merges a `PreToolUse` and a `PostToolUse` hook for `AskUserQuestion` into `~/.claude/settings.json`. The hook writes the question to `~/.local/share/claude-desk/spool/` and exits without a decision, so Claude still shows its own dialog. `sessions.sh` reads `~/.claude/sessions/*.json` plus that spool. A pick on the panel types Down and Enter into the session's terminal, starting from the first option. `./host/sessions.sh --dump` prints the frame and does not open Bluetooth.

Cloud sessions do not run hooks from this machine's settings file, so they do not appear here.

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/claude-desk
pio run -t upload
pio device monitor
```

If serial permission fails: `sg dialout -c 'pio run -t upload'`.

See the repo [technical specification](../ESP32-C6-ePaper-1.54-Technical-Specification.md) for pinout and USB caveats. Do not wire buttons to GP12/GP13.
