# WIDI group setup without the phone UI

The CME WIDI app's Auto-learn control was inaccessible on the user's phone. The Android app was acquired with the requested `apkeep` tool and inspected to recover its BLE command sequence. The resulting helper was tested on the attached WIDI Master; the Core2 subsequently received the keyboard's notes in WIDI group mode.

## Repeat the setup

1. Keep the instrument/WIDI Master powered. Close the CME phone app and temporarily turn that phone's Bluetooth off. If an existing group already occupies the WIDI, switch its peripherals off temporarily so the control client can connect.
2. Select **Bluetooth → WIDI group** on the Core2. This is the firmware default.
3. With [uv](https://docs.astral.sh/uv/) installed, run:

   ```sh
   uv run tools/widi_control.py status
   uv run tools/widi_control.py group --peripheral AA:BB:CC:DD:EE:01
   ```

4. Keep/power the Core2 on and verify that its connection dot turns green and it responds to the keyboard. The group has already been saved in software. Substitute your Core2's address when using different hardware.

The script pins Bleak 3.1.0 through inline dependency metadata. `status` reads the role and four address slots. Both setup commands first back up those settings to `build/widi-research/group-backup-<UTC timestamp>.json`, then write and verify the new values. `learn` enables Auto-learn discovery; `group` explicitly saves the listed members. The optional `--device` accepts a device name or the platform's BLE identifier; use it to target the intended central when several WIDI devices are present. `--log` records the request/reply transcript. If an existing group occupies the WIDI, temporarily switch off its peripherals and stop Mac MIDI advertising to free the configuration connection.

On macOS, a MIDI connection can remain available through CoreBluetooth even after the original client exits and the device stops advertising. The helper checks the connected MIDI peripherals as well as scanning. At the end of setup, it releases the outgoing connection through Apple's `MIDIBluetoothDriverDisconnect` API and sends the WIDI disconnect action. The device-side release was necessary in the hardware test: after it, the WIDI immediately connected to the Core2.

## Include the Mac as a group peripheral

The Mac can receive the same keyboard MIDI alongside the display. In Audio MIDI Setup, open MIDI Studio → Open Bluetooth Configuration and select **Advertise**. Keep that window open and the Mac awake during enrollment. The intended roles are WIDI Master = central, Core2 = peripheral, Mac = peripheral.

An example two-peripheral group is:

```sh
uv run tools/widi_control.py group \
  --peripheral AA:BB:CC:DD:EE:01 \
  --peripheral AA:BB:CC:DD:EE:02
```

Replace the first placeholder with the Core2's public BLE address and the second with the Mac's MIDI advertisement address, observed by the ESP32 scanner.

macOS repeatedly restored its old outgoing WIDI connection, even after the CoreMIDI disconnect API and device-side disconnect succeeded. After the saved group was verified and the control client exited, the old **WIDI Master** pairing was removed through System Settings → Bluetooth → Forget Device while MIDI advertising remained active. Bluetooth logs then showed an **incoming** WIDI connection, followed by successful pairing. The Core2 connected to the same WIDI central, and both received the instrument's active-sensing stream simultaneously. CoreMIDI's cached `MIDI Local Peripheral`/`MIDI Remote Peripheral` properties retained their old values; the controller's incoming-connection log established the actual connection direction.

A subsequent Core2 restart rejoined the saved group automatically while the Mac's stream continued. This verifies reconnecting the display; a full instrument/WIDI power cycle remains a separate check. Keep MIDI advertising available on the Mac when establishing a new session.

The helper supports one to four `--peripheral` public BLE MAC addresses. It follows the app's manual central-programming path: initialize the four slots to six `FF` bytes each, set role AUTO, then write each member address into its slot. It backs up the old settings and checks the resulting values before releasing the control link.

Address values are controller-order, little-endian bytes. This was corroborated on the real hardware: the Core2's advertised public MAC was observed, and its saved slot returned those same six bytes reversed. The WIDI's serial-derived address representation follows that order too. The Core2 and Mac slots read back as `1e b9 0c 3b 01 5c` and `ef c2 ce 57 2f 84`; the two unused slots remain all `FF`.

## Artifact identity and analysis boundary

| Field | Observed value |
| --- | --- |
| Android package | `com.cme.widimaster.android` |
| App version | 3.0.4 / version code 30004 |
| Acquisition | `apkeep 1.0.0`, APKPure source; package identity checked against CME's official app link |
| APK SHA-256 | `5e1a5e73bad89f118ad56aaa1ceea91fa77d4cfa0c6d2e8474eee3df60540dfb` |
| Static engine | Installed JADX CLI 1.5.3, with smali cross-checks for the manual-group path |
| Device model | `CME WIDI Master` |
| Device firmware field | ASCII `0260` |

REA's Android provider reported a missing `REA_JADX_MCP_JAR`; the investigation continued through the installed JADX/apktool commands. These decompiler results are static artifact observations, not REA Evidence records or proof of execution. Hardware observations below independently establish the commands used here. Decompiled third-party sources and the APK remain in ignored `build/widi-research/`; they are not shipped with this repository. JADX reported errors in other methods; the selected Auto-learn and command-builder paths were readable.

The phone UI bug itself was not reproduced or repaired. This implements a working setup path around it.

## Recovered command sequence

All configuration traffic uses the **standard BLE MIDI service**, not the separate firmware-update service:

```text
Service:        03b80e5a-ede8-4b33-a751-6ce34ec4c700
Characteristic: 7772e5db-3868-4112-a1a9-f2669d106bf3
```

Subscribe to notifications before sending requests. Writes use **write without response**; the device answers through MIDI SysEx notifications. Only one request is outstanding at a time, with the app's three-second response timeout.

The Auto-learn enable method performs:

1. Write forced-role register **6** to **0 / AUTO**.
2. Write six zero bytes to each predefined address register **7, 8, 9 and 10**.
3. Wait **1000 ms** after the writes succeed, then disconnect the BLE transport.

There is no separate Auto-learn opcode or final commit command in that app method. The app treats AUTO plus four zero address slots as learning enabled. The original hardware settings were AUTO with all four slots set to `FF:FF:FF:FF:FF:FF`; those values differ from the learning state.

For WIDI Master, the SysEx header is `F0 00 20 63 0F 09`. Body commands are **02** to read a register and **03** to write one. Each decoded data byte is encoded as **low nibble, high nibble**. The checksum is the sum of the command and body data bytes, masked with `7F`; it excludes the header and final `F7`.

```text
Read forced role:
F0 00 20 63 0F 09 02 06 01 09 F7

Write forced role AUTO:
F0 00 20 63 0F 09 03 06 01 00 00 0A F7

Read address slot 1:
F0 00 20 63 0F 09 02 07 06 0F F7

Clear address slot 1:
F0 00 20 63 0F 09 03 07 06 00 00 00 00 00 00 00 00 00 00 00 00 10 F7

Request control-link disconnect:
F0 00 20 63 0F 09 00 00 00 F7
```

For slots 2–4, replace register `07` with `08`, `09` or `0A`, and increment the checksum correspondingly. Device replies set bit `40` on the command: a read reply uses `42`, a write reply `43`. Reply checksums include that direction bit.

The actual role readback was:

```text
F0 00 20 63 0F 09 42 06 01 00 00 49 F7
```

The declared size is one decoded byte; its payload contains two nibbles. The helper checks response framing, direction and checksum, consumes both nibbles per byte, and verifies the post-write values. It does not copy the app's apparent READ_REGISTER slicing quirk.

BLE MIDI adds a timestamp-high header and timestamp-low byte before the first SysEx byte, plus another timestamp-low before the terminal `F7`. Long commands span packets; continuation packets begin with the timestamp-high byte. The helper uses the app's conservative MTU-23 fragmentation, covered by fixed command/packet regression tests.

## Relevant decompiled locations

These paths are relative to the retained `build/widi-research/jadx/sources/` tree:

| Question | Class / method |
| --- | --- |
| Enable switch | `com/cme/widimaster/android/presentation/device/detail/DeviceViewModel.java`, `enableAutoLearnGroup` |
| Clear four slots | Same class, `setPredefinedAddressesToZero`, associated completion coroutine |
| Register encoding/checksum | `com/cme/widible/domain/sysex/SysexMidiImplementation.java`, register writers and `sendSysexWithCommand` |
| BLE framing/reassembly | `com/cme/widible/domain/sysex/SysexSplitter.java` |
| Register numbers | `com/cme/widible/domain/sysex/Constants.java` |
| WIDI Master product identifier | `com/cme/widible/domain/product/Product.java` |

The independent manual-group investigation is retained in `build/widi-research/manual-group-recovery.json`. Its address-order ambiguity was resolved by comparing the WIDI's serial representation, observed BLE addresses and the saved Core2 slot.

Official context: [CME's app guide](https://www.cme-pro.com/cme-widi-app-online-start-guide/) describes third-party peripherals and macOS MIDI advertising; [CME's support guide](https://www.cme-pro.com/support/) describes macOS automatic reconnection and forgetting old pairings. The software save procedure above avoids reliance on the hardware controls described in legacy manuals.
