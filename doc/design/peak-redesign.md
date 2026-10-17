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
  (+12 dBFS, matches the gain clamp). Not 8 bit: a linear 8-bit value cannot
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

One file per source, `<source name>.peaks` in `peakfiles/`. Written with
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
(64 frames each). Replaces the `.norm` file.

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

## 5. Proof of concept status

Done (branch `peak-redesign`): parts 3.1-3.4, the tests and benchmark of
section 4, and the view path:

- `TWaveform` (core): one per clip source; opens the source's `.peaks` file
  on the first paint, has `TPeakBuilder` build it when it is missing or
  stale, answers `columns()` with a reader of its own for the sample source.
- `TPeakBuilder` (core): one worker thread, one job at a time, jobs for the
  same file are shared; a job owns its own reader; cancel on destruction.
- `TAudioClipView::draw_columns()`: behind the setting
  `Themer/paintwaveformcolumns` (default on in this branch). One column per
  device pixel of the exposed rect, queried in chunks of 256 so a rounded
  column width cannot drift; gain = clip gain x clip curve x track curve x
  fades per column (`fill_gain()`); spans blended into a premultiplied image
  with the wave brush's colour per row (gradients kept), optional cosmetic
  outlines, then one `drawImage`. Classic and rectified and merged-stereo
  views come from the same columns. No tile cache.
- Verified headlessly (offscreen QPA, project "Out of sync"): first paint
  starts the build, the file is built and opened, the painted image matches
  the old tile's shape; the paint count per 20 s is the same as the old path.

Not done yet: 3.6 (normalise still uses the old `TPeak`, so it needs an old
peak file), 3.7 recording (the recording waveform is not drawn by either
path today), sample dots at extreme zoom, removal of `TPeak` /
`ClipTileCache` / the zoom-step coupling once the user has compared both
paths.
