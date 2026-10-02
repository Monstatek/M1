<!-- See COPYING.txt for license details. -->

# MonstaTek M1 — BLE UI Design (Scan / Details / Signal Meter / GATT Explorer)

**Status:** design proposal, no code yet. Target: M1-side (STM32H573) implementation only.
**Firmware dependency:** none — the ESP32-C6 firmware (`~/Desktop/m1-esp32c6-fw`) already
implements the whole command surface this UI needs. This document maps the four BLE functions
in the spec onto that existing protocol.

Related: `documentation/ESP32/…`, and in the firmware repo `docs/BLE_GATT_PROTOCOL.md`,
`docs/BLE_SIGNAL_METER.md`, `docs/PROTOCOL.md`.

---

## 1. Scope & the one prerequisite fix

Four functions, presented as one connected workflow:

```
Scan Devices ──▶ Device Details ──▶ Signal Meter
                              └────▶ GATT Explorer
```

The **selected BLE device** (its list index *and* MAC/addr, captured at scan time) persists
across all four functions until: it disappears, the user picks another, or the BLE session
resets (menu exit / `stop`).

**Prerequisite fix (unavoidable):** the current `bt_show_device_details()` in
[`m1_csrc/m1_bt.c`](m1_csrc/m1_bt.c) parses the *old* firmware `list <id>` layout
(`Address :`, `Name :`, `Event type :` …). The shipping firmware emits a different layout
(`- Flags:`, `- Complete Local Name:`, `- TX Power:` …) and no longer puts the MAC in the
detail output. Device Details is rewritten against the new format (§7).

---

## 2. What the M1 already provides (reuse, don't rebuild)

| Piece | Location | Use |
|---|---|---|
| ESP32 bring-up + `mode -b` | `bt_prepare_esp32_cmd_path()` / `bt_restore_esp32_cmd_path()` | entry/exit of every BLE screen |
| One-shot command + read-to-prompt | `bt_cmd_process(cmd, buf, size, timeout_s)` | scan, list, services, read, subscribe (request side) |
| Line read primitive | `esp32_uart_read_until_prompt(buf, size, ms, ">> ")` | basis for the new streaming reader (§5) |
| Action bar | `m1_draw_bottom_bar(u8g2, lbmp, ltext, rtext, rbmp)` — inverted box y52–63 | bottom bar on every screen |
| Header frame | `m1_frame_128_14` (y0–14) | title row |
| Icons | `arrowleft_8x8`, `arrowright_8x8`, `hourglass_18x32` | nav + busy |
| Fonts | `M1_DISP_MAIN_MENU_FONT_N` (title), `M1_DISP_SUB_MENU_FONT_N` (body, ~10px, ~21 chars/line) | text |
| Buttons | OK=0, UP=1, LEFT=2, RIGHT=3, DOWN=4, BACK=5; events CLICK / HOLD | via `main_q_hdl` + `button_events_q_hdl` |

**Layout budget (128×64):** title band y0–14, body y14–51 (≈3–4 small-font rows), action bar y52–63.

---

## 3. Menu restructure

`menu_Bluetooth` gains four children (replacing the single `Scan`):

```
S_M1_Menu_t menu_Bluetooth_ScanDevices  = { "Scan Devices",  bluetooth_scan_devices, ... };
S_M1_Menu_t menu_Bluetooth_DeviceDetails= { "Device Details",bluetooth_device_details,... };
S_M1_Menu_t menu_Bluetooth_SignalMeter  = { "Signal Meter",  bluetooth_signal_meter, ... };
S_M1_Menu_t menu_Bluetooth_GattExplorer = { "GATT Explorer", bluetooth_gatt_explorer,... };

S_M1_Menu_t menu_Bluetooth = { "Bluetooth", menu_bluetooth_init, menu_bluetooth_exit, NULL,
    4, 0, menu_m1_icon_bluetooth, NULL,
    {&menu_Bluetooth_ScanDevices, &menu_Bluetooth_DeviceDetails,
     &menu_Bluetooth_SignalMeter, &menu_Bluetooth_GattExplorer} };
```

Each of the three downstream functions, when no device is selected, transparently routes the
user through Scan Devices first (spec: "no hidden requirement to visit another function").

---

## 4. Shared selected-device state model

A single module-static struct in `m1_bt.c`, plus the existing scan table:

```c
typedef struct {
    uint8_t  valid;                 /* a device is currently selected            */
    uint16_t index;                 /* id used in list/connect/signal <id>       */
    char     mac[BSSID_STR_SIZE];   /* "AA:BB:CC:DD:EE:FF" from scan summary     */
    char     name[BLE_SCAN_NAME_MAX];
    int      rssi;                  /* last-seen RSSI from scan summary          */
} ble_selected_t;
static ble_selected_t g_ble_sel;
```

- Set by Scan Devices on CENTER-select (and kept as the user pages the scan list).
- The **index** is only valid for the current scan session; every downstream `connect <id>`
  / `signal <id>` uses it, and the firmware re-captures the device by *address* internally
  (Signal Meter) or fails with `[!] Invalid BLE ID` (handled → re-scan prompt).
- Cleared on menu exit and on `stop`.

Selection helpers: `ble_sel_set_from_item(index)` and `ble_sel_clear()`.

---

## 5. New infrastructure: the streaming reader

Signal Meter and GATT need lines that arrive **after** the `>> ` prompt (`[BLE:SIG]`,
`[BLE:NTF]`, `[BLE:CONN]`, `[BLE:DISC]`…). One small helper covers all of it:

```c
/* Read exactly one '\n'-terminated line into buf (prompt "  >> " stripped/ignored).
 * Returns line length, or 0 on timeout. Non-blocking-ish: short timeout so the caller
 * can interleave button polling. */
static uint16_t bt_read_line(char *buf, uint16_t size, uint32_t timeout_ms);
```

Screen loops become: *poll button queue (non-blocking) → `bt_read_line(…, ~50ms)` → if a
`[BLE:…]` line arrived, update model + redraw → repeat*. BACK sends the appropriate stop
(`signal stop` / `disconnect` / `unsubscribe`) and drains to the next prompt before leaving.

This is the only genuinely new mechanism; everything else is screen drawing + parsing.

---

## 6. Function 1 — Scan Devices

**Command:** `scan` (active) → then `list` (summary rows).
**Parses (summary):** `[%02d] <MAC> RSSI=%d NAME=%s`  → id, mac, rssi, name (name may be empty).

Busy screen while scanning (existing `Scanning BLE...` + hourglass). On completion, a scrollable
list. Repeatable (a Rescan affordance).

```
┌────────────────────────────┐  title band (MAIN font)
│ BLE Devices                │
├────────────────────────────┤
│  Sample Device             │  ← selected row (inverted box)
│  ESP32-C6                  │
│  (unknown) DA:2F..         │  no name → shortened MAC
│  SensorTag                 │
│                       3/12 │  ← sel/total counter (no arrows)
├────────────────────────────┤
│ [C] Details            ▲▼  │  action bar
└────────────────────────────┘
```

**Buttons:** UP/DOWN move selection · **OK = select → store in `g_ble_sel` → open Device
Details** · a held-key or a dedicated "Rescan" is TBD (see §12 Q3) · BACK exits.
Row label: name if present, else shortened MAC (`AA:BB..`).

**Checkpoint 1 note:** the *existing* `bluetooth_scan()` scan+pager implementation is preserved
as-is functionally; Scan Devices is reached through the new menu entry. Full list-UI polish per
this section is a later checkpoint — Checkpoint 1 keeps scan behavior intact and focuses on the
menu, selected-device state, and the Device Details rewrite.

---

## 7. Function 2 — Device Details (advertisement inspector)

**Entry:** from Scan-select, or from the menu when `g_ble_sel.valid` (else route to Scan).
**Command:** `list <index>`.
**Parses (`print_detail` output) — only present fields shown, no `N/A` placeholders:**

| Firmware line | Field |
|---|---|
| `    RSSI: <d>` | RSSI dBm |
| `- Flags: 0x<hh>` | Flags |
| `- Shortened Local Name: <s>` / `- Complete Local Name: <s>` | Name |
| `- Service UUID16 (0x<hhhh>):` | 16-bit service UUID (repeatable) |
| `- Service UUID128: <hex>` | 128-bit service UUID (repeatable) |
| `- TX Power: <d> dBm` | TX power |
| `- Service Data UUID16: 0x<hhhh>, Data: <hex>` | Service data |
| `- Manufacturer Data: company_id=0x<hhhh>, data=<hex>` | Mfg data |
| `Raw ADV data : <hex>` | raw adv |
| `Raw SCAN_RSP data : <hex>` | raw scan-rsp |

MAC comes from `g_ble_sel.mac` (not in the detail output). Content is paged; page count is
**dynamic** (only pages with real data are built).

```
Page 1/4               Page 2/4              Page 3/4            Page 4/4
┌──────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│ Device Detail│   │ Advertisement│   │ Services     │   │ Manufacturer │
├──────────────┤   ├──────────────┤   ├──────────────┤   ├──────────────┤
│ Sample Device│   │ Flags: 0x06  │   │ 1800         │   │ ID: 0x004C   │
│ RSSI:-47 dBm │   │ TXPwr:-4 dBm │   │ 1801         │   │ 4C 00 10 05  │
│ AA:BB:CC:DD..│   │              │   │ 180F         │   │ 9A 1F ...    │
│          1/4 │   │          2/4 │   │          3/4 │   │          4/4 │
├──────────────┤   ├──────────────┤   ├──────────────┤   ├──────────────┤
│ [C] Actions ◀▶│  │ [C] Actions ◀▶│  │ [C] Actions ◀▶│  │ [C] Actions ◀▶│
└──────────────┘   └──────────────┘   └──────────────┘   └──────────────┘
```

**Buttons:** LEFT/RIGHT flip pages (counter shown, so no extra arrows in body) · **OK =
Actions** → small menu {Signal Meter, GATT Explorer} operating on the selected device · BACK
returns to Scan list. Long hex (UUID128 / mfg / raw) wraps/pages within its own page.

---

## 8. Function 3 — Signal Meter

**Entry:** menu or Device-Details→Actions. If `g_ble_sel.valid`, start immediately; else route
through Scan → select → Signal Meter.
**Commands:** `signal <index>` to start; `signal stop` on exit. Streams every 500 ms.
**Parses:**

| Firmware line | Handling |
|---|---|
| `[BLE:SIG:START] id=I addr=MAC[ (random address…)]` | mark started; flag random-addr identity caveat |
| `[BLE:SIG] id=I addr=MAC raw=R avg=A cat=C age=Nms` | update meter (use **avg** as the big number, `raw` optional) |
| `[BLE:SIG:LOST] id=I addr=MAC …` | show "Lost / searching…" |
| `[BLE:SIG:STOPPED] id=I` | clean exit |
| `[!] Radio busy (WIFI active)…` | Wi-Fi-owns-radio screen (§11) |

Category strings from firmware: **Very strong / Strong / Medium / Weak / Very weak** (cutoffs
≥−55 / ≥−67 / ≥−77 / ≥−87 / else). **Decision needed** — spec lists *Excellent / Good / Fair /
Weak / Very Weak* (see §12 Q1).

```
┌────────────────────────────┐
│ Signal Meter               │
├────────────────────────────┤
│ Sample Device              │
│                            │
│      -46 dBm               │  ← large, avg value, immediately readable
│      Strong                │
│  ▐▊▊▊▊▊▊░░░                │  ← optional bar from avg
├────────────────────────────┤
│ [<] Back                   │
└────────────────────────────┘
```

**Buttons:** BACK exits (sends `signal stop`, drains to prompt). Display updates continuously.
Favors the big number over field clutter (spec).

---

## 9. Function 4 — GATT Explorer

Guided flow; the user never sees raw connection state. Radio ownership handled behind the scenes.

### 9a. Connect
**Commands:** `connect <index>` → firmware auto-discovers. Stream until terminal line.

| Firmware line | Handling |
|---|---|
| `[*] Connecting to <mac> (30s timeout)...` | show `Connecting…` + hourglass |
| `[BLE:CONN] connected handle=H` | show `Connected` |
| `[BLE:DISC] complete: S service(s), C char(s), D descriptor(s)` | discovery done → fetch tree |
| `[BLE:ERR] connect failed status=<s>` / `[BLE:CONN] disconnected reason=R` | simple error + Retry/Back |
| `[!] Radio busy (WIFI active)…` | Wi-Fi-owns-radio screen (§11) |

No stack-level status codes on normal screens (spec).

### 9b. Service list
**Command:** `services` → parse the tree. Known 16-bit UUIDs → readable names (small table:
1800 Generic Access, 1801 Generic Attribute, 180A Device Info, 180F Battery, 180D Heart Rate,
…); unknown shown as raw `0x1234` / `UUID128`.

```
┌────────────────────────────┐        Parse targets (ble_gatt_disc_print):
│ GATT Services              │        [SVC i] UUID16 0xXXXX handles a-b
├────────────────────────────┤            [CHR] UUID16 0xXXXX val=H props=0xPP [R W WNR N I]
│  Generic Access            │                [DSC] UUID16 0xXXXX handle=H
│  Battery Service           │
│  Device Information        │        props flags: R=read W=write WNR=write-no-rsp
│  Unknown (0xFE95)          │                    N=notify I=indicate
│                       2/7  │
├────────────────────────────┤
│ [C] Select             ▲▼  │
└────────────────────────────┘
```

### 9c. Characteristic list (per selected service)
Filter the parsed tree to the selected service's handle range. Compact properties.

```
┌────────────────────────────┐
│ Battery Service            │
├────────────────────────────┤
│  Battery Level             │
│  0x2A19   Props: R N       │  ← UUID + compact props
│                       1/1  │
├────────────────────────────┤
│ [C] Actions            ▲▼  │
└────────────────────────────┘
```

### 9d. Characteristic actions (built from that char's props only)
Menu shows only supported ops — Write hidden if not writable, Notify hidden if no notify bit,
Descriptors shown if the char has any DSC.

```
{ Read | Write | Notify/Indicate | Descriptors }
```

- **Read** — `read <val_handle>` → stream `[BLE:READ] handle=H len=N data=<hex>`. Show decoded
  short values (e.g. 1-byte → decimal `64`) and always offer hex; long data pages.
- **Write** — value entry (hex via virtual keyboard, `m1_virtual_kb`), pick
  `write` (with-response) or `writenr` (no-response, only if props allow) → firmware **stages**
  (`[*] PENDING WRITE …`) → M1 auto-sends `confirm` after an on-screen confirm, or `cancel`.
- **Notify / Indicate** — `subscribe <h>` (or `indicate <h>`) → `[BLE:SUB] ok`, then live view
  streaming `[BLE:NTF]/[BLE:IND] handle=H len=N data=<hex>`; latest value highlighted; BACK sends
  `unsubscribe <h>`. `[BLE:DROP] n …` shown as a small "n dropped" note.
- **Descriptors** — from the parsed `[DSC]` entries; known descriptor UUIDs → names, unknown → raw.

```
Read value             Notifications live
┌──────────────┐   ┌──────────────┐
│ Value        │   │ Notifications│
├──────────────┤   ├──────────────┤
│ 64           │   │ 2A37         │
│ (0x40)       │   │ 01 48   ◀ new│
│ 01 7F 22 4A  │   │ 01 49        │
├──────────────┤   ├──────────────┤
│ [<] Back     │   │ [<] Stop     │
└──────────────┘   └──────────────┘
```

**Exit:** leaving GATT Explorer sends `disconnect`; the firmware releases the radio on every
disconnect / failure / timeout (so Wi-Fi works immediately after).

---

## 10. Command ⇄ screen map (quick reference)

| Screen | Sends | Reads until | Async lines consumed |
|---|---|---|---|
| Scan Devices | `scan`, `list` | `>> ` | — |
| Device Details | `list <id>` | `>> ` | — |
| Signal Meter | `signal <id>` / `signal stop` | streaming | `[BLE:SIG*]` |
| GATT connect | `connect <id>` | `[BLE:DISC] complete` / err | `[BLE:CONN]`,`[BLE:DISC]`,`[BLE:ERR]` |
| GATT services | `services` | `>> ` | — |
| GATT read | `read <h>` | `[BLE:READ]` / `[BLE:ERR]` | `[BLE:READ]` |
| GATT write | `write/writenr <h> <hex>`,`confirm`/`cancel` | `[BLE:WRITE]`/`[BLE:ERR]` | `[BLE:WRITE]` |
| GATT notify | `subscribe/indicate/unsubscribe <h>` | `[BLE:SUB]` then streaming | `[BLE:NTF]`,`[BLE:IND]`,`[BLE:DROP]` |
| exit any GATT | `disconnect` / `stop` | `[BLE:CONN] disconnected` | — |

---

## 11. Radio ownership (Wi-Fi vs BLE)

Any command may return `[!] Radio busy (WIFI active). Stop WIFI first.` The M1 shows a plain
message rather than an unexplained failure:

```
┌────────────────────────────┐
│ Bluetooth                  │
├────────────────────────────┤
│  Wi-Fi is currently using  │
│  the radio.                │
│  Stop Wi-Fi and try again. │
├────────────────────────────┤
│ [<] Back                   │
└────────────────────────────┘
```

Firmware enforces bidirectional exclusion via `m1_radio_manager`; BLE releases the radio on
scan-complete / stop / disconnect / signal-stop, so this is a transient, understandable state.

---

## 12. Open decisions (defaults chosen; revisit if needed)

1. **Signal categories.** Firmware emits *Very strong / Strong / Medium / Weak / Very weak*.
   Spec text says *Excellent / Good / Fair / Weak / Very Weak*. **Default:** map firmware→spec
   labels on the M1, keep firmware cutoffs. (Deferred to the Signal Meter checkpoint.)
2. **CENTER action-bar icon.** Spec wants a CENTER icon + action word. Today only
   `arrowleft/right_8x8` exist. **Default:** add a small `center_8x8` XBMP to
   `m1_display_data.c`. (Introduced when first needed by a BLE screen.)
3. **Rescan gesture** on the Scan list — deferred to the Scan-UI polish checkpoint.
4. **Write value entry** — reuse `m1_virtual_kb`; auto-pick with-response when available.
   (GATT write checkpoint.)
5. **File layout.** **Default:** keep BLE code in `m1_bt.c` / `m1_bt.h` for now to minimize
   surface area; split into `m1_bt_*.c` only if the file grows unwieldy.

---

## 13. New function inventory (M1 side)

Files touched: `m1_csrc/m1_bt.c`, `m1_csrc/m1_bt.h`, `m1_csrc/m1_menu.c`,
`m1_csrc/m1_display_data.c` (center icon, when introduced).

```
bluetooth_scan_devices(void)        // §6  (menu entry → existing scan preserved in Checkpoint 1)
bluetooth_device_details(void)      // §7  (rewrite of bt_show_device_details parsing)
bluetooth_signal_meter(void)        // §8
bluetooth_gatt_explorer(void)       // §9  → gatt_connect / gatt_services /
                                    //       gatt_characteristics / gatt_char_actions /
                                    //       gatt_read / gatt_write / gatt_notify / gatt_descriptors
ble_sel_set/is_valid/clear()        // §4  selected-device state
ble_require_selection_or_scan()     // §4  guided entry
bt_read_line()                      // §5  streaming reader
bt_uuid16_name() / bt_props_str()   // §9  UUID + property prettifiers
```

---

## 14. Build order (checkpoints)

1. **Checkpoint 1 (this branch, in progress):** menu restructure (4 entries) + `ble_selected_t`
   state + preserve existing Scan Devices + Device Details rewritten against the current firmware
   `list <id>` format. *Hardware checkpoint.*
2. `bt_read_line()` + Signal Meter. *Hardware checkpoint.*
3. GATT Explorer: connect → services → characteristics → actions (read first). *Checkpoint.*
4. GATT write / notify / descriptors. *Checkpoint.*

All changes are BLE-scoped; no unrelated RFID/NFC/Sub-GHz/Wi-Fi/USB/IR/GPIO/Settings behavior is
modified beyond the smallest shared integration change genuinely required by BLE.
