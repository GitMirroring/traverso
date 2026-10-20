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
   time budget per tick and a self-wakeup while work remains. (Since
   2026-10-20 a build is an idle task of DiskIO's round loop, one read per
   round, no time budget: doc/design/diskio-rounds.md.) While the
   sheet records, it does not build (flag). No thread of its own. First
   version: at most 3 ms per DiskIO wakeup (one per audio cycle), no
   self-wakeup. The other sheets of the project go on building while one
   records (accepted: peak files are rebuilt only after a new file version
   or when they are lost, and then nobody records yet).
4. Results reach the GUI through a `QPromise` driven by the build task and a
   `QFutureWatcher` in `TWaveform`: exactly one `finished` per watcher, no
   QObject job. A waveform never cancels a build; builds are cancelled when
   the project closes.
5. (Changed 2026-10-20 by the user's decision: peak files are a cache and
   are not synced any more; publish() only renames, on the GUI thread that
   holds the mapping: doc/design/diskio-rounds.md §5. What follows is the
   design as it was.) A peak file reaches its name only after it is **on
   the disk**: the finished part file is synced (fdatasync) and then
   renamed over its name (`TPeakFileWriter::publish()`). So a file under its name is always
   whole, also after a crash, and nothing checks block data afterwards: the
   header keeps a checksum (72 bytes, cheap, catches a damaged field that
   keeps the layout the same size), the blocks have none. A build publishes
   from the global thread pool (the wait can take a second on a slow disk,
   and DiskIO serves the streams); a recording publishes at its stop on its
   write-DiskIO, which has nothing else to do then, so its clips become
   final a little later (on ext4 the first sync also flushes the audio data
   still in the cache). Damage from other causes (bit rot, an outside tool)
   is not noticed: a wrong waveform until the source changes, accepted for
   a cache. History: the first design had one checksum over the file with a
   check in DiskIO's idle time, then a checksum per record and a "checked"
   flag; that grew to ~300 lines of glue with two of the three bugs the
   reviews found. Rule taken from that: a rare failure gets the simplest
   mechanism that excludes it, not one that detects it.
6. Peak files are prepared when a clip gets its source and its sheet, not
   at its first paint: whether a clip is visible does not matter. When peak
   files must be rebuilt, every sheet's DiskIO builds at once (accepted: it
   is rare) and as fast as the disk allows while no stream needs reading.
7. No network file systems for now.

## 2. File format, version 6

Version 6 (2026-10-18) stores values as log codes and adds an RMS plane:
doc/design/peak-redesign.md section 6.

```
[header, fixed size][record 0][record 1] ... [record n-1][tail]
```

- **Period**: 2^S level-0 blocks (2^(S+6) file frames). Inside a period level
  L ≤ S has exactly 2^(S-L) blocks, so all levels 0..S complete their part
  of a period at the same moment. S is in the header (default 14: 1,048,576
  frames, ~22 s at 48 kHz); tests use small values.
- **Record**: the blocks of one complete period for levels 0..S, level by
  level (2^(S-L) blocks each); per level the min/max of every channel, then
  the RMS codes of every channel. All records have the same size, so record
  i starts at `dataStart + i × recordSize`. No table. The planes sit inside
  every record, not one after the other in the file: a second plane's
  offset would depend on the length of the first, which a recording does
  not know while it streams.
- **Tail**, written at finish: the part of every level 0..S beyond the
  complete periods, then the levels above S, each level in the same order
  as in a record. Its layout follows from the frame count
  (`expected_block_counts`).
- **Header** (80 bytes; it names the planes, min/max and RMS, and the codes
  per octave), written at once with frame count 0 (= unfinished). Finish adds
  the frame count, the source's size and modification time (a recording's
  file is complete only then) and the header's checksum (a damaged period
  shift or frame count can keep the file size the same). A file with frame
  count 0 or a header that does not match its checksum is Invalid at open
  and is built again.
- The file is written as `<name>.part`, synced and renamed over `<name>` by
  publish (an atomic replace on POSIX). Writing in place cut a file short
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
writes its record from the builder's chunks and publishes it; the chunks of written periods can then be released. Builds
release them at once; a recording when no read is going on (section 4).

## 4. Steps on branch `peak-diskio`

1. Builder with a period shift, the writer and reader, the header checksum;
   headless tests (all frame counts around period edges, damaged files,
   sources that end early, many S). The existing build and write jobs use the
   new writer unchanged in behaviour.
2. A resumable build task (`step(budget)`; since 2026-10-20 `step()`, one
   read), core-free, with tests.
3. DiskIO idle runner, a build service on `TSheet`, `TWaveform` on
   promise/watcher; TPeakBuilder's thread goes.
4. The recording writer streams; the live view maps the growing file.

A recording streams through `TPeakRecording` (since 2026-10-20
`TPeakStream`, which builds stream through as well): the writer thread appends,
writes every complete period to the part file at once and publishes the
count. A reader (the GUI) reads inside `read()`: its snapshot names the
written periods in a mapping of the part file (remapped as it grows, new
mapping before the old one goes) and only the periods not written yet in
memory; it is valid during the call only, and views copy what they need
(also a Qt Quick item: it copies columns in its update, the render thread
never sees model memory). The writer frees a written period's memory as
soon as no read is going on; a read only defers that to a later append.
The reader announces itself before it loads the written count, the writer
publishes the count before it looks for readers (seq_cst): either the
writer sees the reader and waits, or the reader sees the count and does
not touch that memory. So memory per recording stays bounded (the current
period plus one 4 KB chunk per level), whether anything reads or not.
When a remap fails the snapshot ends where the mapping ends.
`finish_export()` writes the tail and the header, syncs and renames the
file; the clip's next waveform opens it. When writing fails the recording keeps its
peaks in memory and the waveform builds the file from the audio
afterwards. `TAudioFileCopyConvert` streams the same way.

Open: S (14 proposed), the mapping window size for growing files.

## The peak cache (2026-10-20)

User work order of 2026-10-19, with the adjustments agreed that evening.

**Where.** Peak files are no longer kept in `<project>/peakfiles/`. There
is one cache directory per user, `TPeakCache::directory()`: the platform's
cache location plus `/peaks`.

| Platform | Directory |
|---|---|
| Linux | `~/.cache/Traverso/Traverso/peaks` |
| macOS | `~/Library/Caches/Traverso/Traverso/peaks` |
| Windows | `%LOCALAPPDATA%/Traverso/Traverso/cache/peaks` (not roamed, right for a cache) |

The setting `Peaks/cachedir` moves the cache, for instance to another disk.
This gives:
- projects on a NAS or a read-only medium build their peaks locally;
- the same source in two projects has one peak file;
- project folders and their backups hold no cache;
- recordings to a hard disk write their peaks to the system disk;
- the space for peaks is bounded.

**The key.** The name is `<hash>-<name>.peaks`:
- `<hash>`: the first 32 hex digits of SHA-256 over the source's canonical
  path, in UTF-8 and NFC (macOS can hand out decomposed names);
- `<name>`: the file name, at most 40 characters, any character outside
  `A-Z a-z 0-9 . _ -` replaced by `_`, so it stays readable for debugging
  and valid on every file system (Windows' reserved names cannot occur
  behind the hash).

It is the path, not the file name, because two projects can each have a
`take1.wav`. The canonical path resolves symlinks, so a symlink and its file
share the peak file. A recorded file does not exist yet when its recording
starts; its directory's canonical path plus its name give the key it will
have once it exists. Validation is unchanged: the header compares rate,
channels, frames and the source's size and time. A different file at the
same path makes the peak file Invalid, and it is rebuilt.

The source's time may differ by up to 2000 ms. Network shares and FAT or
exFAT round times to 1 or 2 seconds, and without the tolerance a project
there could rebuild its peaks on every open.

A consequence: moving or renaming a project, or mounting a share somewhere
else, changes the keys, and the peak files are built again. With format 6 a
build takes about 1 s per hour of WAV on a local SSD. A project on a NAS is
network-bound: an hour of mono float WAV (~690 MB) takes 6–7 s to read over
gigabit.

**Use and eviction.**
- **Last use.** A peak file's mtime records its last use: `touch()` after
  every successful open, after a build and after a recording. atime is
  useless, since many systems mount with noatime or relatime.
- **Limit.** `Peaks/cachelimit` in MB, default 10240; 0 means no limit.
- **When.** After every successful build or recording, and once at
  startup, on the global thread pool, one eviction at a time.
- **Order.** Least recently used first, until the cache is within its
  limit. What is left alone:
  - the files this process has open, counted per file name (`TPeakFile`
    open, and a `TPeakStream` for its lifetime);
  - peak files younger than an hour, built or opened just now (otherwise a
    limit below the working set would remove a file before its waveform
    opens it, and rebuild it forever; the limit is a soft bound);
  - part files younger than a day, from a build or recording that may still
    run, also in another Traverso.
- **Leftovers.** Older part files are left over from a crash and go,
  whatever the limit.
- **Failures.** A file that cannot be removed is skipped (on Windows, one
  that another Traverso has mapped).
- **Other instances.** On Linux and macOS a file another instance has
  mapped can go: its mapping stays valid until that instance closes it.
- **Comparing names.** Files are compared by file name, not by path:
  std::filesystem and Qt write separators differently on Windows.

**Several instances.** A part file is named per process
(`<peak>.<pid>.part`). Two instances building the same peak file each write
their own part file, and each rename puts a complete file in place. With one
shared part name, each would truncate the other's file.

**Version.** The magic is still `TRAVPEAK`. Versions are counted from 2:
legacy was `TRAVPF` 1.5, and `TRAVPEAK` 3 to 6 only existed in development
branches. Reusing 2 is safe because the location is new: early-development
version-2 files lie in project `peakfiles/` directories that are no longer
read, and their header size and checksum differ anyway.

**Old peak directories.** `<project>/peakfiles/` is no longer read or
written, and is not removed automatically: it is in the user's project
folder. Opening a project that has one logs once that it can be removed.
New projects no longer get one.
