<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Peak files as records, level 0 first (design, 2026-10-20)

The simplified peak system, decided with the user on 2026-10-20 under the
KISS principle: simple where possible, complex only where it must be. It
replaces the periods, the in-memory pyramid and the lock-free reader
protocol of `peak-diskio.md` (sections 2 to 4). The value codes, the RMS,
the peak cache and the DiskIO rounds stay as they were.

## 1. File format: TRAVPEAK version 3

```
[header, 72 bytes][level 0 records][level 1 records] ... [top level: 1 record]
```

- **A record per block**, the same for every level: the min and max code of
  every channel, then the RMS code of every channel (int16 log codes, 1024
  steps per octave). 6 bytes per channel, 12 for stereo.
- **Level counts round up** (ceil): level L + 1 has half the blocks of level
  L, rounded up, down to a level of one block. The last block of a level
  may cover fewer frames; it is weighted by its real frames.
- **Every offset follows from the frame count** in the header: level L
  starts at `72 + recordBytes × (blocks of the levels below it)`.
- **The header is written last.** Until then it is zero, so an unfinished
  file is Invalid and gets rebuilt (it is a cache). It holds the source's
  rate, channels, frames, size and modification time, and a checksum.
- Separate planes (all min/max, then all RMS) would need the length before
  the first record is written; a recording does not know it.

## 2. Writing: one path for recordings, builds and imports

`TPeakStream`:

1. `open()`: a part file of its own, `<peak>.<pid>-<n>.part`, never one that
   exists already; 72 zero bytes for the header.
2. `append()`: every 64 frames a level-0 record. After each append the
   records are written, flushed, and the frames they cover are published in
   one atomic count (release). Nothing else is shared between threads.
3. `finish(audioPath)`: the partial last block; the source's size and time
   for the header.
4. `post_process()`, one piece per call (16384 level-0 blocks): the levels
   above level 0 from the decoded level-0 codes, in one pass.
   - Min and max are combined codes: exact.
   - The RMS comes from the mean squares of the decoded level-0 codes,
     weighted by frames, rounded once: within one code step (0.0059 dB) at
     every level. The user accepted this as invisible.
   - Each level collects its records and writes them at its offset, so the
     memory stays at a few hundred KB.
   - At the end: the header, then the rename to the peak file's name
     (`TPeakFile::publish()`, on Windows a few retries against a scanner or
     the indexer holding the file).

Who calls what:

| Writer | append | post_process |
|---|---|---|
| Recording | its writer, on the write DiskIO | a job of `TPeakBuildService` in the read DiskIO's idle time, asked for by `TAudioClip::finish_write_source()` |
| Build | `TPeakBuildTask` steps in DiskIO's idle time | the same task's next steps |
| Import, merge | their own thread | at once, in `finish_export()` (until imports move to DiskIO, a separate work order) |

**Write failure** (a full cache disk) never affects the recording: the
stream stops writing, the count freezes, the log says it once. `finish()`
removes the part file; there is no post-processing, and the waveform builds
the file from the audio afterwards.

## 3. Reading

- **While a file is written** the waveform reads the level-0 records in view
  below the count with plain reads (`TPeakStream::read()`), opening and
  closing the file per read: no handle stays open that keeps Windows from
  renaming it. The view (`TPeakLevels`) has level 0 only, a window of it;
  the column query is the one of a finished file, exact.
- **Accepted interim cost:** zoomed far out over a long take, this reads all
  of level 0 in view, 16 MB per channel-hour at 48 kHz, mostly from the
  page cache, on every full repaint. Measured: one hour of stereo in 3840
  columns, 14.8 ms per repaint (0.8 ms from the finished file). It ends with
  post-processing.
- **After the stop** the recording's file waveform asks for its peak file,
  finds the post-processing job of the same file and joins it: it shows
  level 0 until the job is done, then it maps the finished file. Once
  renamed, a read through the stream reads level 0 from the file's name (it
  is in the same place).
- **A finished file** is mapped (`TPeakFile`): every level an array of
  records. Columns take the middle of a column from the highest level that
  fits and the edges from the levels below; `max_amplitude()` is the bound
  from the levels plus exact reads at the edges. No maxima store.

## 4. Measured

Release, one hour of 48 kHz stereo (`traverso_peak_benchmark`):

| | master (periods, in-memory pyramid) | records |
|---|---|---|
| making the pyramid | 732 ms + 18 ms writing | level 0 551 ms + post-processing 172 ms |
| file size | 64.8 MB | 64.8 MB |
| whole hour in 3840 columns | 0.81 ms | 0.57 ms (finished file), 14.8 ms (level 0 while written) |

Headless app (Debug), Bendictus, 6 sources: build span 1.04–1.08 s against
1.6–2.1 s on master.

## 5. What went

- `TPeakPyramidBuilder`: the in-memory pyramid, its pages, published counts
  per level, released chunks and poisoned blocks;
- the periods and the tail, `TPeakFileWriter`, `write_periods()`;
- the snapshot of memory plus mapping, `remap()`, the reader protocol;
- the publish on the GUI thread and the posting of results to it.

The peak files (`TPeak*`, `TWaveform*`) went from 2606 to 2098 code lines.

## 6. Tests

Headless, on the pure classes (`tests/peaks`):
- a round trip against brute force (mono to three channels, partial last
  blocks, the RMS within half a step at level 0 and a step above);
- appends in any chunk size, builds in any slice size: the same file;
- a crash before the header, after level 0 and halfway post-processing:
  Invalid; a stream given up or gone halfway leaves nothing;
- a write failure in the middle of a recording (the process's file size
  limit): the count freezes, what was written stays readable, the stop
  leaves no file;
- a cancel during level 0 and during post-processing;
- the hand-over: a recording's post-processing as a task of its own;
- stress, also under ThreadSanitizer: a writer appends 256-frame buffers
  while a reader queries below the count; the finished file's columns equal
  the live ones (min and max exactly, the RMS within a step);
- the benchmark of the zoomed-out live query (section 4).

Not tested headless: the app's recording path (writer, clip, service), which
needs a runtime test.
