<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Peaks v6: verification report (2026-10-19)

Branch `peaks-v6` on master 929ffc19 (= peak-diskio 76733210 plus the
recording play head fix, which is not part of v6). Verified before the
blind review, as the verification work order asks.

| Commit | What |
|---|---|
| f885079c | log value codes, compiler-built tables, format 6 |
| dd2936ac | normalise on the pyramid's bound |
| e282a435 | RMS plane, band in the painter |
| 0c544109 | `Unsupported` waveform state |
| a2cd15fc | stress test, `TRAVERSO_TSAN` |
| 934e290b | verification tests; fix of a wrong stress check |
| 8bc953c6 | faster encoding and lookups (found by the measurements) |
| 884f047d | measurement program `tests/peaks/measure_peaks.cpp` |
| 10d984dc, 098f3180, d7a5a13b, c8a17cd6 | fixes from the blind review (section 8) |

Line numbers in sections 1 and 2 refer to 884f047d (`git show
884f047d:<file>`), the state that was verified; section 8 lists what the
review changed after it.

## 1. Agreed points

### Log coding (min/max and RMS)

| Point | | Where |
|---|---|---|
| 1024 steps per octave, about −168 to +24 dB, step 0.0059 dB | ✓ | `TPeakPyramid.h:117` (constants and comment), test `codes` |
| Decoding: mantissa × power of two, code 0 silence; convention at the table | ✓ | `TPeakPyramid.h:57` (comment), `:190` `to_value()`. Convention: code c > 0 is `Octaves[c >> 10] * Mantissas[c & 1023]`, octave c/1024 and step c%1024; −c is the value negated. Equivalent to the `(k−1)` form of the order; no code is wasted on an offset. |
| Both tables `constexpr`, own constexpr 2^(i/1024), no `std::exp2`; GCC and Clang without raised limits | ✓ | `TPeakPyramid.h:26` `TPeakCodeMath` (Newton square roots, `octave_fraction()` at `:42`), tables at `:63` and `:72`; built with GCC 15 and Clang with default constexpr limits |
| `static_assert`s: mantissa[0] == 1, mantissa[1023] < 2, strictly increasing | ✓ | `TPeakPyramid.cpp:29-33`, also 1024 steps make an octave and Octaves[28] == 1, Octaves[31] == 8 |
| Encoding: `frexp` plus binary search in the same table; octave overflow handled | ✓ | `TPeakPyramid.cpp:54` `split()` (frexp), `:40` `mantissa_index()` (binary search, branchless loop), overflow `:72` |
| Codes symmetric −32767 … 32767, −32768 never written | ✓ | encoders clamp to ±MaxCode (`:62`, `:77`, `:96`); `to_value()` clamps −32768 when reading (`TPeakPyramid.h:190`); test `codes` |
| Only an exact 0.0 gives code 0 | ✓, read as below | A max > 0 rounds up to at least +1, a min < 0 down to at most −1, also below the range (test `codes`: 1e-30, −1e-30, the smallest float). A tiny positive *min* or negative *max* rounds outward toward zero, and 0 is then the right outward code; anything else would put the stored span outside the real one. |
| Min/max outward (max up, min down), RMS nearest | ✓ | `TPeakPyramid.cpp:472` (min/max), `:446` (RMS `code_nearest`) |
| Saturation above +24 dB: ±32767 and the existing detection | ✓ | encoders; detection in `TWaveformColumns.cpp:76-91`; tests `codes`, `max amplitude` |
| `ValueScale` nowhere | ✓ | `grep -rn ValueScale src tests` finds nothing |

### Normalise on the bound

| Point | | Where |
|---|---|---|
| Partial end blocks from the samples, the inside from the pyramid's bound | ✓ | `TWaveformColumns.cpp:61-78` |
| The candidate scan is gone | ✓ | `TWaveformColumns.cpp` (dd2936ac) |
| Blocks are read only when saturated | ✓ | `TWaveformColumns.cpp:80-91` |
| The comment states the error: one step, ≈ 0.006 dB, never below | ✓ | `TWaveformColumns.h:74`; `TWaveform.h`, `TAudioClip.cpp` normalise comment |

### RMS plane

| Point | | Where |
|---|---|---|
| Log int16 of the RMS, mean squares combined; own array (SoA); `range()` for min/max does not read it | ✓ | `TPeakPyramid.h:122` (`Chunk`: blocks and RMS side by side, separate arrays), builder `:288`; `range()` `TPeakPyramid.cpp:193` reads blocks only |
| Builder: one pending value per level per channel; parents from unrounded children; one rounding per stored value; `finish()` uses them | ✓ | `TPeakPyramid.h:298` (pending mean square and frames, a double); `TPeakPyramid.cpp` `add_block()`; `finish()` `:376` |
| Partial last block weighted by its frames | ✓ | `TPeakPyramid.cpp:473`; test `rms` (also a mutation check: weighting the partial block as full makes 35 checks fail) |
| Column RMS = √(Σ ms × frames / Σ frames); from the samples directly | ✓ | `TPeakPyramid.cpp:200` `range(..., meanSquare)`, `TWaveformColumns.cpp:212`; samples `:303` |
| Live recording: RMS in the snapshot | ✓ | `TPeakRecording.cpp:178` (memory), `:194` (mapping) |
| Band in the column painter | ✓, not seen on screen | `TAudioClipView.cpp:861` (darker shade), `:939` (band). Only in fill mode, and since 098f3180 not for columns from the samples; checked by building and a headless run, not by eye |

The pending value is a double, not a float: the mean square of a level's
unpaired block, kept exactly until its pair arrives.

### Format and the rest

| Point | | Where |
|---|---|---|
| One bump to v6; older files Invalid, built again | ✓ | `TPeakFile.h:72`; test `file` (versions 2 to 5 refused) |
| The header names the planes (min/max, RMS); `end() == fileSize` and the header checksum fit the layout | ✓ (code), test ✗ until 10d984dc | `TPeakFile.cpp:36` (`planes`), `:289`, `:290`, `:308`. The test `file` did *not* cover the checksum at 884f047d: its frame-count cases patched offset 40, which in the 80-byte header is `planes`, so they were refused by the planes check. Found by the review, fixed in 10d984dc (offset 48, padding and codes per octave added). |
| Recording and build write both planes per period; streaming unchanged | ✓ | `TPeakFile.cpp:454` (records), `:504` (tail); per level min/max of every channel, then the RMS codes, so a period is still one record |
| More than two channels: `Unsupported` with a message, not `Failed` | ✓ | `TWaveform.h:43`, `TWaveform.cpp:139`, view `TAudioClipView.cpp:801` |
| BlockFrames is 64 | ✓ | `TPeakPyramid.h:108` |

## 2. Tests

All in ctest (`peaks`, `peak_build`), green with GCC and Clang, clean under
ASan/UBSan and TSan.

| Asked for | Test |
|---|---|
| Log sweep −180 … +30 dB, both signs, outward | `codes` (sweeps −200 … +30 dB in 0.001 dB steps) |
| Around powers of two: just below, at, just above 2^k, min and max | `codes` (k = −27 … 3) |
| `max_amplitude` of a 1e-9 signal > 0 | `max amplitude` |
| Decoding monotonic over all codes | `codes` (every code, and every code's value maps back to it) |
| Hard-limited: within one step, never below, ≤ 2 × BlockFrames read | `max amplitude` (random ranges, and a signal clamped at −1 dBFS) |
| Peak just outside the range, in a shared block | `max amplitude` |
| Source above +24 dB: exact via saturation | `max amplitude` |
| Sine A: A/√2 per full column within a step, every level | `rms` (32 frames per period, levels 0 … 12, blocks and columns) |
| Pyramid RMS = direct from the samples within a step, every level, tail block | `rms` |
| Silence: RMS code 0 | `rms` |
| Stress: queries at changing zoom during `append()`, RMS, `max_amplitude`, poison, counts never back, file equal after `finish()` | `peak_build` → `stress_recording()` |
| `TRAVERSO_TSAN`, suite TSan-clean | `cmake/CompilerFlags.cmake:21`; run below |
| Round trip builder → file → open, mono/stereo, with and without tail, both planes | `file` (1 to 3 channels, lengths around period edges, the period itself has no tail; `same_levels` compares the RMS too) |
| A v5 file is Invalid | `file` |
| 3+ channels: Unsupported | `peak_build` (task state, `supports()`); TWaveform itself cannot be linked into a test |

TSan (`-DTRAVERSO_TSAN=ON`, Clang, builds/peaks-v6-tsanopt):

```
traverso_peak_build_test: exit 0, 0 ThreadSanitizer warnings
    recording of 589145 frames: 186 snapshots
    recording of 504462 frames: 203 snapshots
    stress: 143 reads, 16162 columns compared
all peak build tests passed
traverso_peak_test: exit 0, 0 ThreadSanitizer warnings
all peak tests passed
```

## 3. Deviations

1. **The column query is slower than v5, not equal.** With RMS a 2000-column
   query takes 1.5–1.8 × as long (table in section 5). Without RMS, v6 was
   still 10–25 % slower (41/61/225 µs against 32/54/202 µs, measured as a
   diagnostic before the chunk table was made flat). That part comes from the
   agreed decoding through two tables instead of one multiplication. The rest
   is the RMS: a second value per visited block, its square and weight, and a
   root per column. In absolute terms a full repaint of 2000 columns costs
   56–305 µs instead of 32–202 µs. Choices, not made here: accept it; have
   the query compute RMS only when the view draws the band; or decode with
   one 64K table (a design change).
2. **Decoding convention** `Octaves[c >> 10] * Mantissas[c & 1023]` instead of
   the order's `(k − 1)` form: the same tables, no offset.
3. **"Only 0.0 gives code 0"** holds outward from zero, see section 1.
4. **The header names the planes with a field, not a table:** this version
   writes and reads exactly min/max + RMS, and anything else is Invalid, like
   every other format change (built again). A plane *after* another one in
   the file would also break streaming: its offset depends on the length of
   the first, which a recording does not know. The planes are therefore per
   level inside every record and the tail.
5. **Building is slower:** 1.65 × for WAV, 1.15 × for FLAC, 1.09 × for MP3
   (the decoder dominates). Not a requirement; measured and reduced from
   2.65 × (8bc953c6).

## 4. Bugs found and fixed

| Commit | Bug |
|---|---|
| 934e290b | The stress test required a snapshot's frames to reach the end of its last block; after `finish()` that block can be partial, so a read right after finish failed now and then. A test bug. |
| 8bc953c6 | Not a bug but found here: encoding did two searches for a nearest code and the query two separate chunk lookups per block; v6 built 2.65 × slower than v5. |
| e282a435 (during the work) | So RMS weights hold while a recording grows, a snapshot's frame count must cover every block it names. Before, a snapshot read the frames before the counts; now the builder publishes the frames before a block and a snapshot reads the counts first (`TPeakPyramid.cpp:349`, `:414`). |

## 5. Measurements

Measured at 884f047d. After the review fixes (c8a17cd6): the WAV build 0.95
s, the column queries 57 / 79 / 301 µs, the same as below.

One hour of stereo pink noise at 48 kHz, hard-limited at −1 dBFS (sox),
as WAV, FLAC (−5) and MP3 (320 kbit/s). Release builds (GCC 15, -O2),
`tests/peaks/measure_peaks.cpp` compiled against v5 (master 929ffc19) and
v6. Page cache warm.

**Peak file size**, per hour of stereo audio at 48 kHz:

| v5 | v6 | ratio |
|---|---|---|
| 43,200,136 bytes | 64,800,176 bytes | 1.50 |

**Build** (decode, pyramid, write, sync and rename):

| | v5 | v6 |
|---|---|---|
| WAV | 582 ms | 962 ms |
| FLAC | 2216 ms | 2551 ms |
| MP3 | 4155 ms | 4525 ms |

**`max_amplitude` over the whole hour:**

| | v5 | v6 |
|---|---|---|
| WAV | 1123 ms, 172.8 M frames read (the whole file, 2.7 M reads) | 0 ms, 0 frames |
| FLAC | 2213 ms, the whole file decoded | 0 ms, 0 frames |
| MP3 | 37 ms, 64 frames | 0 ms, 0 frames |

The range is the whole source, block-aligned, so v6 reads no partial end
blocks. The MP3 decoder overshoots the limit (peak 1.403), so there one block
holds the maximum and v5 is fast. Results: WAV/FLAC v5 0.891296387, v6
0.891300857 (+0.00004 dB); MP3 v5 1.40342104, v6 1.40372252 (+0.0019 dB).
Both within one step above, never below.

**Column query**, 2000 columns, best of 10 × 20 places after a warm-up,
median of three runs:

| frames per column | v5 | v6 | ratio |
|---|---|---|---|
| 256 | 31.6 µs | 56.1 µs | 1.78 |
| 8192 | 53.6 µs | 79.4 µs | 1.48 |
| 86400 (the hour in 2000 columns) | 202 µs | 305 µs | 1.51 |

**Recording memory**, 8 mono tracks of 10 minutes at 48 kHz, peaks in memory
at most:

| v5 | v6 | ratio |
|---|---|---|
| 1440 KB | 2160 KB | 1.50 |

Bounded by about one period (22 s) per track plus one chunk per level, not by
the length of the recording. CPU: 284 against 564 ms for 80 track-minutes.

## 6. Code count

Code lines only (no comments, no blank lines), against 929ffc19, at
884f047d (after the review, at c8a17cd6: src +483 −82 net +401, tests
+624 −25 net +599; the review fixes added the per-block NaN rule, the
recorded-blocks query, the chunk run helper and tests):

| File | added | removed | net |
|---|---|---|---|
| src/core/TPeakPyramid.cpp | +171 | −29 | +142 |
| src/core/TPeakPyramid.h | +107 | −9 | +98 |
| src/core/TPeakFile.cpp | +71 | −14 | +57 |
| src/sheetcanvas/TAudioClipView.cpp | +24 | −3 | +21 |
| src/core/TWaveform.cpp | +13 | 0 | +13 |
| src/core/TPeakRecording.cpp | +14 | −2 | +12 |
| src/core/TWaveformColumns.cpp | +21 | −12 | +9 |
| src/core/TPeakBuildTask.cpp | +4 | −2 | +2 |
| src/core/TPeakBuildTask.h | +2 | 0 | +2 |
| src/core/TPeakFile.h | +3 | −1 | +2 |
| src/core/TPeakRecording.h | +2 | 0 | +2 |
| src/core/TWaveform.h | +3 | −1 | +2 |
| src/core/TWaveformColumns.h | +1 | 0 | +1 |
| src/core/TAudioClip.cpp | 0 | 0 | 0 (comment) |
| **src** | **+436** | **−73** | **+363** |
| tests/peaks/test_peaks.cpp | +245 | −18 | +227 |
| tests/peaks/test_peak_build.cpp | +144 | −3 | +141 |
| tests/peaks/measure_peaks.cpp | +178 | 0 | +178 |
| **tests** | **+567** | **−21** | **+546** |

## 7. Changed files

```
cmake/CompilerFlags.cmake
doc/design/peak-redesign.md
doc/design/peaks-v6-report.md
src/core/TAudioClip.cpp
src/core/TPeakBuildTask.cpp
src/core/TPeakBuildTask.h
src/core/TPeakFile.cpp
src/core/TPeakFile.h
src/core/TPeakPyramid.cpp
src/core/TPeakPyramid.h
src/core/TPeakRecording.cpp
src/core/TPeakRecording.h
src/core/TWaveform.cpp
src/core/TWaveform.h
src/core/TWaveformColumns.cpp
src/core/TWaveformColumns.h
src/sheetcanvas/TAudioClipView.cpp
tests/peaks/CMakeLists.txt
tests/peaks/measure_peaks.cpp
tests/peaks/test_peak_build.cpp
tests/peaks/test_peaks.cpp
```

`git diff 929ffc19..peaks-v6 -- <files>` gives them in one go.

## 8. Blind review (fable-xhigh-reviewer, 2026-10-19)

No critical or high findings. The reviewer built both suites (GCC, and
Clang with `TRAVERSO_TSAN`): green, 0 TSan warnings. It checked the encoder
exhaustively over all 276,824,064 floats in [2^-29, 16) (outward rounding,
smallest/largest code, symmetry, saturation, NaN, inf, denormals, −32768),
the memory ordering of frames before counts, the RMS weighting in growing,
finished and capped snapshots, the file offsets against the writer, and
max_amplitude; all correct.

| # | Severity | Finding | Done |
|---|---|---|---|
| 1 | medium | The header tests for the frame count patched offset 40 (`planes` in the v6 header), so the header checksum was not tested, and the report claimed it was | 10d984dc: offset 48; padding and codes per octave tested too; this report corrected |
| 2 | medium | One NaN sample made a block's mean square NaN and, through the exact parents, the RMS of every block above it (an inf the same with the top code): the band vanished at every zoom | 10d984dc, then c8a17cd6: a level-0 block whose sum of squares is not finite gets its peak, clamped to the code range; per block, not per sample (a guard per sample cost a third of the build time); tests with NaN and ±inf, mutation-checked |
| 3 | low | `code_nearest()` of a value at 2^-28 gave 0 (silence) instead of code 1 | 10d984dc; the nearest test now sweeps −200 … +30 dB and checks it is the nearer neighbour |
| 4 | low | With the frames published first, a recording's length could be a block ahead of what a read has: the last column could paint empty for a frame | 098f3180: the length comes from the published blocks |
| 5 | low | The RMS band was also drawn for columns from the samples, darkening part of the line | 098f3180: no band for sample columns |
| 6 | low (design) | The query always computes the RMS, also when the band is not drawn (outline mode) | open, for the user: the same choice as deviation 1 |
| 7 | low | `State::Unsupported` of the build task went through `fail()`, which logged "building failed" as a warning | 098f3180: logged as not built |
| 8 | low (tests) | The growing-pyramid test did not check the RMS nor the frames covering the blocks; the RMS tests allowed a whole step for a nearest code | 10d984dc |
| 9 | nits | Duplicated decoding and chunk arithmetic; vectors without reserve; the ten roots recomputed per table entry (+0.18 s per translation unit with Clang); the measurement program in `all`; a negative read in it | d7a5a13b (0.12 s less per translation unit measured) |

Still open after the review: the RMS opt-in (6 and deviation 1), and
looking at the band on screen (5).

