#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["bleak==3.1.0"]
# ///
"""Read WIDI Master settings or configure its group without the phone UI.

Usage: uv run tools/widi_control.py status
       uv run tools/widi_control.py learn
       uv run tools/widi_control.py group --peripheral AA:BB:CC:DD:EE:01

Protocol recovered from CME WIDI Android 3.0.4; see docs/widi-protocol.md.
Both setup commands back up the existing role/address registers before writing.
"""

import argparse
import asyncio
import ctypes
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import time

MIDI_SERVICE = "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
MIDI_CHARACTERISTIC = "7772e5db-3868-4112-a1a9-f2669d106bf3"
HEADER = bytes.fromhex("f0 00 20 63 0f 09")
REGISTERS = {6: 1, 7: 6, 8: 6, 9: 6, 10: 6}


def sysex(command, data):
    body = bytes([command]) + bytes(data)
    if any(value > 127 for value in body):
        raise ValueError("SysEx data must be seven-bit bytes")
    return HEADER + body + bytes([sum(body) & 127, 0xF7])


def nibbles(data):
    return bytes(part for value in data for part in (value & 15, value >> 4))


def packets(frame, stamp, mtu=23):
    """Match the app's SysexSplitter, including its conservative MTU budget."""
    if not frame.startswith(HEADER) or frame[-1] != 0xF7:
        raise ValueError("Not a WIDI Master SysEx frame")
    body = frame[1:-1]
    if len(frame) + 3 < mtu - 3:
        pieces = [body]
    else:
        first = min(mtu - 7, len(body) - 1)
        pieces = [body[:first]]
        offset = first
        while offset < len(body):
            remaining = len(body) - offset
            size = remaining if remaining + 3 <= mtu - 4 else mtu - 6
            pieces.append(body[offset:offset + size])
            offset += size
    high, low = 0x80 | ((stamp >> 7) & 63), 0x80 | (stamp & 127)
    result = []
    for index, piece in enumerate(pieces):
        packet = bytes([high, low, 0xF0]) + piece if index == 0 else bytes([high]) + piece
        if index == len(pieces) - 1:
            packet += bytes([low, 0xF7])
        result.append(packet)
    return result


class Decoder:
    """Collect SysEx across BLE packets, ignoring timestamps and normal MIDI."""
    def __init__(self):
        self.partial = bytearray()

    def feed(self, packet):
        result = []
        index = 1  # BLE timestamp-high header
        while index < len(packet):
            value = packet[index]
            if value < 128:
                if self.partial:
                    self.partial.append(value)
                index += 1
                continue
            index += 1  # timestamp-low, even when its value equals F0/F7
            if index == len(packet):
                break
            status = packet[index]
            if status == 0xF0:
                self.partial = bytearray([0xF0])
                index += 1
            elif status == 0xF7:
                if self.partial:
                    self.partial.append(0xF7)
                    result.append(bytes(self.partial))
                    self.partial.clear()
                index += 1
            elif status >= 128:
                if status < 0xF8:
                    self.partial.clear()
                index += 1
            # A low byte after a timestamp is continued SysEx/running data.
        return result


def parse_reply(frame):
    if not frame.startswith(HEADER) or len(frame) < 10 or frame[-1] != 0xF7:
        raise ValueError("Invalid WIDI reply framing")
    if not frame[6] & 0x40:
        raise ValueError("Expected a device-to-host reply")
    if sum(frame[6:-2]) & 127 != frame[-2]:
        raise ValueError("WIDI reply checksum mismatch")
    if len(frame) == 10 and frame[7] >= 0x7A:
        raise RuntimeError(f"WIDI rejected request: error 0x{frame[7]:02x}")
    return frame[6] & 15, frame[7], frame[8], frame[9:-2]


async def find_device(name, timeout):
    from bleak import BleakScanner
    scanner = BleakScanner(service_uuids=[MIDI_SERVICE])
    await scanner.start()
    try:
        # macOS can retain a MIDI connection after its client exits. Such peers
        # stop advertising and cannot be recovered through an ordinary scan.
        if sys.platform == "darwin":
            from CoreBluetooth import CBUUID
            from bleak.backends.device import BLEDevice
            manager = scanner._backend._manager
            peers = manager.central_manager.retrieveConnectedPeripheralsWithServices_(
                [CBUUID.UUIDWithString_(MIDI_SERVICE)])
            for peer in peers:
                identifier = str(peer.identifier().UUIDString())
                if name.lower() in str(peer.name()).lower() or name.lower() == identifier.lower():
                    return BLEDevice(identifier, str(peer.name()), (peer, manager))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for device in scanner.discovered_devices:
                if name.lower() in (device.name or "").lower() or name.lower() == device.address.lower():
                    return device
            await asyncio.sleep(0.1)
    finally:
        await scanner.stop()
    raise RuntimeError("WIDI Master is unavailable for configuration. Disconnect the phone's Bluetooth. "
                       "If its saved group is connected, temporarily switch off the group peripherals "
                       "and stop Mac MIDI advertising so the WIDI becomes available for configuration.")


def release_mac_midi_connection(identifier):
    """Release just this outgoing CoreMIDI link before WIDI takes central role."""
    if sys.platform != "darwin":
        return
    foundation = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
    midi = ctypes.CDLL("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI")
    foundation.CFStringCreateWithCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
    foundation.CFStringCreateWithCString.restype = ctypes.c_void_p
    foundation.CFRelease.argtypes = [ctypes.c_void_p]
    midi.MIDIBluetoothDriverDisconnect.argtypes = [ctypes.c_void_p]
    midi.MIDIBluetoothDriverDisconnect.restype = ctypes.c_int32
    string = foundation.CFStringCreateWithCString(None, identifier.encode(), 0x08000100)
    try:
        status = midi.MIDIBluetoothDriverDisconnect(string)
        if status:
            raise RuntimeError(f"Could not release outgoing Mac MIDI connection: OSStatus {status}")
    finally:
        foundation.CFRelease(string)


class Control:
    def __init__(self, client):
        self.client = client
        self.decoder = Decoder()
        self.replies = asyncio.Queue()
        self.transcript = []

    def receive(self, _, packet):
        for frame in self.decoder.feed(packet):
            if frame.startswith(HEADER):
                self.transcript.append({"rx": frame.hex(" ")})
                self.replies.put_nowait(frame)

    async def request(self, command, data):
        frame = sysex(command, data)
        self.transcript.append({"tx": frame.hex(" ")})
        stamp = int(time.monotonic() * 1000) & 8191
        for packet in packets(frame, stamp):
            await self.client.write_gatt_char(MIDI_CHARACTERISTIC, packet, response=False)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            reply = await asyncio.wait_for(self.replies.get(), deadline - time.monotonic())
            reply_command, register, size, payload = parse_reply(reply)
            if reply_command == command and register == data[0]:
                return size, payload
        raise TimeoutError("WIDI response timed out")

    async def read(self, register):
        size, payload = await self.request(2, [register, REGISTERS[register]])
        expected = REGISTERS[register]
        if len(payload) != expected * 2 or any(value > 15 for value in payload):
            raise ValueError(f"Unexpected register {register} payload: size={size}, {payload.hex(' ')}")
        return bytes(payload[i] | (payload[i+1] << 4) for i in range(0, len(payload), 2))

    async def write(self, register, value):
        size, payload = await self.request(3, bytes([register, len(value)]) + nibbles(value))
        print(f"Write acknowledged: register {register}, size {size}, payload {payload.hex(' ')}", flush=True)

    async def status(self):
        result = {}
        for register in REGISTERS:
            value = await self.read(register)
            result[str(register)] = value.hex(" ")
            print(f"Register {register}: {value.hex(' ')}", flush=True)
        return result

    async def release(self):
        # The app closes its Android transport. macOS may keep a MIDI connection
        # alive for another client, so explicitly request the device disconnect.
        # Verified on this WIDI Master: it releases the Mac and joins the Core2.
        frame = sysex(0, [0])
        self.transcript.append({"tx": frame.hex(" "), "purpose": "release control connection"})
        for packet in packets(frame, int(time.monotonic() * 1000) & 8191):
            await self.client.write_gatt_char(MIDI_CHARACTERISTIC, packet, response=False)
        await asyncio.sleep(0.5)


async def run(args):
    from bleak import BleakClient
    device = await find_device(args.device, args.timeout)
    print(f"Connecting: {device.name} [{device.address}]", flush=True)
    transcript = []
    async with BleakClient(device, timeout=args.timeout) as client:
        model = bytes(await client.read_gatt_char("00002a24-0000-1000-8000-00805f9b34fb")).decode(errors="replace")
        if model != "CME WIDI Master":
            raise RuntimeError(f"This tool targets CME WIDI Master; received model {model!r}")
        firmware = bytes(await client.read_gatt_char("00002a26-0000-1000-8000-00805f9b34fb")).decode(errors="replace")
        print(f"Model: {model}; firmware field: {firmware}", flush=True)
        control = Control(client)
        await client.start_notify(MIDI_CHARACTERISTIC, control.receive)
        before = await control.status()
        if args.action in ("learn", "group"):
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
            backup = args.backup or Path(f"build/widi-research/group-backup-{stamp}.json")
            backup.parent.mkdir(parents=True, exist_ok=True)
            backup.write_text(json.dumps({"device":device.address, "model":model, "firmware":firmware,
                "registers":before, "transcript":control.transcript}, indent=2) + "\n")
            print(f"Saved group settings: {backup}", flush=True)
            if args.action == "learn":
                await control.write(6, bytes([0]))
                for register in (7, 8, 9, 10):
                    await control.write(register, bytes(6))
                expected = {str(register): bytes(size).hex(" ") for register, size in REGISTERS.items()}
            else:
                # Match the app's manual group central setup. Addresses are
                # little-endian controller bytes, corroborated by the WIDI's
                # serial and the Core2 address learned/saved on real hardware.
                expected = {"6": "00"}
                for register in (7, 8, 9, 10):
                    await control.write(register, bytes([255]) * 6)
                    expected[str(register)] = (bytes([255]) * 6).hex(" ")
                await control.write(6, bytes([0]))
                for index, address in enumerate(args.peripheral):
                    await control.write(7 + index, address[::-1])
                    expected[str(7 + index)] = address[::-1].hex(" ")
            after = await control.status()
            if after != expected:
                raise RuntimeError("Group readback differs from the expected role/address values")
            if args.action == "learn":
                print("Verified: role AUTO and all four address slots zero; Group Auto-learn enabled", flush=True)
            else:
                print(f"Verified saved group for {len(args.peripheral)} peripherals", flush=True)
            await asyncio.sleep(1)
            release_mac_midi_connection(device.address)
            await control.release()
        transcript = control.transcript
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(json.dumps({"device":device.address,"action":args.action,"transcript":transcript},indent=2)+"\n")
    print("Disconnected control client", flush=True)
    if args.action == "learn":
        print("Discovery is enabled. To save known members in software, use group --peripheral MAC "
              "for each member; see docs/widi-protocol.md.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["status", "learn", "group"])
    parser.add_argument("--device", default="WIDI Master")
    parser.add_argument("--timeout", type=float, default=12)
    parser.add_argument("--backup", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--peripheral", action="append", default=[], help="Known public BLE MAC address; repeat for group members")
    args = parser.parse_args()
    try:
        args.peripheral = [bytes.fromhex(value.replace(":", "")) for value in args.peripheral]
    except ValueError:
        parser.error("Peripheral addresses must be six-byte hexadecimal MAC addresses")
    if args.action == "group" and (not 1 <= len(args.peripheral) <= 4 or any(len(value) != 6 for value in args.peripheral)):
        parser.error("group requires one to four --peripheral MAC addresses")
    if args.action == "group" and (len(set(args.peripheral)) != len(args.peripheral) or
            any(value in (bytes(6), bytes([255]) * 6) for value in args.peripheral)):
        parser.error("Group members must be distinct device addresses, not wildcard/empty slots")
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
