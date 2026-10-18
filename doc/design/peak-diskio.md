<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Peaks streamed to disk, built in DiskIO (design, 2026-10-18)

Follow-up of `peak-redesign.md` after the review of the peak classes
(2026-10-17/18). Decisions are the user's; open points are marked.

## 1. Decisions

1. Peak files are written as a **stream**, for builds from an audio file and
   for recordings alike. Nothing waits for the whole pyramid in memory.
2. One data path: a waveform always reads a mapped peak file, also while it
   is still being written (a recording). The in-memory pyramid of a
   recording, its switch to the file at stop and the stop-time write go.
3. Builds run in **DiskIO's idle time**: the DiskIO of the sheet whose clip
   asked for the peaks, in its idle branch (no stream needs work), with a
   time budget per tick and a self-wakeup while work remains. While the
   sheet records, it does not build (flag). No thread of its own. First
   version: at most 3 ms per DiskIO wakeup (one per audio cycle), no
   self-wakeup.
4. Results reach the GUI through a `QPromise` driven by the build task and a
   `QFutureWatcher` in `TWaveform`: exactly one `finished` per watcher, no
   QObject job. A waveform never cancels a build; builds are cancelled when
   the project closes.
5. A **checksum** in the header tells whether the block data is complete.
   No fsync: the peak file is a cache. Validation (and a sequential read that
   doubles as a prefetch) runs off the GUI thread.
6. No network file systems for now.

## 2. File format, version 3

```
[header, fixed size][record 0][record 1] ... [record n-1][tail]
```

- **Period**: 2^S level-0 blocks (2^(S+6) file frames). Inside a period level
  L ≤ S has exactly 2^(S-L) blocks, so all levels 0..S complete their part
  of a period at the same moment. S is in the header (default 14: 1,048,576
  frames, ~22 s at 48 kHz); tests use small values.
- **Record**: the blocks of one complete period for levels 0..S, level by
  level, channel by channel (2^(S-L) blocks each). All records have the same
  size, so record i starts at `dataStart + i × recordSize`. No table.
- **Tail**, written at finish: the part of every level 0..S beyond the
  complete periods, then the levels above S, each level by channel. Its
  layout follows from the frame count (`expected_block_counts`).
- **Header**, written at once with frame count 0 (= unfinished). Finish adds
  the frame count, the source's size and modification time (a recording's
  file is complete only then) and the checksum over all block data in file
  order and the header itself (a damaged period shift or frame count can
  keep the file size the same). A file with frame count 0 or a checksum mismatch is Invalid and is
  built again.
- The file is written as `<name>.part` and renamed over `<name>` by
  finish (an atomic replace on POSIX). Writing in place cut a file short
  under a waveform that had it mapped (SIGBUS, found in the first headless
  run); the rename leaves an existing mapping on the old file. An unfinished
  part file says so in its header and is removed when the write is given
  up. Records are flushed as they are written, so a mapping of the growing
  part file sees them (for the recording view, step 4). Open on Windows:
  replacing a file that is mapped fails there; such a build reports an
  error.

In `TPeakLevels` a level L ≤ S has chunks of 2^(S-L) blocks: chunk i is the
level's part of record i, the last chunk may be in the tail. A level above
S is one chunk in the tail. This is the chunked view the pyramid already
has, so `TWaveformColumns` and the painters do not change.

## 3. Writing

`TPeakPyramidBuilder` gets the period shift: the storage of level L ≤ S is
chunked per period (chunk shift S-L; this also removes the 256 KB chunks of
the high levels that wasted memory). When a period completes the writer
writes its record from the builder's chunks, adds it to the checksum and
publishes it; the chunks of written periods can then be released. Builds
release them at once. A recording's view still reads the current period
from memory; **only the GUI frees** period storage, once its mapping covers
the period (the writer hands the storage over).

## 4. Steps on branch `peak-diskio`

1. Builder with a period shift, the v3 writer and reader, the checksum;
   headless tests (all frame counts around period edges, damaged files,
   sources that end early, many S). The existing build and write jobs use the
   new writer unchanged in behaviour.
2. A resumable build task (`step(budget)`), core-free, with tests.
3. DiskIO idle runner, a build service on `TSheet`, `TWaveform` on
   promise/watcher; TPeakBuilder's thread goes.
4. The recording writer streams; the live view maps the growing file.

A recording streams through `TPeakRecording`: the writer thread appends,
writes every complete period to the part file at once and publishes the
count; the GUI's snapshots read the written periods from a mapping of the
part file (remapped as it grows, new mapping before the old one goes) and
the rest from memory. A period's memory is freed only after the GUI's
mapping covers it: each `levels()` call tells the writer which periods the
previous snapshot read from the mapping only. `finish_export()` writes the
tail and the header and renames the file; the clip's next waveform opens it.
When writing fails the recording keeps its peaks in memory and the waveform
builds the file from the audio afterwards. `TAudioFileCopyConvert` streams
the same way.

Open: S (14 proposed), the mapping window size for growing files.
