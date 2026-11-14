#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = ["mido==1.3.3"]
# ///
"""Audit the production chord analyzer against a local lesson archive.

Detailed results stay in ignored build/; no lesson PDFs or MIDI are copied.
Run: uv run --python 3.12 tools/audit_lessons.py
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
from fractions import Fraction
import hashlib
from itertools import groupby
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

REPO = Path(__file__).resolve().parent.parent
PC_NAMES = "C Db D Eb E F Gb G Ab A Bb B".split()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_manifest(root=REPO):
    paths = sorted((root / "lib").rglob("*.cpp")) + sorted((root / "lib").rglob("*.h"))
    paths += [root / "tools/chord_audit.cpp", root / "CMakeLists.txt"]
    files = {str(p.relative_to(root)): digest(p) for p in paths}
    checksum = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    return {"sha256": checksum, "files": files}


def snapshot_source(destination):
    """Keep concurrent firmware edits from changing a long audit's baseline."""
    before = source_manifest()
    paths = [REPO / name for name in before["files"]]
    for directory in ("tests", "tools"):
        for extension in ("*.cpp", "*.py"):
            paths += list((REPO / directory).glob(extension))
    for path in paths:
        target = destination / path.relative_to(REPO)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
    if source_manifest(destination) != before:
        raise RuntimeError("analyzer changed while copying the source snapshot; rerun")
    return before


def run_detector(binary, note_sets):
    requests = "".join(f"{i} {' '.join(map(str, notes))}\n" for i, notes in enumerate(note_sets))
    result = subprocess.run([str(binary)], input=requests, capture_output=True, text=True, check=True)
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    if [row["id"] for row in rows] != list(range(len(note_sets))):
        raise ValueError("analyzer did not return every request in order")
    return dict(zip(note_sets, rows))


class SoundingNotes:
    """Offline counterpart of MidiState; preserve channels, duplicates and CCs."""

    def __init__(self):
        self.held = Counter()
        self.sustained = set()
        self.pedal = set()
        self.unmatched_offs = 0

    def receive(self, message):
        kind = message.type
        if kind == "reset":
            self.held.clear()
            self.sustained.clear()
            self.pedal.clear()
            return
        channel = getattr(message, "channel", 0)
        if kind in ("note_on", "note_off"):
            key = channel, message.note
            if kind == "note_on" and message.velocity:
                self.held[key] = min(255, self.held[key] + 1)
                self.sustained.discard(key)
            elif self.held[key]:
                self.held[key] -= 1
                if not self.held[key] and channel in self.pedal:
                    self.sustained.add(key)
            else:
                self.unmatched_offs += 1
        elif kind == "control_change":
            cc = message.control
            if cc == 64:
                if message.value >= 64:
                    self.pedal.add(channel)
                else:
                    self.pedal.discard(channel)
                    self.sustained = {k for k in self.sustained if k[0] != channel}
            elif cc in (120, 121):
                self.sustained = {k for k in self.sustained if k[0] != channel}
                if cc == 120:
                    self.held = Counter({k: v for k, v in self.held.items() if k[0] != channel})
                else:
                    self.pedal.discard(channel)
            elif cc == 123 or 124 <= cc <= 127:
                for key in list(self.held):
                    if key[0] == channel:
                        if self.held[key] and channel in self.pedal:
                            self.sustained.add(key)
                        self.held[key] = 0

    def notes(self):
        return tuple(sorted({n for (c, n), count in self.held.items() if count} |
                            {n for c, n in self.sustained}))


def read_midi(path, minimum_ms):
    import mido

    midi = mido.MidiFile(path)
    if midi.type == 2:
        raise ValueError("asynchronous type-2 MIDI is not a single performance")
    tempo = 500_000
    tick = 0
    seconds = 0.0
    events = []
    counts = Counter()
    for message in mido.merge_tracks(midi.tracks):
        seconds += mido.tick2second(message.time, midi.ticks_per_beat, tempo)
        tick += message.time
        if message.type == "set_tempo":
            tempo = message.tempo
        elif message.type in ("note_on", "note_off", "control_change", "reset"):
            events.append((tick, seconds, message))
            counts[message.type] += 1
            if message.type == "note_on" and message.velocity and message.channel == 9:
                counts["channel_10_note_ons"] += 1
            if message.type == "control_change" and message.control == 64:
                counts["sustain_events"] += 1
    state = SoundingNotes()
    slices = []
    previous_seconds = 0.0
    previous_tick = 0
    discarded = Counter()

    def emit(end_seconds):
        notes = state.notes()
        duration_ms = (end_seconds - previous_seconds) * 1000
        classes = len({n % 12 for n in notes})
        if classes < 2:
            discarded["silence_or_single_pitch"] += 1
        elif duration_ms + 1e-6 < minimum_ms:
            discarded["short_polyphonic_segments"] += 1
        else:
            slices.append({"start_ms": round(previous_seconds * 1000, 3),
                           "duration_ms": round(duration_ms, 3),
                           "quarter_beat": round(previous_tick / midi.ticks_per_beat, 6),
                           "notes": notes, "pitch_classes": classes})

    for at_tick, grouped in groupby(events, key=lambda item: item[0]):
        group = list(grouped)
        at_seconds = group[0][1]
        emit(at_seconds)
        for _, _, message in group:
            state.receive(message)
        previous_seconds, previous_tick = at_seconds, at_tick
    if seconds > previous_seconds:
        emit(seconds)
    return {"slices": slices, "events": dict(counts), "discarded": dict(discarded),
            "unmatched_offs": state.unmatched_offs,
            "notes_still_sounding_at_eof": list(state.notes()),
            "duration_ms": round(seconds * 1000, 3), "tracks": len(midi.tracks)}


@dataclass
class XmlNote:
    part: str
    measure: str
    offset: Fraction
    duration: Fraction
    pitch: int
    staff: str


def read_xml(path):
    """Read literal OMR pitches with per-measure backup/forward/chord timing.

    Locations are score coordinates, not a promise of MIDI beat alignment:
    music21 can expand repeats and pad malformed measures during conversion.
    """
    root = ET.parse(path).getroot()
    nonzero = Counter(int(e.text) for e in root.iter("divisions") if e.text and int(e.text) > 0)
    fallback = nonzero.most_common(1)[0][0] if nonzero else 1
    stats = {"notes": 0, "rests": 0, "unpitched": 0, "zero_divisions": 0,
             "harmonies": len(root.findall(".//harmony")), "parts": len(root.findall("part")),
             "measures": len(root.findall(".//measure"))}
    notes = []
    for part in root.findall("part"):
        divisions = fallback
        transpose = 0
        for measure in part.findall("measure"):
            cursor = Fraction(0)
            last_onset = Fraction(0)
            for element in measure:
                if element.tag == "attributes":
                    value = element.findtext("divisions")
                    if value is not None:
                        divisions = int(value)
                        if divisions <= 0:
                            stats["zero_divisions"] += 1
                            divisions = fallback
                    value = element.find("transpose")
                    if value is not None:
                        transpose = int(value.findtext("chromatic", "0")) + 12 * int(value.findtext("octave-change", "0"))
                elif element.tag in ("backup", "forward"):
                    duration = Fraction(element.findtext("duration", "0")) / divisions
                    cursor += duration * (-1 if element.tag == "backup" else 1)
                elif element.tag == "note":
                    duration = Fraction(element.findtext("duration", "0")) / divisions
                    chord = element.find("chord") is not None
                    onset = last_onset if chord else cursor
                    if not chord:
                        last_onset = onset
                        cursor += duration
                    pitch = element.find("pitch")
                    if pitch is not None:
                        alter = Fraction(pitch.findtext("alter", "0"))
                        if alter.denominator != 1:
                            raise ValueError("microtonal pitch cannot be tested as integer MIDI")
                        number = ((int(pitch.findtext("octave")) + 1) * 12 +
                                  dict(C=0, D=2, E=4, F=5, G=7, A=9, B=11)[pitch.findtext("step")] +
                                  int(alter) + transpose)
                        notes.append(XmlNote(part.get("id"), measure.get("number"), onset,
                                             duration, number, element.findtext("staff", "1")))
                        stats["notes"] += 1
                    elif element.find("rest") is not None:
                        stats["rests"] += 1
                    else:
                        stats["unpitched"] += 1
    return notes, stats


def fixture_xml_notes(fixture, root):
    notes, _ = read_xml(root / fixture["musicxml"])
    location = fixture["location"]
    offset = Fraction(str(location.get("offset", 0)))
    return sorted({note.pitch for note in notes
                   if note.measure == str(location["measure"])
                   and ("part" not in location or note.part == location["part"])
                   and ("staff" not in location or note.staff == str(location["staff"]))
                   and note.offset <= offset < note.offset + note.duration})


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def supported_requested_types(fixture, patterns):
    """Check the intended symbol's evidence independently of the chosen name.

    This admits enharmonic b5/#11 or #5/b13 readings, without admitting a
    missing defining third, seventh or altered fifth. It does not choose roots.
    """
    intervals = {(note - fixture["expected_root"]) % 12 for note in fixture["notes"]}
    inferred = 0 not in intervals
    supported = []
    for pattern in patterns:
        if pattern["type"] not in fixture["allowed_types"]:
            continue
        tones = {n % 12 for n in pattern["intervals"]}
        required = {n % 12 for n in pattern["required"]}
        optional = {n % 12 for n in pattern["optional"]}
        any_required = {n % 12 for n in pattern.get("required_any", [])}
        if inferred:
            third = intervals & tones & {3, 4}
            seventh = intervals & tones & {10, 11}
            six_nine = not tones & {10, 11} and {2, 9} <= intervals & tones
            if len(intervals) < 3 or not third or not (seventh or six_nine):
                continue
            required.discard(0)
        if (required <= intervals and intervals <= tones | optional and
                (not any_required or intervals & any_required)):
            supported.append(pattern["type"])
    return supported


def assess_fixture(fixture, result, patterns):
    readings = ([result] if result["root"] is not None else []) + result.get("alternatives", [])
    primary_match = result["root"] == fixture["expected_root"] and result["type"] in fixture["allowed_types"]
    type_match = any(r["root"] == fixture["expected_root"] and r["type"] in fixture["allowed_types"] for r in readings)
    supported = supported_requested_types(fixture, patterns)
    equivalent = bool(supported) and any(r["root"] == fixture["expected_root"] for r in readings)
    category = fixture["category"]
    if category == "control":
        assessment, met = "literal_control", primary_match
    elif category in ("capability_gap", "input_corruption", "ambiguity"):
        if supported:
            assessment, met = "supported_reading", equivalent
        else:
            # Some named diminished shells also omit their quality-defining
            # altered fifth. A plausible interpretation cannot prove that tone.
            assessment, met = "insufficient_quality", not type_match
            if category != "ambiguity":
                met = False
    elif category == "insufficient_evidence":
        assessment, met = "insufficient_quality", not supported and not type_match
    elif category == "harmonic_substitution":
        assessment, met = "literal_substitution", result["root"] is not None and not supported and not type_match
    else:
        raise ValueError(f"unknown fixture category: {category}")
    return {"matches_workbook": primary_match, "candidate_type_match": type_match,
            "candidate_evidence_match": equivalent, "supported_requested_types": supported,
            "assessment": assessment, "expectation_met": bool(met)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=Path.home() / "git/midi-tutor-mcp/data/openstudiojazz")
    parser.add_argument("--out", type=Path, default=REPO / "build/lesson-audit")
    parser.add_argument("--fixtures", type=Path, default=REPO / "tests/fixtures/lesson_chords.json")
    parser.add_argument("--minimum-ms", type=float, default=85)
    args = parser.parse_args()
    if args.minimum_ms <= 0:
        parser.error("--minimum-ms must be positive")
    root, out = args.archive.expanduser().resolve(), args.out.resolve()
    if not (root / "lessons-index.json").is_file():
        parser.error(f"lesson archive not found: {root}")
    out.mkdir(parents=True, exist_ok=True)
    script_hash = digest(Path(__file__))
    fixture_hash = digest(args.fixtures) if args.fixtures.exists() else None
    source = out / "source"
    before = snapshot_source(source)
    write_json(out / "source-manifest.json", before)
    build = out / "native-snapshot"
    subprocess.run(["cmake", "-S", str(source), "-B", str(build)], check=True, stdout=sys.stderr)
    subprocess.run(["cmake", "--build", str(build), "--target", "chord_audit", "-j", "4"], check=True, stdout=sys.stderr)
    binary = build / "chord_audit"
    if source_manifest(source) != before:
        raise RuntimeError("analyzer source changed during build; rerun for a consistent snapshot")
    patterns = [json.loads(line) for line in subprocess.check_output([str(binary), "--patterns"], text=True).splitlines()]
    units = json.loads((root / "lessons-index.json").read_text())["units"]
    unit_by_midi = {u["artifacts"]["midi"]: u for u in units if u["artifacts"].get("midi")}
    inventory = {}
    hashes = {}
    for kind, directory, extension in (("pdf", "raw/worksheets", "pdf"), ("musicxml", "omr/musicxml", "musicxml"), ("midi", "omr/midi", "mid")):
        paths = sorted((root / directory).rglob(f"*.{extension}"))
        groups = defaultdict(list)
        for path in paths:
            groups[digest(path)].append(str(path.relative_to(root)))
        hashes[kind] = groups
        inventory[kind] = {"files": len(paths), "unique_sha256": len(groups)}
    print(f"Inventory: {inventory}", file=sys.stderr)
    write_json(out / "duplicates.json", {kind: dict(groups) for kind, groups in hashes.items()})
    xml_stats = {}
    errors = []
    for i, paths in enumerate(hashes["musicxml"].values()):
        try:
            _, stats = read_xml(root / paths[0])
            xml_stats[paths[0]] = stats
        except Exception as error:
            errors.append({"stage": "musicxml", "file": paths[0], "error": str(error)})
    write_json(out / "xml-quality.json", xml_stats)
    performances = []
    midi_voicings = {}
    note_sets = set()
    for index, (sha, aliases) in enumerate(hashes["midi"].items(), 1):
        path = aliases[0]
        try:
            performance = read_midi(root / path, args.minimum_ms)
            performance.update(file=path, sha256=sha, aliases=aliases)
            performances.append(performance)
            voicings = {s["notes"] for s in performance["slices"]}
            note_sets.update(voicings)
            for alias in aliases:
                midi_voicings[alias] = voicings
        except Exception as error:
            errors.append({"stage": "midi", "file": path, "error": str(error)})
        if index % 200 == 0:
            print(f"Read {index}/{len(hashes['midi'])} unique MIDI files; {len(note_sets)} note sets", file=sys.stderr)
    fixtures = json.loads(args.fixtures.read_text()) if args.fixtures.exists() else []
    note_sets.update(tuple(sorted(set(f["notes"]))) for f in fixtures)
    ordered_notes = sorted(note_sets)
    print(f"Analyzing {len(ordered_notes)} distinct voicings with the production core", file=sys.stderr)
    detections = run_detector(binary, ordered_notes)
    write_json(out / "detections.json", list(detections.values()))
    totals = Counter()
    by_course = defaultdict(Counter)
    by_size = defaultdict(Counter)
    clean_pitched = Counter()
    integrity_files = 0
    channel_10_files = 0
    unknown = defaultdict(lambda: {"count": 0, "ms": 0, "examples": []})
    with (out / "slices.jsonl").open("w") as stream:
        for performance in performances:
            integrity_problem = bool(performance["unmatched_offs"] or performance["notes_still_sounding_at_eof"])
            channel_10 = bool(performance["events"].get("channel_10_note_ons"))
            performance["integrity_problem"] = integrity_problem
            integrity_files += integrity_problem
            channel_10_files += channel_10
            course = unit_by_midi.get(performance["file"], {}).get("course", {}).get("slug", performance["file"].split("/")[2])
            file_counts = Counter()
            for segment in performance["slices"]:
                result = detections[segment["notes"]]
                known = result["type"] is not None
                status = "recognized" if known else "unrecognized"
                counters = [totals, by_course[course], by_size[str(segment["pitch_classes"])], file_counts]
                if not integrity_problem and not channel_10 and segment["pitch_classes"] >= 3:
                    counters.append(clean_pitched)
                for counter in counters:
                    counter["segments"] += 1
                    counter[status] += 1
                    counter["duration_ms"] += segment["duration_ms"]
                    counter[status + "_ms"] += segment["duration_ms"]
                    if result["blocked_rootless_candidates"]:
                        counter["blocked_rootless_three_note_segments"] += 1
                    if known and not result.get("root_present", True):
                        counter["inferred_root_segments"] += 1
                    if result.get("alternatives"):
                        counter["segments_with_alternatives"] += 1
                if not known:
                    key = ",".join(PC_NAMES[p] for p in sorted({n % 12 for n in segment["notes"]})) + f" / {PC_NAMES[segment['notes'][0] % 12]}"
                    item = unknown[key]
                    item["count"] += 1
                    item["ms"] += segment["duration_ms"]
                    if len(item["examples"]) < 3:
                        item["examples"].append({"file": performance["file"], **segment})
                stream.write(json.dumps({"file": performance["file"], **segment, "detected": result}, ensure_ascii=False) + "\n")
            performance["counts"] = dict(file_counts)
            del performance["slices"]
    checked = []
    for fixture in fixtures:
        result = detections[tuple(sorted(set(fixture["notes"])))]
        assessment = assess_fixture(fixture, result, patterns)
        literal = fixture_xml_notes(fixture, root) if fixture.get("location") else None
        midi_path = fixture["musicxml"].replace("omr/musicxml/", "omr/midi/").replace(".musicxml", ".mid")
        pdf_path = fixture["musicxml"].replace("omr/musicxml/", "raw/worksheets/").replace(".musicxml", ".pdf")
        if fixture["evidence"] == "literal_xml" and literal != fixture["notes"]:
            raise ValueError(f"fixture {fixture['id']} no longer matches its literal XML location")
        checked.append({**fixture, "detected": result, **assessment,
                        "pdf": pdf_path, "pdf_sha256": digest(root / pdf_path), "midi": midi_path,
                        "musicxml_sha256": digest(root / fixture["musicxml"]),
                        "voicing_occurs_in_midi": tuple(fixture["notes"]) in midi_voicings.get(midi_path, set()),
                        "literal_xml_notes": literal,
                        "pdf_notes_missing_from_xml": sorted(set(fixture["notes"]) - set(literal or [])),
                        "literal_xml_matches": literal == fixture["notes"] if literal is not None else None})
    after = source_manifest(source)
    if after != before:
        raise RuntimeError("analyzer source changed during audit; rerun for a consistent snapshot")
    if digest(Path(__file__)) != script_hash or (digest(args.fixtures) if args.fixtures.exists() else None) != fixture_hash:
        raise RuntimeError("audit script or fixture evidence changed during audit; rerun")
    summary = {"archive": str(root), "source": before, "binary_sha256": digest(binary),
               "audit_script_sha256": script_hash,
               "fixture_sha256": fixture_hash,
               "minimum_segment_ms": args.minimum_ms, "inventory": inventory,
               "midi_files_scanned": len(performances), "distinct_voicings": len(ordered_notes),
               "totals": dict(totals), "by_pitch_class_count": dict(by_size),
               "clean_no_channel_10_three_or_more": dict(clean_pitched),
               "midi_integrity_problem_files": integrity_files, "midi_files_with_channel_10": channel_10_files,
               "by_course": dict(by_course), "patterns": patterns,
               "xml_harmony_annotations": sum(s["harmonies"] for s in xml_stats.values()),
               "xml_files_with_zero_divisions": sum(s["zero_divisions"] > 0 for s in xml_stats.values()),
               "fixtures": {"total": len(checked), "matches": sum(f["matches_workbook"] for f in checked),
                            "candidate_type_matches": sum(f["candidate_type_match"] for f in checked),
                            "candidate_evidence_matches": sum(f["candidate_evidence_match"] for f in checked),
                            "expectations_met": sum(f["expectation_met"] for f in checked),
                            "assessments": dict(Counter(f["assessment"] for f in checked))},
               "errors": errors}
    write_json(out / "summary.json", summary)
    write_json(out / "files.json", performances)
    write_json(out / "fixtures.json", checked)
    write_json(out / "unknown-sets.json", sorted(({"set": k, **v} for k, v in unknown.items()), key=lambda x: -x["count"]))
    lines = ["# Local lesson audit", "", f"Source fingerprint: `{before['sha256']}`.", "",
             "These figures measure recognition coverage, not chord-label accuracy. "
             "The MIDI is OMR-derived and the MusicXML contains no trusted chord-symbol ground truth.", "",
             f"Scanned {len(performances)} unique MIDI files; {len(ordered_notes)} distinct voicings.",
             f"Kept constant sounding sets lasting at least {args.minimum_ms:g} ms. "
             "All channels are combined, as with the firmware's default Omni setting. "
             "Sustain and repeated note-ons are retained. The device's settling timer and BLE arrival timing are not simulated.", "",
             "| Pitch classes | Segments | Recognized | Unrecognized |", "| --- | ---: | ---: | ---: |"]
    for size, counts in sorted(by_size.items(), key=lambda x: int(x[0])):
        lines.append(f"| {size} | {counts['segments']} | {counts['recognized']} | {counts['unrecognized']} |")
    lines += ["", f"Files with unmatched releases or notes sounding at EOF: {integrity_files}. "
              f"Files with channel-10 notes: {channel_10_files}. These remain in the overall counts; "
              "summary.json also reports the clean, no-channel-10 subset with at least three pitch classes.",
              "", "## Workbook probes", "", "Printed harmony can have a different literal voicing. "
              "Primary matches, evidence-supported alternative roots, and intentional rejection of unproved qualities "
              "are assessed separately. None is a corpus accuracy score. See fixtures.json for provenance and alternatives.", "",
              "| Case | Assessment | Workbook | Primary | Supported reading | Expectation met |",
              "| --- | --- | --- | --- | --- | --- |"]
    for f in checked:
        lines.append(f"| {f['id']} | {f['assessment']} | {f['expected_symbol']} | {f['detected']['flat_name']} | {f['candidate_evidence_match']} | {f['expectation_met']} |")
    lines += ["", f"Parse failures: {len(errors)}. Full details are in the adjacent JSON files.", ""]
    (out / "summary.md").write_text("\n".join(lines))
    print(json.dumps({"inventory": inventory, "totals": dict(totals), "fixtures": summary["fixtures"], "errors": len(errors), "output": str(out)}, indent=2))


if __name__ == "__main__":
    main()
