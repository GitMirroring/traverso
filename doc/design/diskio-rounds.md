<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# DiskIO in rounds (design, 2026-10-20)

User work order of 2026-10-19 ("DiskIO met één rondelus voor readers,
writers en peakbuilds"), executed in the version agreed with the user. This
describes what branch `peaks-v6` does now.

Since 2026-10-21 DiskIO is the library `traversoioscheduler` (src/ioscheduler):
`TIOScheduler` (was `TDiskIOThread`), `TIOPlanner`, `TIOTask`, and the streams
implement `TIOStream`. The rounds described here are unchanged.

## 1. Threads

Each sheet keeps **two** DiskIO threads:
- one for its readers and the background work (peak builds);
- one for its writers (recordings).

A round is a barrier: it ends when its slowest step is done. A read that
hangs (a disk spinning up, a seek storm on a hard disk) would otherwise hold
up the flush of the write buffers (about one second), and that is recorded
audio lost. Both threads run the same loop. DiskIO stays per sheet; one
DiskIO for the whole project is a later step.

## 2. The loop

`TIOScheduler` is a `QObject` of the GUI thread with a loop thread of its
own (`start()`); a test calls `dispatch()`, one turn of the loop, without
it. The loop (`loop()`):
1. Wait for a wake: the audio thread's `tick()` every cycle, also with the
   transport stopped, or a request. One semaphore, any number of wakers,
   the audio thread never blocks. After a round with idle work the next
   round follows without waiting.
2. Take the requests, between rounds and never during a step: streams added
   and removed, new idle tasks (one mutex). A seek, a rate or a quality
   change are atomics the round reads. `remove_stream()` returns once the
   loop has let go of the stream (`retire()` frees its ring buffers); its
   owner deletes it then, on the GUI thread.
3. One round (`do_round()`):
   - every stream reports its state in one call (`disk_state()`: whether it
     has work, whether its fill counts, its fill);
   - every idle task has work (until its step says it is over);
   - `TIOPlanner` decides the order;
   - one step of each runs with one `QtConcurrent::blockingMap`, spread
     over the global pool. In Qt 6 `blockingMap()` starts the work on the
     pool and waits: the loop thread takes part only when the pool is busy
     (then it runs the round alone, so the scheduler always gets on). A
     round of one step runs on the loop thread without the hand-over to the
     pool.
4. After the round: the GUI's fill meter takes the emptiest stream in sync;
   a seek round reports `seekFinished`; idle tasks that are over are handed
   back (`retire()`, a build reports its result then, see
   §5) and deleted.

A seek that comes in during a round is taken at the next round. Idle steps
of the running round that have not started yet give way to it; a read that
runs already finishes first, so a seek waits at most one step (a fifth of a
second of a source, longer on a slow share).

Streams tell the round what they need (`disk_state()`); background work is
a `TIOTask`, whose `step()` says when it is over:
- **A reader** has work when it is out of sync or its ring is at 80% or
  less. A reader **out of its window** (the transport far from its clip) has
  work only once the transport comes within its window, and then it seeks.
  Before, it only noticed this because it kept every audio cycle a round. It
  never counts for the idle work's fill: a clip far away would otherwise
  keep the idle work away for good.
- **A writer** has work as soon as there is a slot to write (it is recorded
  audio), or when the recording stopped and its file is not finished.
- **A step** reads or writes everything there is room for. A seek round
  lets every stream seek (and then fill), with no idle work.
- **A peak build** is an idle task: one read of a fifth of a second of the
  source per step, about what a reader reads per refill. So the steps of a
  round take about as long as each other and a round stays short.

## 3. The planner

`TIOPlanner` is pure: no threads, no streams, no device. It is tested
headless (ctest `diskio_planner`, mutation-checked). Per round it decides:
- **The streams that need work, the emptiest first.** `blockingMap` starts
  at the front.
- **Idle tasks on the threads that are left** (the pool's), never more
  than the idle limit, and only while the hysteresis allows it:
  - idle work starts when the emptiest reader that counts is above
    `IdleStartThreshold` (80%, the threshold a reader gets a step at);
  - it goes on until that reader drops below `ResumeThreshold` (60%);
  - a seek stops it until the readers are above 80% again;
  - recording turns it off altogether (`set_idle_work_allowed(false)`).
- **Whether the next round follows without waiting for the tick:** only
  with idle work. A stream's step fills or flushes all it can, so a stream
  that still wants work waits for the next tick, and one that cannot make
  progress does not spin.

No time is measured and nothing is weighed: the kernel schedules, DiskIO
only knows the order. The CPU time the GUI shows is a display, not a plan.

## 4. The idle limit

How many builds may read at the same time:
- **One on a hard disk:** a second read sends it seeking.
- **As many as there are threads on an SSD.**

On Linux the kind of disk of the audio sources decides: in sysfs, the
device's `queue/rotational`, or the disk's for a partition. When the file
system has no block device of its own (tmpfs, a btrfs subvolume, a network
share) the kind is unknown, and the limit is one. On Windows and macOS it is
always one for now: the seek-penalty query (`IOCTL_STORAGE_QUERY_PROPERTY`)
and IOKit's "Medium Type" could tell, but they are untested here. The
setting `DiskIO/IdleLimit` overrides the limit (0 means: from the disk).

## 5. Peak builds and their waveform

- **One path** (since 2026-10-20: doc/design/peak-records.md). `TPeakStream`
  appends level 0 to the part file and post-processes the levels above it.
  A recording feeds it from its writer, a build from a decoder
  (`TPeakBuildTask`).
- **The waveform grows.** All clips of a source share one build and one
  stream (`TPeakBuildService`, one job per peak file). While a waveform is
  Building, it answers column queries from what is built so far and
  returns empty columns past it. The clip view repaints with the build's
  progress, at most ten times a second.
- **Limitation:** a build goes from the start of the source to its end, so
  the middle of a long source appears only when the build gets there.
  Putting visible clips first is a later refinement.
- **The rename happens in DiskIO.** Nobody maps the part file (the GUI
  reads level 0 with plain reads), so the last post-processing step renames
  it, also on Windows. Then DiskIO hands the finished job back
  (`retire()`), its future finishes, and a waveform that opens the file
  finds it. A recording's post-processing is such a job too, asked for by
  `TAudioClip::finish_write_source()`. A crash in between leaves a part
  file, which is never opened; the next open builds again.
- **No fsync for peak files** (decided by the user, 2026-10-19). Peak
  files are a cache: a file damaged by a power loss shows a wrong waveform
  until it is built again. What stays:
  - the header is written last (zero until then: unfinished);
  - a file gets its name only once it is complete;
  - an unfinished part file is removed;
  - `TPeakFile::open()` never reads part files.
  Recorded audio is another matter: doc/design/recording-data-safety.md.
- **One thread appends at a time.** In a build that is the thread that
  runs `step()`, possibly another one each round. The round's hand-over
  gives the happens-before: blockingMap synchronises inside Qt, and two
  atomics around the round make it visible to a thread sanitizer too. The
  GUI reads the level-0 records below the stream's published count.

## 6. Measured

Release build, headless app on the Dummy driver, timed from the log's first
"building" to its last "built". The project copies have no peak files.

| Project | Before (idle loop, 3 ms per tick) | Rounds, tmpfs (limit 1) | Rounds, NVMe SSD (limit 12, a pool of 6 threads) |
|---|---|---|---|
| Bendictus, 6 sources (180 MB WAV) | 0.25 s | 0.25 s | 0.13 s |
| Pirates, 22 sources, 175 clips | 0.44–0.70 s | 0.45–0.53 s | 0.25 s |

The first version of the round looked at every stream three times per
round. With a reader per clip (175 here) that made builds 50% slower, until
a stream reported its state in one call.

Checked:
- GCC and Clang builds, ctest;
- ASan/UBSan: tests, plus the app building 22 peak files;
- TSan: the threaded tests are clean (the recording and the build stress
  test). In the app, the only reports left on the new code are
  QtConcurrent and QFuture internals (Qt here is not built for TSan).

## 7. What was removed

- the idle loop with its time budget (`IdleBudget`, `run_idle_tasks*()`,
  `m_moreIdleWork`);
- `std::function` idle tasks;
- the budget in `TPeakBuildTask::step()`;
- the thread-pool publish with its sync;
- the platform variants of `sync_to_disk()`;
- the "Building Peaks: x%" text.
