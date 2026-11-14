# Chord analyzer: lesson archive audit

Audited 2026-11-13 against the local Open Studio Jazz archive in
`~/git/midi-tutor-mcp/data/openstudiojazz`. The initial findings below identified
unreachable three-note rootless hypotheses and discarded alternative readings.
Both are now addressed by general detector rules. Worksheet identities and
course names are evidence metadata only; none enters firmware behavior.

The initial audit added a native batch interface, a repeatable corpus runner,
and 23 annotated probes. Its baseline and evidence are retained below; the
follow-up implements and verifies the fixes.

## Follow-up fixes and verification

The detector now visits absent roots with at least three distinct pitch
classes. It requires sounding third/seventh guide tones, or third/sixth/ninth
for 6/9, plus every other mandatory tone. Missing thirds, sevenths and altered
fifths are not invented. Ordinary triads, chromatic clusters and complete
voicings remain covered by negative and positive regressions.

One best spelling is retained for each eligible root. A compatible reading
with a sounding root remains the literal default; up to eleven compact other
roots are returned as alternatives. Full MIDI/name vectors are materialized
for at most three candidates. Rootless scoring treats optional omissions
equally, avoiding an accidental preference for complicated altered spellings.
Equal-scoring rootless readings prefer fewer alterations before a
transposition-invariant tie-break.

Chord/Harmony screens show useful alternatives on an `ALSO` line. The selected
tonic/fifth order these suggestions without changing the key or rerooting a
complete literal chord. USB `?` and the native batch API expose every retained
root. Inferred names and Romans carry `?`; confidence is capped at 0.65, and
cadences involving inferred roots remain `possible`. The missing root is never
highlighted as sounding.

The corpus was rerun from a frozen source snapshot in
`build/lesson-audit-verified/`. Source fingerprint:
`4bf58e81735be9e6bc3313c0eb5e5cccc9761c5203920ebabd048addc3414e56`.
The saved comparison baseline in `build/lesson-audit/` has fingerprint
`3d947d2707d664ceac8440e968e1ceedc8c55d5188a5d24fee34986e01dc0b2a`.

| Coverage measure | Baseline | After fixes |
| --- | ---: | ---: |
| Stable segments | 127,241 | 127,241 |
| Unlabeled three-class segments | 7,364 | 844 |
| All unlabeled segments | 43,118 | 36,598 |
| Clean, no-channel-10, three-or-more classes: unlabeled | 11,330 / 80,542 | 5,286 / 80,542 |

All 6,520 segments blocked by the four-note guard now have qualified inferred
readings. Two-note and dense unsupported-set coverage is unchanged. There are
2,518 newly labeled distinct voicings and no previously recognized voicing
became unlabeled. Primary root/type changes for 513 previously labeled distinct
voicings; these readings still undergo the same complete pitch-accounting
checks, with other roots retained. This is improved **coverage, not measured
accuracy**. The same notes can still imply several legitimate harmonies.

All 23 annotated probes meet their evidence-based expectations: five unchanged
literal controls, thirteen additional supported readings, four deliberately
underdefined qualities, and one literal harmonic substitution. Eighteen probes
have the intended root/type among the returned readings. The other five are
not forced into their printed full symbols. In particular, C–A–Eb does not
prove the missing altered fifth of Cdim7; Cm6 is a supported alternative.
Printed harmony, actual notes and OMR damage remain separate measurements.

Validation includes all 4,096 pitch-class subsets: every eligible root is
reachable, every returned reading accounts for every sounding class, and all
mandatory evidence is present. Sparse/rootless shapes run in all twelve keys
with octave changes. A separate corpus integrity pass checked all 42,629
distinct voicings and 100,276 returned primary/alternative readings. All six
native suites pass with AddressSanitizer/UndefinedBehaviorSanitizer; all seven
audit-support tests pass. Both Core2 firmware variants compile, and native
previews cover sparse and dense alternative rows. Live-device validation is
still separate.

Reproduce the follow-up without overwriting the saved baseline:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCHORD_SANITIZE=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
AUDIT_BINARY="$PWD/build/chord_audit" uv run --python 3.12 --with mido python -m unittest discover -s tests -p test_lesson_audit.py -v
uv run --python 3.12 tools/audit_lessons.py --out build/lesson-audit-verified
pio run -e core2 -e core2-demo
```

The historical findings below describe the initial implementation. Their
candidate-generation and alternative-exposure priorities are now implemented;
the cautions about incomplete evidence and OMR remain applicable.

## Scope and reproducibility

| Artifact | Files | Distinct SHA-256 contents |
| --- | ---: | ---: |
| PDF worksheets/workbooks | 1,645 | 1,423 |
| OMR MusicXML | 1,413 | 1,338 |
| OMR-derived MIDI | 1,372 | 1,298 |

All 1,298 distinct MIDI files were parsed successfully. Each retained segment is
one unchanged sounding note set between MIDI event groups, lasting at least
85 ms and containing at least two pitch classes. Repeated note-ons, channels,
note-offs, tempo changes, and sustain/controller semantics are preserved.
Octave doubling remains in the notes passed to the production analyzer.

The native runner calls `chord::analyze` and the production symbol formatter.
The Python MIDI-state reader was checked against the actual C++ `MidiState`
after 5,521 deterministic events, including overlapping voices, sustain,
controller resets, and the voice-count limit. Separate tests check score voice
timing, divisions repair, transposition, tempo changes, and malformed event order.

Run from this repository:

```sh
uv run --python 3.12 tools/audit_lessons.py
uv run --python 3.12 --with mido python -m unittest discover -s tests -p test_lesson_audit.py -v
```

`--archive`, `--out`, and `--minimum-ms` override the defaults. Python 3.12 is
intentional: the installed Homebrew Python 3.14 cannot load its XML parser.
`uv` isolates the pinned `mido` dependency.

Detailed results remain under ignored `build/lesson-audit/`: `summary.md`,
`summary.json`, `slices.jsonl`, `detections.json`, `files.json`, `fixtures.json`,
`unknown-sets.json`, `xml-quality.json`, and `duplicates.json`. The archive is
read only; its PDFs, MusicXML and MIDI are not copied into this repository.
The runner saves the analyzer sources and their hashes beside the results so
concurrent firmware edits cannot change an audit's baseline.
The summary also records the audit script and fixture hashes.

The recorded run used source fingerprint
`5e355099db79c3fd5a7909fb8545e891d133388454c6502288da15a61ee2a669`.
The source manifest includes the native runner and CMake configuration as well
as the library sources. Subsequent runs can legitimately produce other results.

## Recognition coverage

| Sounding pitch classes | Stable segments | No chord label | Unlabeled |
| --- | ---: | ---: | ---: |
| 2 | 38,361 | 29,338 | 76.5% |
| 3 | 33,498 | 7,364 | 22.0% |
| 4 | 35,183 | 1,827 | 5.2% |
| 5 | 14,776 | 1,994 | 13.5% |
| 6 | 4,127 | 1,509 | 36.6% |
| 7 | 1,021 | 811 | 79.4% |
| 8–11 | 275 | 275 | 100% |
| **All** | **127,241** | **43,118** | **33.9%** |

These are recognition-coverage figures, **not accuracy or error rates**. A blank
can be appropriate for a fragment, passing-note collection, or dense cluster;
a returned chord can still disagree with the intended harmony. Segments are
not independent exercises, and longer polyphonic arrangements contribute more
segments than short worksheets.

Two-note fragments account for 68% of unlabeled segments. Restricting to three
or more classes leaves 88,880 segments, of which 15.5% are unlabeled. Excluding
the 18 files with note-state integrity problems and the 37 files containing
channel-10 notes leaves 80,542 such segments, with 14.1% unlabeled. Channel 10
is flagged because percussion note numbers are unsuitable as harmonic evidence.

The scan combines channels like the firmware's default Omni setting. It does
not simulate BLE transport timing, the `Settler` scheduling loop, display
refreshes, or the harmony trail. This is an audit of stable chord recognition.

## 1. Three-note rootless hypotheses are unreachable

In [ChordDetector.cpp](../lib/ChordDetector/src/ChordDetector.cpp), this guard
skips every absent root when fewer than four pitch classes sound:

```cpp
if (!present && pitchClasses.size() < 4) continue;
```

The *Shell + Pretty* lesson explicitly teaches three-note rootless voicings.
The following inputs all return `--`:

| Printed harmony | Played notes | MIDI pitches | Evidence |
| --- | --- | --- | --- |
| Cmaj9 | E3–B3–D4 | 52, 59, 62 | PDF, MusicXML and MIDI agree |
| C9 | E3–Bb3–D4 | 52, 58, 62 | Manually read from PDF; OMR is damaged |
| C7#9 | E3–Bb3–D#4 | 52, 58, 63 | Manually read from PDF; OMR drops the chord |
| C7b13 | Bb3–E4–Ab4 | 58, 64, 68 | Manually read from PDF; OMR retains only Ab4 |

The audit also calls the existing scorer directly for diagnostic purposes.
Among the 7,364 unlabeled three-class segments, **6,520** have at least one
missing-root candidate above the production score threshold. Those candidates
are never visited by `detectChord`. In the clean, no-channel-10 subset this
occurs in 6,044 segments.

This demonstrates a candidate-generation gap; it does not establish that all
6,520 intended harmonies are known, nor that accepting the top hypothetical
candidate would label them correctly. Rootless voicings can imply several roots.

**Priority:** evaluate three-note rootless candidates with actual guide-tone
evidence, retaining ambiguity information and testing against ordinary triads
and clusters before changing the default label.

## 2. One chosen label loses useful harmonic alternatives

Several worksheet interpretations share exactly the same pitches with the
analyzer's result:

| Worksheet interpretation | Input | Analyzer result |
| --- | --- | --- |
| C6, fifth omitted | C3–E3–A3 | Am/C |
| Cm6, fifth omitted | C3–Eb3–A3 | Adim/C |
| Cdim7, fifth omitted | C2–A2–Eb3 | Adim/C |
| Rootless C7b9 | E3–Bb3–Db4 | Bbdim/E |
| Rootless C7#11 | Bb3–E4–F#4 | Gb7/Bb |
| Rootless C7#9b13 | E3–Bb3–D#4–Ab4 | Emaj7#11 |
| Cm7 context, voiced Cm11/F | F3–Bb3–Eb4–G4–C5 | F9sus4 |

These results account for the sounding pitches. Calling them incorrect would
overstate the evidence. The limitation is that the device exposes one
interpretation while a jazz exercise depends on another. The selected key is
used by the Harmony layer after detection; it does not guide root selection.
The detector's optional context flag records history but does not use that
history to score candidates.

**Priority:** expose credible alternatives and, if desired, add a separate
contextual choice using the selected key and preceding harmony. Preserve the
literal reading and qualify inferred roots. More chord templates alone will
not resolve identical pitch sets.

## 3. OMR corruption would create false detector failures

The *Shell + Pretty* PDF has particularly clear examples:

| Printed voicing | MusicXML/MIDI result |
| --- | --- |
| B3–E4–A4, Cmaj13 | B3–A4; the third disappears |
| E3–A3–B3–D4, Cmaj13 | E3–B3–D4; the thirteenth disappears |
| E3–A3–D4, C6/9 | D4 alone |
| E3–Bb3–D#4, C7#9 | No notes at that score location |

Across the distinct XML files, 89 have zero-valued divisions that the archive's
MIDI conversion script repairs. None of the 1,413 XML files contains a
`<harmony>` element, so their chord symbols cannot be used as an automatic
ground-truth track.

Eighteen distinct MIDI files have unmatched releases and notes still sounding
at EOF under firmware semantics, despite balanced total note-on/off counts.
They are identified individually in `files.json`. Their ordering can leave a
phantom note in subsequent chord inputs. They remain in the all-files figures
and are excluded from the clean subset.

**Priority:** keep manually verified PDF probes separate from literal OMR
inputs, and quarantine malformed streams in any accuracy benchmark. Repairing
OMR should be a separately measured preprocessing step.

## 4. Printed symbols are not always literal voicing labels

*The Mu-2-5-1* starts with the printed harmony Cmaj7, but the PDF itself voices
C3–A3–C4–E4. MusicXML and MIDI preserve those notes. This is a C6 shell / A minor
triad over C; there is no B. An exact Cmaj7 expectation would falsely penalize
the detector or the transcription.

Likewise, the *Root + Shell* worksheet uses C–Eb–Bb under both Cm7 and Cm7b5;
the omitted fifth removes the only evidence distinguishing them. The
*Root + Half-shell* example C–B does not determine the third at all. These are
useful pedagogical voicings, but their intended full symbols need context.

**Priority:** benchmark literal pitch accounting, plausible chord alternatives,
and contextual harmonic role separately. Do not force full chord qualities
from notes that do not establish them.

## Retained probes and validation

[lesson_chords.json](../tests/fixtures/lesson_chords.json) records source paths,
score locations, PDF page/position, pitches, intended symbols, and the reason
for each assessment. The generated `fixtures.json` adds PDF/XML hashes, actual
labels, literal XML pitches, and whether each test voicing occurs in its MIDI.
Corrected PDF inputs are explicitly marked; they are not presented as literal
MIDI failures.

The 23 probes comprise five passing controls, four capability gaps, three
input-corruption examples, seven ambiguous readings, three insufficient-note
examples, and one harmonic substitution. Their raw match count is deliberately
not an accuracy score.

The five existing CTest suites and the five new audit-support tests pass.
