"""Independent checks for the audit's timing and evidence handling.

Run with the audit's isolated dependencies:
  uv run --python 3.12 --with mido python -m unittest discover -s tests -p test_lesson_audit.py
"""

import importlib.util
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("audit_lessons", ROOT / "tools/audit_lessons.py")
audit = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = audit
SPEC.loader.exec_module(audit)


class ScoreLocations(unittest.TestCase):
    def read(self, body):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "score.musicxml"
            path.write_text(f'<score-partwise><part id="P1">{body}</part></score-partwise>')
            return audit.read_xml(path)

    def test_voices_chords_and_held_notes(self):
        notes, _ = self.read('''<measure number="7"><attributes><divisions>2</divisions></attributes>
          <note><pitch><step>C</step><octave>3</octave></pitch><duration>8</duration><staff>2</staff></note>
          <backup><duration>8</duration></backup>
          <note><pitch><step>E</step><octave>4</octave></pitch><duration>4</duration></note>
          <note><chord/><pitch><step>G</step><octave>4</octave></pitch><duration>4</duration></note>
          <forward><duration>2</duration></forward>
          <note><pitch><step>B</step><octave>4</octave></pitch><duration>2</duration></note>
        </measure>''')
        self.assertEqual([(n.pitch, n.offset, n.duration) for n in notes],
                         [(48, 0, 4), (64, 0, 2), (67, 0, 2), (71, 3, 1)])
        self.assertEqual([n.pitch for n in notes if n.offset <= 3 < n.offset + n.duration], [48, 71])

    def test_divisions_repair_and_transposing_instrument(self):
        notes, stats = self.read('''<measure number="1"><attributes><divisions>0</divisions>
          <transpose><chromatic>-2</chromatic><octave-change>-1</octave-change></transpose></attributes>
          <note><pitch><step>C</step><alter>1</alter><octave>4</octave></pitch><duration>4</duration></note>
          </measure><measure number="2"><attributes><divisions>4</divisions></attributes></measure>''')
        self.assertEqual((notes[0].pitch, notes[0].duration), (47, 1))
        self.assertEqual(stats["zero_divisions"], 1)


class MidiTiming(unittest.TestCase):
    def test_tempo_change_and_same_tick_note_replacement(self):
        import mido
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "score.mid"
            midi = mido.MidiFile(ticks_per_beat=480)
            track = mido.MidiTrack()
            midi.tracks.append(track)
            track.extend([mido.Message("note_on", note=60, velocity=90),
                          mido.Message("note_on", note=64, velocity=90),
                          mido.MetaMessage("set_tempo", tempo=1_000_000, time=480),
                          mido.Message("note_off", note=64, time=480),
                          mido.Message("note_on", note=67, velocity=90),
                          mido.Message("note_off", note=60, time=480),
                          mido.Message("note_off", note=67)])
            midi.save(path)
            result = audit.read_midi(path, 85)
        self.assertEqual([(s["notes"], s["start_ms"], s["duration_ms"], s["quarter_beat"])
                          for s in result["slices"]],
                         [((60, 64), 0, 1500, 0), ((60, 67), 1500, 1000, 2)])
        self.assertEqual(result["notes_still_sounding_at_eof"], [])

    def test_corrupt_zero_length_order_is_visible(self):
        import mido
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.mid"
            midi = mido.MidiFile()
            midi.tracks.append(mido.MidiTrack([mido.Message("note_off", note=60),
                                              mido.Message("note_on", note=60)]))
            midi.save(path)
            result = audit.read_midi(path, 85)
        self.assertEqual(result["unmatched_offs"], 1)
        self.assertEqual(result["notes_still_sounding_at_eof"], [60])


class MidiStateParity(unittest.TestCase):
    def test_offline_voice_state_matches_firmware(self):
        binary = Path(os.environ.get("AUDIT_BINARY", ROOT / "build/chord_audit"))
        if not binary.exists():
            self.skipTest("build chord_audit or run the lesson audit first")
        rng = random.Random(817)
        events = [(0xFF, 0, 0)]
        events += [(0x90, 60, 100)] * 260 + [(0x80, 60, 0)] * 260
        for _ in range(5000):
            kind = rng.choice([0x80, 0x90, 0x90, 0xB0])
            channel = rng.randrange(3)
            data1 = rng.choice([64, 120, 121, 123, 124, 127]) if kind == 0xB0 else rng.randrange(48, 73)
            data2 = rng.choice([0, 30, 63, 64, 100, 127])
            events.append((kind | channel, data1, data2))
        state = audit.SoundingNotes()
        expected = []
        for status, data1, data2 in events:
            kind = status & 0xF0
            if status == 0xFF:
                message = SimpleNamespace(type="reset")
            elif kind == 0xB0:
                message = SimpleNamespace(type="control_change", channel=status & 15,
                                          control=data1, value=data2)
            else:
                message = SimpleNamespace(type="note_on" if kind == 0x90 else "note_off",
                                          channel=status & 15, note=data1, velocity=data2)
            state.receive(message)
            expected.append((list(state.notes()), bool(state.pedal)))
        result = subprocess.run([str(binary), "--events"], input="".join(
            f"{i} {s} {a} {b}\n" for i, (s, a, b) in enumerate(events)),
            text=True, capture_output=True, check=True)
        actual = [(row["notes"], row["pedal"]) for row in map(json.loads, result.stdout.splitlines())]
        self.assertEqual(len(actual), len(expected))
        for i, (actual_state, expected_state) in enumerate(zip(actual, expected)):
            self.assertEqual(actual_state, expected_state, f"event {i}: {events[i]}")


class ChordEvidence(unittest.TestCase):
    def test_equivalent_spelling_requires_actual_quality_evidence(self):
        patterns = [{"type": "dominant7#11", "intervals": [0,4,7,10,18],
                     "required": [0,4,10,18], "optional": [7], "required_any": []}]
        fixture = {"category": "ambiguity", "notes": [58,64,66], "expected_root": 0,
                   "allowed_types": ["dominant7#11"]}
        result = {"root": 6, "type": "dominant7", "alternatives": [{"root": 0, "type": "dominant7b5"}]}
        assessment = audit.assess_fixture(fixture,result,patterns)
        self.assertFalse(assessment["candidate_type_match"])
        self.assertTrue(assessment["candidate_evidence_match"])
        self.assertTrue(assessment["expectation_met"])
        fixture["notes"] = [58,66]  # no third, only two classes
        fixture["category"] = "capability_gap"
        assessment = audit.assess_fixture(fixture,result,patterns)
        self.assertFalse(assessment["candidate_evidence_match"])
        self.assertFalse(assessment["expectation_met"])

    def test_native_probes_respect_their_evidence_categories(self):
        binary = Path(os.environ.get("AUDIT_BINARY", ROOT / "build/chord_audit"))
        if not binary.exists():
            self.skipTest("build chord_audit first")
        patterns = [json.loads(line) for line in subprocess.check_output([str(binary), "--patterns"], text=True).splitlines()]
        fixtures = json.loads((ROOT / "tests/fixtures/lesson_chords.json").read_text())
        detections = audit.run_detector(binary,[tuple(f["notes"]) for f in fixtures])
        for fixture in fixtures:
            with self.subTest(fixture=fixture["id"]):
                assessment = audit.assess_fixture(fixture,detections[tuple(fixture["notes"])],patterns)
                self.assertTrue(assessment["expectation_met"],assessment)


if __name__ == "__main__":
    unittest.main()
