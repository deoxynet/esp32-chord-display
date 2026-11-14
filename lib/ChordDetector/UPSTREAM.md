# Upstream chord detector

Source: <https://github.com/LongKelvin/midi-chord-detector-plugin>

Pinned commit: `07bdccc4903968e7b2a13845f6a5cfa3458a768c`

Imported directory: `Source/chord_detection/detector/`

Author: Long Kelvin. The original [MIT license](LICENSE) is included here and applies to the imported code and its adaptations. JUCE, plugin/standalone wrappers, upstream fonts and desktop UI code are not part of this import.

## Embedded adaptations

- The upstream chord families remain, with corrections and additions for jazz voicings: 9sus4, 13sus4, 7sus4b9, half-diminished ninth, maj13#11 and minor add11. Perfect fifths and lower tensions can be omitted where declared optional. The invalid #5/#9/b13 entry, which named one pitch class twice, was removed.
- `ChordScoring.cpp` uses twelve-bit pitch-class masks. Ninth/eleventh/thirteenth intervals project to their corresponding pitch class, rather than adding synthetic octave copies of every played interval. This preserves exact matching and identity under octave doubling and eliminates per-pattern tree allocations.
- A match must explain every sounding class and contain its mandatory tones. `ChordPattern::requiredAny` requires actual altered-tone evidence for 7alt. Required/important evidence is normalized by pattern size so long suffixes do not win merely by naming more required tones. Full voicings and voicings omitting optional tones have separate completeness bonuses; allowed additional tensions preserve completeness.
- `ChordDetector.cpp` keeps one compact candidate per root and constructs at most three full result objects in the primary group. Compatible readings with a sounding root take precedence over missing-root hypotheses. The result also retains at most eleven compact alternatives, without copying their actual MIDI/name vectors. All twelve roots are considered; rootless candidates require at least three distinct played pitch classes.
- The full-scan mask scorer does not build the unused desktop interval index at startup.
- For rootless hypotheses, the required root can be absent. Actual third/seventh evidence is required, or third/sixth/ninth for 6/9. Missing optional fifths do not favor an altered enharmonic spelling. Rootless confidence is capped at 0.65 as a heuristic; the UI qualifies inferred names/Romans, shows relevant alternatives and keeps inferred cadence resolutions tentative.
- Bass-guided ambiguity handling also covers minor-sixth/half-diminished extensions and minor-eleventh/add9/suspended equivalents. The quartal-four-note pattern is a fallback behind familiar minor-eleventh readings; the app independently recognizes literal fourth/fifth spacing. Confidence is computed after ambiguity resolution and clamped to 0–1.
- Input MIDI notes are validated before pitch-class conversion. Bass reporting uses the lowest MIDI note rather than the numerically smallest pitch class.
- Drop-2/drop-3 classification requires reconstructing a close, four-note chord with distinct pitch classes. The UI marks these structural interpretations as inferred.
- The unused regex/exception-based note-string parser (`NoteUtils.cpp`) and detailed text formatter (`ChordFormatter.cpp`) are excluded from embedded and native app builds. Those optional desktop helper methods are not part of this firmware's linked API.

The upstream header/API shape is largely retained for provenance and ease of comparison. Historical comments describing the desktop implementation are not embedded performance guarantees. This detector uses small dynamic containers in the main task; it is not allocation-free or intended to run in the BLE callback.
