<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Peak data redesign (design, 2026-10-17)

Waveform data for audio clips: how it is built, stored, queried and painted.
Replaces `TPeak` (`src/core/TPeak.cpp`), the micro/macro branches in
`TAudioClipView::draw_tile()` and `ClipTileCache`. Proof of concept on branch
`peak-redesign` (from `rt-owners` b6c0b694).

## 1. What is wrong today

| Problem | Where |
|---|---|
| The macro/micro switch is counted in 44.1 kHz frames (`zoom < 64`, `framesPerPeak >= 64`), so a 96 kHz file switches at a different visual density than a 44.1 kHz file, and both paths convert with ad-hoc `44100` factors. | `TPeak::calculate_peaks()`, `TAudioClipView` |
| Zoom levels that are not a power of two snap to the *nearest* cached level (`nearest_power_of_two` + `cache_index_lut`) and the start offset is rounded (`qRound(startPos / pow2)`): pixels drawn from a level that is up to 1.5x too coarse or fine, and a start that is off by up to half a block. | `TPeak::calculate_peaks()` |
| Level 0 is "64 frames at 44.1 kHz" counted with a `TTimeRef` step size, so a level-0 block holds a non-integer number of real file frames for other rates. Blocks straddle samples. | `TPeak::prepare_processing()` / `process()` |
| Micro view reduces samples per pixel to *one* value (the larger of upper/lower), so the line loses the opposite extreme of a pixel. | `TPeak::calculate_peaks()` micro branch |
| Two drawing paths (polyline vs. filled polygon) with different scaling (`MAX_DB_VALUE` leaks into the view), and a visible jump when crossing zoom 64. | `TAudioClipView::draw_tile()` |
| Tile pixmap cache (`ClipTileCache`) keyed on zoom/height/selection/DPR: memory, invalidation, artifacts at tile edges (4 px padding). Introduced to hide slow painting; the Qt Quick plan does not need it (cursor in its own layer, clips repaint on model change). | `ClipTileCache`, `TAudioClipView` |
| One peak file per channel, a separate temp `.norm` file, header without sample rate/channel count/source identity, file written in place (a crash leaves a broken file that only the mtime check may catch). | `TPeak` |
| Build, file I/O, query, normalisation and threading in one 1000-line class with shared `QFile` handles across threads (`m_peakBuildRunning` guard). | `TPeak` |
| Zoom steps of the UI (`TPeak::zoomStep`, `ZOOM_LEVELS`, `max_zoom_value`) are defined by the peak class. | `TSheetWidget`, `TSession` |

## 2. Principles

1. **The model decides, the view draws.** The view asks: "columns for this
   source time range, this time per column, this many columns, for all
   channels". The model picks the data source (source samples or pyramid) and
   returns one uniform result. The view never sees frames, levels or scales.
2. **Exact columns, no interpolation between levels.** A column holds the
   lowest and highest value of the signal inside its time range. Interpolating
   between two pyramid levels (mipmap LERP) makes spikes grow and shrink while
   zooming ("breathing") and shows values that are not in the audio. Arbitrary
   zoom comes from combining pyramid blocks per column, not from blending.
3. **One rendering type for every zoom.** Each column is a vertical span
   `[min, max]` and the view draws exactly that, for every column. Far out
   this is the classic filled waveform; zoomed in (less than one sample per
   column) the spans are the pieces of the line between samples, so the spans
   *are* the line.
   No switch in the view, no jump: min and max converge by themselves because
   the data is exact. No FFT based threshold is needed: what looks like "line"
   or "fill" follows from the audio itself.
4. **Real file frames everywhere below the query.** Blocks are counted in
   frames of the source file at its own rate. Time conversions happen once,
   in `TTimeRef`.
5. **The peak file is a cache.** If it is missing, from another version, or
   does not match the source, it is rebuilt. No converter, no legacy loader.
6. **Pure parts, headless tests.** Pyramid, file format and column query are
   plain classes without GUI/audio dependencies, tested by brute force against
   the samples.

## 3. Parts

```
 source file ──► TPeakBuilder ──► TPeakPyramidBuilder ──► TPeakFile (v2, mapped)
 (reader)        (background,      (incremental: import                │
                  cancellable)      and recording)                     ▼
                                                               TPeakLevels (view)
 source samples ───────────────────────────────────────────►  TWaveformColumns
                                                                       │ columns[channel][i] = {min, max}
                                                                       ▼
                                                    TAudioClipView (one span per column) / QSG column quads later
```

### 3.1 `TPeakPyramidBuilder` (pure, incremental)

- Input: planar float buffers, any chunk size (`append(const float* const* channels, nframes)`).
- Level 0: one block per `BlockFrames = 64` real file frames. Level *L* block
  *k* covers frames `[k·64·2^L, (k+1)·64·2^L)`.
- A block is `{qint16 min, qint16 max}` with `min = floor(x·8192)`,
  `max = ceil(x·8192)` clamped to ±32767: conservative, the stored span always
  contains the true span; resolution 1/8192 (−78 dBFS), headroom ±4.0
  (+12 dBFS, matches the gain clamp). (Format 6 replaces this linear code
  by a log code and adds an RMS plane: section 6.) Not 8 bit: a linear 8-bit value cannot
  show anything below −42 dBFS and steps become visible when clip gain and
  automation scale the waveform up.
- Level *L+1* block *k* = combine(level *L* blocks *2k*, *2k+1*). A block is
  published to the next level as soon as both children are complete, so a
  recording grows all levels as it goes; `finish()` closes the last partial
  level-0 block and promotes trailing odd blocks.
- The last block of a level may cover fewer frames than its size; the frame
  count is stored, so no query ever reads beyond it.
- Same code for import and recording.

### 3.2 `TPeakLevels` (read-only view)

Pointers + counts per level and channel. Produced by the builder (in memory)
and by `TPeakFile` (mapped file). `TWaveformColumns` only knows this view.

### 3.3 `TPeakFile` (format v2)

(The current format is 6, streamed per period: doc/design/peak-diskio.md
for the layout, section 6 here for the values and sizes. Peak files are not
synced to the disk: they are a cache, doc/design/diskio-rounds.md §5.)

One file per source, `<source name>.peaks` in `peakfiles/` (since
2026-10-20 in the user's peak cache instead: doc/design/peak-diskio.md, "The
peak cache"). Written with
`QSaveFile` (never a half-written cache), read with `QFile::map()` (the OS
page cache does the caching, no reads per paint).

| Offset | Type | Field |
|---|---|---|
| 0 | char[8] | `"TRAVPEAK"` |
| 8 | u32 | format version = 2 |
| 12 | u32 | byte order mark `0x01020304` (native order; other order = rebuild) |
| 16 | u32 | header size (incl. level table) |
| 20 | u32 | sample rate of the source |
| 24 | u32 | channel count |
| 28 | u32 | block frames (64) |
| 32 | u32 | value scale (8192) |
| 36 | u32 | level count |
| 40 | u64 | source frame count |
| 48 | i64 | source file size in bytes |
| 56 | i64 | source modification time (ms since epoch, UTC) |
| 64 | {u64 blocks, u64 offset} × levels | level table |
| … | qint16 pairs | per level, per channel contiguous: `blocks` × `{min, max}`, 8-byte aligned |

Validation on open: magic, version, byte order, header size, file size covers
all levels, rate/channels/frames equal the reader's, source size and mtime
equal the source file. Any mismatch → rebuild. No separate normalisation
data: the pyramid answers the max amplitude (3.6).

Size: ≈ 2 × level 0 = 8 bytes per 64 frames per channel = 1/8 byte per frame
per channel (same as v1 without the `.norm` file). A one hour 48 kHz stereo
file: 43 MB.

### 3.4 `TWaveformColumns` (pure query; the "model decides" part)

```
struct TWaveformColumn { float min; float max; };   // empty column: min > max

TWaveformColumns::Source query(const TPeakLevels& levels,
                               TWaveformSampleSource* samples,
                               const TTimeRef& start,        // source time of column 0's left edge
                               const TTimeRef& columnWidth,  // time per column (> 0)
                               int count,
                               TWaveformColumn* out);        // out[channel * count + i]
```

Column *i* covers source time `[start + i·w, start + (i+1)·w)`. With file rate
*R*, frame *n* sits at time `n/R`; the frames inside the column are
`[ceil((start+i·w)·R), ceil((start+(i+1)·w)·R))` (computed by `TTimeRef`).
Adjacent columns share the boundary, so the columns partition the frames:
every frame is in exactly one column.

The model picks one source for the whole request (one rendering type per
zoom, the user's requirement; per clip the same for all channels):

- **Samples** when a column holds fewer than `SourceThreshold` file frames
  (64, i.e. one level-0 block; a parameter tuned with the benchmark). One
  read of the needed frames plus one neighbour on each side. The signal is
  the polyline through the samples: column span = min/max of the samples
  inside, the interpolated values at both column edges included, so the spans
  of neighbouring columns always touch (the drawn line is continuous) and a
  column without samples (zoomed in beyond one sample per column) holds the
  segment that crosses it.
- **Pyramid** otherwise. A level-0 block belongs to the column that holds its
  first frame (same partition rule as frames). The column's block range
  `[k0, k1)` is decomposed like a segment tree into at most ~2·log2 blocks of
  mixed levels, so the cost per column is O(log n) at any zoom and the result
  equals the brute-force min/max over its level-0 blocks. Error against the
  exact sample columns: a block's samples can be shown in the column of the
  block start, at most 63 frames away; since a column holds ≥ 64 frames this
  is less than one column, and only the values of that one block move.
  Values are conservative by at most 1/8192.

The result carries which source was used (`Samples` / `Pyramid`), for e.g.
sample dots at extreme zoom later; the drawing does not depend on it.

Frames outside the file (before 0, after the last frame) give empty columns.

### 3.5 View (`TAudioClipView`, PoC behind a setting)

- Paint only the exposed columns, directly, no tile cache.
- Per channel and column one vertical span `[min, max]`, at least one pixel
  high so silence and flat lines stay visible; optional outlines through the
  maxima and the minima with a cosmetic 1 px pen. Far out this is the filled
  waveform, zoomed in the spans are the line.
- Not one filled polygon: measured (section 4) a polygon envelope of a dense
  signal takes Qt's raster rasterizer seconds (2.7 s for a 4K row at 64
  frames per column), while the column spans cost the same at every zoom.
- Gain: clip gain, clip/track gain curves and fades multiply min and max per
  column as today (`curveMixdown`). Rectified view = `max(|min|, |max|)` from
  the bottom; merged stereo = min of mins / max of maxes over the channels.
  Both are view choices on the same columns.
- Qt Quick later: the same columns become one quad per column in a
  `QSGGeometryNode` (fastest option in the curve GPU benchmark).

### 3.6 Max amplitude (normalise)

`get_max_amplitude(start, end)`: interior from the pyramid with the same
segment-tree decomposition, the partial blocks at both ends from source
samples (< 64 frames each). To make the result exact instead of 1/8192
conservative, the block(s) holding the maximum are read from the source
(64 frames each). Replaces the `.norm` file. (Format 6: the interior comes
from the bound alone, at most one log step above the real peak: section 6.)

### 3.7 `TPeakBuilder` (later step)

Background job per source on a worker thread: reads the source in large
chunks, feeds `TPeakPyramidBuilder`, writes `TPeakFile`, reports progress,
cancellable, coalesces requests for the same source. Recording: the writer
thread feeds a `TPeakPyramidBuilder` per recorded clip; the GUI reads
published level counts (atomics, blocks below the count never change) so the
recording waveform needs no separate path. On finish the file is written
from memory, no re-read.

### 3.8 What moves out of the peak code

- Zoom steps (`zoomStep`, `ZOOM_LEVELS`, `max_zoom_value`) belong to the sheet
  view/session, not to peak data. Arbitrary zoom values work with any step
  list.
- `ClipTileCache` is removed when the PoC view becomes the only path.

## 4. Proof of concept results (2026-10-17)

`tests/peaks/test_peaks.cpp` (ctest `peaks`, ~11 s Debug): exact time
conversions against 128-bit math; pyramid against brute force at every level
(17 lengths + ~1M frames, 1-3 channels, random chunks == one append), ranges
on finished and growing pyramids; file round trip, every validation rule;
800 random column queries (rates 8000-384000, 0-80000 frames, 1/20 - 100000
frames per column, starts before/after the source, at the threshold) against
per-sample / per-block brute force, the pyramid columns also against the raw
samples (each within its own column or the one before), and the line
property (neighbouring sample columns touch). Mutations (block rule, missing
edge value, range split, odd-block promotion, short read) are all caught.

`tests/peaks/benchmark_peaks.cpp` (Release, i5-1245U, one hour 48 kHz
stereo, a 4K row of 2 channels, 200 px each):

- build: 300 ms for the whole pyramid (12000x real time); file 43 MB, write
  10 ms, open + validate 0.03 ms (mapped).
- query: 0.04-0.4 ms from the pyramid at every zoom; from samples 0.07 ms
  (1/4 frame per column) to 11 ms (63 frames per column, 500k frames read
  per channel; generated, so decoding a compressed file comes on top).
- paint, ms:

| frames/column | polygon envelope | spans, QPainter alpha rects | spans + outlines | spans, opaque rects | spans, own blend + drawImage |
|---:|---:|---:|---:|---:|---:|
| 0.25 | 1.2 | 0.6 | 1.7 | 1.1 | 0.7 |
| 8 | 48 | 1.8 | 4.1 | 1.8 | 1.5 |
| 63 | ~2700 | 23 | 29 | 2.6 | 2.9 |
| 4096 | 1.1 | 28 | 29 | 2.7 | 2.9 |
| 45000 (whole hour) | 72 | 32 | 34 | 2.8 | 3.1 |

QPainter blends a semi-transparent brush per span; with 1 px wide spans that
is ~10x slower than opaque spans. Opaque spans (or a premultiplied blend of
our own into an image drawn once) stay at ~3 ms for a full 4K row at any
zoom. Qt Quick column quads blend on the GPU.

User observation (2026-10-17, first runtime comparison): the new path
shows the correct maximum/minimum amplitude where the legacy view did not,
and no longer shows odd shapes at certain zoom levels. Causes in the legacy
code: nearest-power-of-two level snapping (a block up to 1.5x too wide or
too narrow per pixel), the start offset rounded to half a block, level-0
blocks not aligned to real frames for other rates, the micro view reduced to
one value per pixel, and values truncated towards zero.

## 5. Proof of concept status

Done (branch `peak-redesign`): parts 3.1-3.4, the tests and benchmark of
section 4, and the view path:

- `TWaveform` (core): one per clip source; opens the source's `.peaks` file
  on the first paint, has `TPeakBuilder` build it when it is missing or
  stale, answers `columns()` with a reader of its own for the sample source.
- `TPeakBuilder` (core): one worker thread, one job at a time, jobs for the
  same file are shared; a job owns its own reader; cancel on destruction.
- `TAudioClipView::draw_columns()` (first behind a setting, now the only
  path). One column per
  device pixel of the exposed rect, queried in chunks of 256 so a rounded
  column width cannot drift; gain = clip gain x clip curve x track curve x
  fades per column (`fill_gain()`); spans blended into a premultiplied image
  with the wave brush's colour per row (gradients kept), optional cosmetic
  outlines, then one `drawImage`. Classic and rectified and merged-stereo
  views come from the same columns. No tile cache.
- Verified headlessly (offscreen QPA, project "Out of sync"): first paint
  starts the build, the file is built and opened, the painted image matches
  the old tile's shape; the paint count per 20 s is the same as the old path.

- 3.6 normalise: `TWaveformColumns::max_amplitude()` (exact, tested against
  brute force incl. saturated blocks) via `TWaveform::max_amplitude()`;
  `TAudioClip::calculate_normalization_factor()` uses it when the waveform
  is ready and falls back to the old `TPeak` otherwise.

- 3.7 recording: the writer feeds a `TPeakPyramidBuilder` on the DiskIO
  thread (`rb_file_write`); the clip gets a live `TWaveform` on the growing
  pyramid (columns from the pyramid only, no samples); on `finish_export`
  the finished pyramid is handed to the peaks thread, which writes the
  `.peaks` file after the audio file is closed (DiskIO never waits for it);
  the clip's normal `TWaveform` keeps painting from the pyramid until that
  file opens.
  Thread safety without locks: chunked storage (blocks never move), a block
  is written once, per level an atomic published count (release/acquire),
  a snapshot per paint takes the minimum count over the channels. Tested:
  writer thread vs. snapshotting reader (`growing pyramid`), also under
  ThreadSanitizer. Zoom: while clips record, `TSheet::minimum_hzoom()`
  limits zooming in to one level-0 block per pixel at the device rate, and
  `prepare_recording()` zooms out to it; the limit lifts with the last
  recording clip. Imports through `TBufferedAudioStreamWriter`
  (`set_process_peaks`) now write `.peaks` files too. The old `TPeak` is no
  longer built during recording.

Parked, with its numbers (2026-10-17): **file-backed recording**. The
pyramid of a recording stays in memory until stop: 21.6 MB per channel-hour
at 48 kHz (43 MB stereo/hour; 100 stems x 15 min at 96 kHz = 1.1 GB, freed
when the take's file is written). Writing it to the `.peaks` file during the
recording would need a chunk-table header (per level and channel a list of
offset + count, chunks appended in fill order, header written at stop), the
builder writing a level's current chunk with batched `pwrite`s, and the live
view mapping the growing file; about 700 lines and a second storage backend.
Only worth it if sessions of 100+ stems over 15 minutes become a real
pattern; the stop-time freeze it would also remove is already gone (the
write runs on the peaks thread). Per-level temp files were rejected (file
descriptors), a separate index file too (no simplification, one more file to
keep in sync). Also parked: idle I/O priority for the peaks thread plus
back-pressure from DiskIO's buffer fill, for disk bandwidth contention of
builds with playback and recording; builds read sequentially in 64 K-frame
chunks and run one at a time.

- 3.8 zoom (2026-10-17): continuous. `get_hzoom()` no longer rounds to a
  power of two; limits are 32 px per frame (zoomed in) and the whole session
  filling the view (zoomed out, `TSheetView::fit_hzoom()`), the recording
  limit on top. `TSheetView::zoom_around(factor, x)` keeps the time under a
  viewport x in place: keyboard zoom (x1.5 steps) and the slider anchor on
  the work cursor / play head, the hold-Z jog (continuous, 100 px = factor
  e), the trackpad pinch and Ctrl+wheel anchor on the pointer. The slider
  is logarithmic between the limits. Zoom to fit = F while holding Z: the
  selected clips, else the whole session. `TPeak::zoomStep` /
  `max_zoom_value()` are no longer used.

- Zero crossings (2026-10-17): `TWaveformColumns::nearest_zero_crossing()`
  (sign change of the channel sum, the frame nearer to zero of the pair,
  within a distance; brute-force tested) via `TWaveform::nearest_zero_crossing(time, maxDistance)`,
  for click-free cuts; the split/trim commands do not use it yet (a snap
  option, controller work). Sample dots at extreme zoom are not wanted:
  the line through the samples is what the user expects.

- Removed (2026-10-17, after the user's comparison): `TPeak` (with its
  build thread and the `.peak` / `.norm` files), `ClipTileCache` and the
  tile painter, the `Themer/paintwaveformcolumns` setting. The column
  painter is the only waveform path; the repaint hooks that invalidated
  tiles now just repaint the affected rect. Old `.peak` files in project
  `peakfiles/` directories are simply unused.

Open after this branch: zero-crossing snap in the split/trim commands and
feedback on the targeted item (controller work), file-backed recording
(parked, section 4).

## 6. Format 6: log codes, RMS, normalise on the bound (2026-10-18)

Measured numbers and the verification: doc/design/peaks-v6-report.md.

### Values as log codes

A min, max or RMS value (1.0 = full scale) is an int16 code: 0 is silence,
code c > 0 is 2^((c − 28672) / 1024), −c the same value negated. 1024 codes
per octave, so a step is 2^(1/1024) = 0.0059 dB at every level, from about
−168 dB (code 1) to +24 dB (code 32767); larger values saturate at ±32767.
Codes compare like their values, so the pyramid combines min and max on the
codes. Decoding is two tables the compiler builds (1024 mantissas
2^(i/1024) from a constexpr function of our own, 32 powers of two; 4 KB):
`Octaves[c >> 10] * Mantissas[c & 1023]`. Encoding takes `frexp` and a
binary search in the same mantissa table, so a max rounded up and a min
rounded down hold their sample exactly; the RMS takes the nearest code.

The step at a level, linear (formats up to 5) against log:

| Level | linear 1/8192 | log, 1024 per octave |
|---|---|---|
| 0 dBFS | 0.001 dB | 0.0059 dB |
| −20 dBFS | 0.011 dB | 0.0059 dB |
| −40 dBFS | 0.11 dB | 0.0059 dB |
| −60 dBFS | 1.0 dB | 0.0059 dB |
| −78 dBFS | 5.9 dB (the last step) | 0.0059 dB |
| −120 dBFS | silence | 0.0059 dB |
| −168 dBFS | silence | the smallest code |

Near full scale the log step is coarser than the linear one: 0.07 % of the
amplitude, 0.3 px in a waveform 1000 px high.

### RMS plane

Every block also holds the RMS of its frames (nearest code) in an array of
its own next to min/max; `range()` for min/max does not read it. The builder
keeps a level's unpaired block's mean square exactly (a double) until its
pair arrives, so a parent comes from exact values and every stored RMS is
rounded once; a partial last block weighs its own frames. A column's RMS is
the root of its blocks' mean squares weighted by their frames, within one
step of the real one; from the samples it is the samples' RMS. The painter
fills the RMS band inside the envelope in a darker shade of the wave (fill
mode). An RMS that includes fades or the gain curve is a weighted query on
the same plane (weight g² per block), parked until a feature needs it.

### Normalise on the bound

`max_amplitude()` takes the blocks between the range's ends from the
pyramid's bound and reads only the two partial end blocks. The bound is
never below the real peak and at most one step (0.0059 dB) above it, so the
gain is at most that much too low and never clips. Below the smallest code
(−168 dB) the bound is that code's value. Only blocks beyond +24 dB
(float sources) are read, because their code only says "beyond".

### Sizes

A block takes 6 bytes per channel (min/max 4, RMS 2). A peak file holds
about 2 × level 0: 64.8 MB per hour of stereo audio at 48 kHz (format 5:
43.2 MB). A recording keeps about one period (22 s) per track in memory plus
one chunk per level: 270 KB per mono track (format 5: 180 KB).

