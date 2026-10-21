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
audio lost. Both work the same way. DiskIO stays per sheet; one
DiskIO for the whole project is a later step.

## 2. The scheduler thread

`TIOScheduler` is a `QObject` of the owner thread (the GUI) with a
scheduler thread of its own (`start()`); a test calls `dispatch()`, one turn
of the scheduler thread, without it. The scheduler thread (`run()`):
1. Wait for a wake: the audio thread's `tick()` every cycle, also with the
   transport stopped, or a request (every `add_stream()`,
   `add_idle_task()`, `remove_stream()`), so idle tasks run also without a
   driver. One counting semaphore with one waiter and any number of wakers:
   an atomic add, and a post to the system semaphore only while the
   scheduler thread waits; the audio thread and a request never wait for
   each other. After a round with idle work the next round follows without
   waiting.
2. Take the requests, between rounds and never during a step: streams added
   and removed, new idle tasks (one mutex, held only to swap the lists out;
   `retire()` runs outside it). A seek, a rate or a quality
   change are atomics the round reads. `remove_stream()` returns once the
   scheduler thread has let go of the stream (`retire()` frees its ring
   buffers); its owner deletes it then, on the GUI thread.
3. One round (`do_round()`):
   - every stream reports its state in one call (`disk_state()`: whether it
     has work, whether its fill counts, its fill);
   - every idle task has work (until its step says it is over);
   - `TIOPlanner` decides the order;
   - one step of each runs with one `QtConcurrent::blockingMap`, spread
     over the global pool. In Qt 6 `blockingMap()` starts the work on the
     pool and waits: the scheduler thread takes part only when the pool is
     busy (then it runs the round alone, so the scheduler always gets on). A
     round of one step runs on the scheduler thread without the hand-over
     to the worker threads.
4. After the round: the GUI's fill meter takes the emptiest stream in sync;
   a seek round reports `seekFinished`; idle tasks that are over are handed
   back (`retire()`, a build reports its result then, see
   §5) and deleted.

### Threads, ownership and hand-overs

| Thread | Calls | Has alone | May block |
|---|---|---|---|
| Audio thread | `tick()`, `set_transport_location()`, `set_seek_transport_location()`; it reads or writes the rings of the streams it processes | nothing of the scheduler | never: atomics, the wake's `signal()`, the streams' lock-free slot queues |
| Owner thread (GUI) | the constructor, `start()`, the destructor; `add_stream()`, `remove_stream()`; the meters | a stream before `add_stream()` and after `remove_stream()` returned | on the request lock for a moment; `remove_stream()` until the round that runs is over |
| Any thread | `add_idle_task()`; the setters (rate, quality, buffer size, idle allowed, idle limit) | — | on the request lock for a moment |
| Scheduler thread | `dispatch()`: the requests, the plan, a round; between rounds the streams' `disk_state()` and `set_output_rate_and_resampler()` (a new resampler), and `retire()` of streams and tasks | the stream list, the task list, the planner, the round's information | on the wake; on the end of a round; on disk IO when it runs a step itself (a round of one step, a busy pool) |
| Worker threads (the global pool) | `step()` of one stream or one task | that stream or task during its step | on disk IO |

| From → to | What | How |
|---|---|---|
| Audio → scheduler | transport location | `std::atomic<qint64>`, universal frames |
| Audio → scheduler | a seek | the location (`std::atomic<qint64>`) first, then the flag (release); the round takes the flag with `exchange()` (acquire) at its start, and the transport goes on from the location it served |
| Audio → scheduler | the wake | `LightweightSemaphore::signal()` |
| Audio ↔ steps | audio data | the stream's ring, its lock-free slot queues |
| Owner, any → scheduler | streams added and removed, tasks | lists under one `std::mutex`; the scheduler thread swaps them out and handles them outside it |
| Owner, any → scheduler | rate, quality, buffer size, idle allowed, idle limit | atomics |
| Scheduler → workers | the steps of a round | `blockingMap`, and the release/acquire pair `m_roundStarted`/`m_stepsDone` |
| Scheduler → owner | a finished seek | `seekFinished()`, queued (the object lives on the owner thread) |
| Scheduler → a task's owner | a task's result | the task's `QPromise`, set in `retire()` |

**A stream**
1. The owner makes it and calls `add_stream()` before the audio thread can
   see it; the rate and the rings (`prepare_rt_buffers()`) are set there, on
   the owner thread.
2. While registered, the scheduler thread and the worker threads change its
   disk side, the audio thread its side of the ring. A seek step with a new
   rate makes the rings again, on a worker thread, while the audio thread
   waits for the seek. *Open:* the GUI writes a reader's position and range
   in place (`m_streamStart`, `m_streamEnd`, `m_sourceStartLocation`)
   while the scheduler thread and the workers read them.
3. Before `remove_stream()` the owner takes it out of the audio processing.
   The project's close order and the stopped transport see to that; the
   scheduler does not check it.
4. `remove_stream()` returns once the scheduler thread took it out between
   two rounds and called `retire()`, which frees the rings. The owner
   deletes it then, on the owner thread.

**No deadlock in `remove_stream()`:** the owner holds no lock there that a
step needs, and a step never waits for the owner thread (no blocking call to
it, no wait for one of its futures). Closing a project removes its streams
one by one, after `TPeakBuildService::cancel_all()`: a cancelled build's
next step ends it, so only the first removal waits for a step that runs.
Measured with 641 streams and six builds cancelled: 12 ms with 5 ms steps,
20 ms with 20 ms steps. Removing a group at once is phase 3's, with one
scheduler for the application.

**A task:** the scheduler's from `add_idle_task()`. `step()` runs at most
once per round, on a worker or the scheduler thread. When it returns false
the scheduler thread calls `retire()` after the round and deletes the task;
at the end the destructor does that for the tasks that are left. A result
goes out through the task's `QPromise`.

**One seek at a time:** the owner asks for the next only after
`seekFinished`. Every seek goes through `TSheet::initiate_seek_start()`,
which refuses meanwhile: a locate from the GUI, and a JACK relocation
(`TJackDriver::process_callback()` → `TAudioDevice::transport_control()` →
`TSheet::transport_control()`). It is called from one thread per driver: the
audio thread when the transport follows JACK, the GUI otherwise. A debug
assert in `set_seek_transport_location()` checks it: a flag set at the
request and cleared just before `seekFinished`.

**The end** (destructor, owner thread), once the audio thread no longer
calls the scheduler: stop, wake and join the scheduler thread (a round that
runs ends first); assert that no stream is registered; retire and delete
the tasks that are left.

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
