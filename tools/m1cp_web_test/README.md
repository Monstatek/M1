<!-- See COPYING.txt for license details. -->

# M1CP Web Serial Test Client

A tiny, framework-free browser tool to validate **M1CP v1** (M1 Manager Control
Protocol) over USB CDC / Web Serial against real M1 hardware.

**This is an engineering validation tool, not the final MonstaTek web app.** It
only exercises the already-implemented, non-destructive commands:

```
HELLO · PING · GET_CAPABILITIES · GET_DEVICE_INFO · END_SESSION
```

No firmware writing, SD transfer, ESP32 flashing, HID, or BadUSB.

## Files

| file            | purpose                                                        |
| --------------- | ------------------------------------------------------------- |
| `m1cp.js`       | M1CP v1 protocol: CRC-16, frame encode, streaming decoder, payload decoders. Runs in browser and Node/jsc. |
| `index.html`    | UI + Web Serial glue (connect, HELLO handshake, command buttons, hex diagnostic log). |
| `m1cp.test.js`  | Deterministic protocol tests (CRC vector, fragmentation, resync, bad CRC, oversize, truncation, seq matching, device-info sentinels). |

Protocol authority: [`documentation/M1CP_v1.md`](../../documentation/M1CP_v1.md).

## Requirements

- **Chrome or Edge, desktop.** Web Serial is not in Firefox/Safari.
- A **secure context**: serve over `http://localhost` (or HTTPS). `file://` does
  **not** expose `navigator.serial`.

## Run the UI

From the repository root:

```bash
python3 -m http.server 8000 --directory tools/m1cp_web_test
```

Then open <http://localhost:8000/> in Chrome/Edge, plug in the M1 (flashed with
the test firmware), and click **Connect Device** → pick the M1's serial port.

The client sends a valid `HELLO` and only marks the session active after a valid
`HELLO_RESPONSE`. It never sends random probing traffic. The diagnostic log shows
TX/RX frames (hex), parsed command, sequence number, payload length, ACK/NACK and
errors/timeouts.

## Run the protocol tests (headless, no device)

With Node:

```bash
node tools/m1cp_web_test/m1cp.test.js
```

Or with macOS JavaScriptCore (no Node required):

```bash
JSC=/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc
"$JSC" tools/m1cp_web_test/m1cp.js tools/m1cp_web_test/m1cp.test.js
```

Expected: `ALL PASS (20/20)`.

## Notes

- Baud rate is nominal (`115200`); CDC ignores it, but Web Serial requires a value.
- `END_SESSION` releases the serial port so you can confirm the legacy console
  returns in a terminal; **Connect** again to start a fresh session.
- Device-info fields that the firmware reports as unavailable (hw revision, SD
  capacity/free, ESP32 firmware version) are shown truthfully as `unknown` /
  `unavailable`; the client never fabricates them.
