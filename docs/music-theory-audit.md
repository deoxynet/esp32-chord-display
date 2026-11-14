# Music-theory audit — 2026-11-13

The serious reproducible issues were fixed in chord matching, functional harmony, interval spelling and cadence feedback. The target is jazz lead-sheet practice with Berklee-style functional families, composite minor and explicit modal reference choices. This is a rule-based practice display; a single voicing cannot establish a composer's intent.

## Findings and fixes

| Severity | Finding before the fix | Result |
| --- | --- | --- |
| High | Unexplained notes cost only four score points. A twelve-note chromatic cluster was called Cm11b5. | Every sounding class must belong to the selected pattern or its declared optional tones. Unmatched sets return `--`. |
| High | Required/important tones earned points by raw count, favoring complex suffixes over equally supported simpler readings. Omitted optional tones also lost the full-match bonus entirely. | Normalize evidence by pattern size, distinguish full/optional-omission completeness and preserve the bass clue. Complete triad inversions retain precedence over incomplete sixth-chord alternatives. |
| High | Functional analysis was gated by whether every played note belonged to the selected scale. Cmaj7#11 became “borrowed Lydian”; G7b9 became a tentative dominant in C major. | Derive function from structural chord quality. Tonic and primary dominant retain their roles with tensions, while actual outside-note highlights stay literal. |
| High | Minor/Aeolian had generic modal labels. Cm6 and Cm(maj7) were called borrowed; Dm7b5 lacked predominant function. | Analyze Minor as composite natural/harmonic/melodic minor. Recognize minor-tonic forms, ii/iv/IV/vi-half-diminished subdominant families, bIII tonic-family and the major-quality V/leading-tone diminished dominant. Explicit Dorian and other modal selections keep modal readings. |
| High | Cadence matching used root motion plus a minor/not-minor flag. Dm6–G7–C7 was reported as a resolved ii–V–I. Major-mode tonal cadences could also end in a lowercase `i` label. | Require a minor-seventh/half-diminished ii, a dominant-quality V and a compatible major/minor tonic. Identify half-diminished ii explicitly and retain the correct tonic case. |
| Medium | Common G9sus/G13sus voicings appeared as F6/9/G or Dm11/G, producing misleading function. Elevenths required the ninth, and add9 required a perfect fifth. | Add suspended extensions and sus(b9), permit appropriate omissions, and use bass clues for equivalent suspended/minor-eleventh/add9 readings. Add half-diminished ninth and Lydian maj13#11. |
| Medium | `7alt` required no actual alteration. Its pattern also failed to describe the complete altered collection, while one entry simultaneously named #5 and b13. | Require at least one altered tone, represent all altered choices, remove the duplicate-pitch suffix and avoid treating every alternative as an asserted augmented triad in Romans. |
| Medium | Secondary tritone substitutes and ascending diminished approaches lacked useful readings. Parallel-major borrowing was absent from the source search. | Add qualified readings such as Ab7 = subV7/V and F#dim7 approaching G. Spell leading diminished roots relative to the target, such as #idim7 approaching ii. Include parallel Ionian, preferring a source that also contains the voiced tensions. |
| Medium | Altered-chord #9 could be labeled/spelled as b3. Outside-mode text used a global sharp/flat preference, so G7b9's Ab could appear as G#. | Use the detected quality for interval spelling and outside-note names. Staff, chips and outside-note readouts agree on #9, b9 and #11. |
| Medium | Literal fifth stacks could receive speculative functional labels from an equivalent suspended or extended chord. | Show an uncertain `5ths / QUINTAL` interpretation and exclude it from the functional trail, mirroring literal fourth stacks. |

Rootless Romans are qualified with `?`. All sevenths, alterations and tensions shown in a detected symbol must have evidence under that pattern's omission rules. A permitted omission is not highlighted as if it were played.

## Requested chord families

| Concept | Support and display |
| --- | --- |
| Mu / moo major | `add9`: C–D–E–G → Cadd9. Close/spread voicings, octave doubling, the third or fifth in the bass and an omitted fifth are covered. The major third stays present. E–D–G–C → Cadd9/E. There is no dedicated μ glyph or automatic mu-spacing tag. |
| Mu with second in bass | Ambiguous: Cadd9/D and D9sus4(no5) contain the same notes. The detector uses D in the bass to favor D9sus4. A missing-root mu voicing can also have a simpler complete-chord reading. |
| Cush | A mode-mixing progression approach, not a new pitch-set quality. Individual chords, parallel-mode borrowing, backdoor possibilities and a stable selected tonic are supported. There is no dedicated Cush progression classifier. |
| Stacked thirds / tertian | The usual major, minor, augmented and diminished triads, sevenths and extensions. Repeated minor thirds give diminished seventh; alternating major/minor thirds can build extended tertian chords. |
| Stacked fourths / quartal | Literal consecutive perfect fourths receive `4ths / QUARTAL`, with an ambiguous root. |
| Stacked fifths / quintal | Literal consecutive perfect fifths receive `5ths / QUINTAL`, with an ambiguous root. Conventional names can coexist: C–G–D is Csus2; C–G–D–A–E is C6/9. |

The fourth/fifth labels describe actual MIDI spacing. They do not reconstruct arbitrary inversions or octave redistributions of stacks. The same pitch collection can support several legitimate chord names; choosing a bass-guided reading is a heuristic.

## Validation

`tests/test_jazz_theory.cpp` adds musical regressions alongside the existing recognition, symbol, harmony, MIDI, notation and protocol tests:

- Every full detector pattern is exercised in all twelve transpositions, allowing meaningful aliases such as aug7/7#5.
- All 4,096 pitch-class subsets are checked: every detected symbol accounts for all sounding tones and has its mandatory evidence, including actual alterations and rootless guide tones.
- Common jazz shells, optional fifths/ninths, sus extensions, altered collections, minor-key functions, borrowed colors and secondary functions are exercised in all twelve keys.
- Mu spacing/inversions, Cush-style modal interchange and tertian/quartal/quintal examples have dedicated cases.
- Negative cadence cases cover ii6, a dominant arrival on I7, suspended tonic and incompatible tonic quality. Positive cases cover major/minor resolutions and borrowed half-diminished ii resolving to major.

Native validation uses AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCHORD_SANITIZE=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
pio run -e core2 -e core2-demo
```

The initial fixes passed five native suites. The follow-up now passes six suites, adding exhaustive candidate reachability/evidence checks, plus seven audit-support tests. Both Core2 firmware environments compile successfully. See the [follow-up results](chord-analyzer-audit.md#follow-up-fixes-and-verification) for the corpus rerun.

Firmware compilation and native tests do not establish live-device latency, LCD readability or real MIDI performance after these changes; those require hardware checks.

## Remaining limits

- Rootless inference now accepts three distinct pitch classes with actual third/seventh evidence, or third/sixth/ninth for 6/9. Compatible complete chords with another sounding root remain literal defaults, with useful alternatives shown explicitly. Bass-guided choices do not establish harmonic intent or use progression context to rerank detection.
- Seventh/extension patterns deliberately allow selected omissions. They do not attempt to explain every imaginable omission, added tone, polychord or mixed structure. Strict matching can leave unsupported but musically valid sets unnamed.
- The scale cards and color prompt describe the selected tonic collection. They are not per-chord available-tension, avoid-note or chord-scale recommendations. Recognizing a chord does not endorse every voiced tension as a resting improvisation note.
- Major/Aeolian function rules and brief cadence recognition do not model blues form, deceptive cadences, substitutions across a whole phrase, automatic modulation, time signature or harmonic rhythm. For example, C7 in selected C major can suggest V7/IV even when a player intends blues I7; the applied reading remains qualified.
- The major/minor distinction in the picker is a product choice: Minor/Aeolian now supports tonal composite-minor function while retaining the natural-minor reference. There is no separate “strict Aeolian only” analysis setting. Dorian and the other explicit modes stay modal.
- The unused desktop-only note-string parser and text formatter remain excluded from the firmware/native core. Their parsing behavior was inspected but is outside the linked music-theory path changed here.

## References

- [Berklee Harmony 2](https://online.berklee.edu/courses/harmony-2): composite minor, harmonic functions, modal interchange and related jazz harmony topics.
- [Berklee piano voicing techniques, excerpted from Randy Felts](https://online.berklee.edu/takenote/basic-piano-voicing-techniques/): guide tones, suspensions, optional perfect fifth and mandatory altered fifths.
- [Berklee voice leading for guitar, excerpted from Rick Peckham](https://online.berklee.edu/takenote/voice-leading-for-guitar/): tonic/subdominant/dominant families, minor forms and secondary dominants.
- [Ted Pease on modal harmony](https://online.berklee.edu/takenote/harmonic-considerations-modal-harmony/): modal tonic support and ambiguous fourth voicings.
- [Walter Becker interview, archived in Metal Leg 14](https://steelydanreader.com/1990/10/15/metal-leg-14/): mu-major added second and third-in-bass variant.

The implementation uses these concepts as a local design interpretation; it is not a comprehensive or endorsed implementation of a Berklee curriculum.
