import unittest

from tools.widi_control import Decoder, nibbles, packets, parse_reply, sysex


class WidiProtocolTests(unittest.TestCase):
    def test_read_role_matches_recovered_command(self):
        self.assertEqual(sysex(2, [6, 1]).hex(" "), "f0 00 20 63 0f 09 02 06 01 09 f7")

    def test_write_auto_matches_recovered_command(self):
        self.assertEqual(sysex(3, [6, 1, 0, 0]).hex(" "), "f0 00 20 63 0f 09 03 06 01 00 00 0a f7")

    def test_actual_hardware_read_reply(self):
        frame = bytes.fromhex("f0 00 20 63 0f 09 42 06 01 00 00 49 f7")
        self.assertEqual(parse_reply(frame), (2, 6, 1, bytes(2)))

    def test_response_checksum_includes_direction_bit(self):
        bad = bytes.fromhex("f0 00 20 63 0f 09 42 06 01 00 00 09 f7")
        with self.assertRaises(ValueError):
            parse_reply(bad)

    def test_low_nibble_first(self):
        self.assertEqual(nibbles(bytes.fromhex("12 34 56 78 9a bc")), bytes.fromhex("02 01 04 03 06 05 08 07 0a 09 0c 0b"))

    def test_hardware_verified_disconnect_command(self):
        self.assertEqual(sysex(0, [0]).hex(" "), "f0 00 20 63 0f 09 00 00 00 f7")

    def test_app_mtu23_address_write_fragments(self):
        frame = sysex(3, bytes([7, 6]) + bytes(12))
        result = packets(frame, 0)
        self.assertEqual([len(p) for p in result], [19, 8])
        self.assertEqual(result[0].hex(" "), "80 80 f0 00 20 63 0f 09 03 07 06 00 00 00 00 00 00 00 00")
        self.assertEqual(result[1].hex(" "), "80 00 00 00 00 10 80 f7")

    def test_fragmented_reply_and_realtime_interleave(self):
        frame = sysex(0x42, bytes([7, 6]) + bytes([15]) * 12)
        fragments = packets(frame, 0x70)  # timestamp-low F0 must not become SysEx start
        decoder = Decoder()
        self.assertEqual(decoder.feed(fragments[0]), [])
        self.assertEqual(decoder.feed(bytes.fromhex("80 f7 fe")), [])
        self.assertEqual(decoder.feed(fragments[1]), [frame])

    def test_error_reply(self):
        with self.assertRaises(RuntimeError):
            parse_reply(sysex(0x43, [0x7E]))


if __name__ == "__main__":
    unittest.main()
