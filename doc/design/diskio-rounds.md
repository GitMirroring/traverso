<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# The IO scheduler as built (since 2026-10-20)

How the IO scheduler works now, thread by thread: the as built of the design
in `diskio-app-wide.md` (requirements, decisions, what is deferred and open).
It began as the rounds of the user's work order of 2026-10-19 ("DiskIO met
één rondelus voor readers, writers en peakbuilds", branch `peaks-v6`); the
file keeps that name.

Since 2026-10-21 DiskIO is the library `traversoioscheduler` (src/ioscheduler):
`TIOScheduler` (was `TDiskIOThread`), `TIOPlanner`, `TIOIdleTask`, and the streams
implement `TIOStream`.

Since 2026-10-22 (branch `app-scheduler`, `dispatcher-conclusie.md`) there
are no rounds any more: the application has one scheduler that reads and one
that writes, every sheet is a group in the read one, and a dispatcher starts
every step on its own. Sections 1 to 3 describe that; the round they replace
is in the history of this file. Sections 4 to 7 are as they were, but for
the measurements added to section 6.

## 1. Threads

The application has **two** schedulers (`TProjectManager`):
- one for the readers and the background work (peak builds), with a pool of
  half the cores plus one;
- one for the writers (recordings), with a pool of two.

Writing has its own scheduler so that recorded audio never waits behind a
read (a disk spinning up, a seek storm on a hard disk). Every sheet is a
group (`TIOGroup`) in the read scheduler: its transport, its seek, whether
it plays (or plays once its seek is done), whether it records, and the idle
limit of the disk of its audio. Writers have no group.

## 2. The scheduler thread

`TIOScheduler` is a `QObject` of the owner thread (the GUI) with a
scheduler thread (`start()`) and a pool of worker threads of its own; a test
calls `dispatch()`, one turn of the scheduler thread, without it, and
`dispatch()` waits then until the steps it started are done. The scheduler
thread (`run()`):
1. Waits for a wake: the audio thread's `tick()`, once per cycle from
   `TProject::process()`, also with the transport stopped; a request; a
   step that is done. One counting semaphore with one waiter and any number
   of wakers: an atomic add, and a post to the system semaphore only while
   the scheduler thread waits.
2. Takes the requests (groups and streams added and removed, new idle
   tasks: one mutex, held only to swap the lists out) and the reports of the
   steps that are done (another mutex, swapped out the same way).
3. Asks every stream whose step does not run what it needs
   (`disk_state()`), lets `TIOPlanner` choose what may start, and starts one
   step of each on the pool (`QThreadPool::start()`), no more than there are
   threads free. It never runs a step itself and blocks on the wake only.
4. A step runs on a worker thread and reports when it is done: its fill,
   whether a task is over, how long it took. The next dispatch starts what
   now fits.

### Threads, ownership and hand-overs

| Thread | Calls | Has alone | May block |
|---|---|---|---|
| Audio thread | `tick()`; a group's `set_transport_location()`, `request_seek()`, `set_playing()`; it reads or writes the rings of the streams it processes | nothing of the scheduler | never: atomics, the wake's `signal()`, the streams' lock-free slot queues |
| Owner thread (GUI) | the constructor, `start()`, the destructor; `add_group()`, `remove_group()`, `add_stream()`, `remove_stream()`; a group's `set_recording()`, `set_idle_limit()`; the meters | a stream before `add_stream()` and after `remove_stream()` returned; its group | on the request lock for a moment; `remove_stream()` until the scheduler let go of the stream (at once, or once its step that runs is done) |
| Any thread | `add_idle_task()`; the setters (rate, resampler, buffer size, idle limit) | — | on the request lock for a moment |
| Scheduler thread | `dispatch()`: the requests, the reports, the plan, starting steps; `disk_state()`, `set_output_rate_and_resampler()` and `retire()` of streams whose step does not run; `retire()` of tasks | the records of the streams, tasks and groups' seeks, the planner, what runs | on the wake only |
| Worker threads (the scheduler's pool) | `step()` of one stream or one task, then the report | that stream or task during its step | on disk IO |

| From → to | What | How |
|---|---|---|
| Audio → scheduler | a group's transport location | `std::atomic<qint64>`, universal frames |
| Audio → scheduler | a group's seek | the location (`std::atomic<qint64>`) first, then the flag (release); the scheduler takes the flag with `exchange()` (acquire), and the group's transport goes on from that location |
| Audio → scheduler | the wake, the tick count | `LightweightSemaphore::signal()`, an atomic count (one refill step per tick) |
| Audio ↔ steps | audio data | the stream's ring, its lock-free slot queues |
| Owner, any → scheduler | groups, streams added and removed, tasks | lists under one `std::mutex`; the scheduler thread swaps them out; a removal waits for a flag of its own |
| Owner, any → scheduler | rate, resampler, buffer size, idle limit; a group's playing, recording, idle limit | atomics |
| Scheduler → workers | a step | `QThreadPool::start()` of a runnable that carries its own `StepInfo`; a static atomic (release before, acquire in the step) shows the order to a thread sanitizer, which does not see QThreadPool's lock |
| Workers → scheduler | a step done | a report under a `std::mutex`, swapped out, and the wake |
| Scheduler → owner | a group's finished seek | `seekFinished(group)`, queued (the object lives on the owner thread) |
| Scheduler → a task's owner | a task's result | the task's `QPromise`, set in `retire()` |

**A stream**
1. The owner makes it and calls `add_stream()` before the audio thread can
   see it; the rate and the rings (`prepare_ring_buffers()`) are set there, on
   the owner thread. The scheduler keeps the rate it reads at and the
   buffer size of its rings.
2. While registered, its steps (one at a time, possibly on another thread
   each time) and the scheduler thread (between its steps) change its disk
   side, the audio thread its side of the ring. A seek step with a new rate
   or buffer size makes the rings again, on a worker thread, while the
   audio thread waits for the seek. *Open:* the GUI writes a reader's
   position and range in place (`m_streamStart`, `m_streamEnd`,
   `m_sourceStartLocation`) while the scheduler thread and the workers read
   them.
3. Before `remove_stream()` the owner takes it out of the audio processing.
   The project's close order and the stopped transport see to that; the
   scheduler does not check it.
4. `remove_stream()` returns once the scheduler thread took it out and
   called `retire()`, which frees the rings: at once, or after the step
   that runs. The owner deletes it then, on the owner thread.

**No deadlock in `remove_stream()`:** the owner holds no lock there that a
step needs, and a step never waits for the owner thread (no blocking call to
it, no wait for one of its futures). A removal waits only for its own
stream: 200 removals next to 50 ms idle steps take 5 ms (test).

**A task:** the scheduler's from `add_idle_task()`. One step at a time,
on a worker thread. When it returns false the scheduler thread calls
`retire()` and deletes the task; at the end the destructor does that for
the tasks that are left, after the pool's steps are done. A result goes out
through the task's `QPromise`.

**One seek at a time per group:** the owner asks for the next only after
`seekFinished(group)`. Every seek goes through
`TSheet::initiate_seek_start()`, which refuses meanwhile: a locate from the
GUI, and a JACK relocation. A debug assert in `TIOGroup::request_seek()`
checks it. A stream whose step runs when its group's seek comes seeks after
that step.

**The end** (destructor, owner thread), once the audio thread no longer
calls the scheduler: stop, wake and join the scheduler thread; wait until
the pool's steps are done; assert that no group and no stream is
registered; retire and delete the tasks that are left. The application's
schedulers end in `~Traverso`, after the last project went.

Streams tell the scheduler what they need (`disk_state()`); background work
is a `TIOIdleTask`, whose `step()` says when it is over:
- **A reader** has work when it is out of sync or its ring is at 80% or
  less. A reader **out of its window** (the transport far from its clip) has
  work only once the transport comes within its window, and then it seeks.
  It never counts for the idle work's fill.
- **A writer** has work as soon as there is a slot to write (it is recorded
  audio), or when the recording stopped and its file is not finished.
- **A step** reads or writes everything there is room for: a stream needs
  one refill step per tick at most.
- **A peak build** is an idle task: one read of a second of the source per
  step (a fifth of a second until 2026-10-23: fewer hand-overs to the pool).
  Steps may take different times (WAV, WavPack, MP3): none waits for
  another. The heaviest build of all (the most frames) never waits; the
  others take their turn in the order they came.

## 3. The planner

`TIOPlanner` is pure: no threads, no streams, no device. It is tested
headless (ctest `io_planner`). Every time the scheduler dispatches it gets
the streams and tasks whose step does not run, the threads that are free and
the idle steps that may start, and decides, in this order:
1. **the seeks of songs that play** (or play once the seek is done);
2. **refills, the emptiest first** (a writer's fill is its free part);
3. **idle work on the threads that are left**, within the idle budget:
   first the seeks of songs that do not play (a preparation to play), then
   the idle tasks.

No more stream steps than there are threads free; what does not fit waits,
and the priority is chosen again when a thread frees. Idle work:
- never takes the pool's last thread (the scheduler gives the planner a
  budget of the idle limit, at most the pool's threads minus one, minus the
  idle steps that run);
- starts when the emptiest reader of the songs that play is above
  `IdleStartThreshold` (80%), and goes on until it drops below
  `IdleStopThreshold` (60%);
- a seek of a song that plays stops it until the readers are above 80%
  again;
- off while any song records; the idle limit is the lowest of the songs'.

No time is measured for the plan and nothing is weighed: the kernel
schedules, the scheduler only knows the order. The steps are timed for the
CPU meter and for the log line at the end of a stretch of idle work ("idle
work done in … s, … % used").

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
  runs `step()`, possibly another one each step. The hand-overs give the
  happens-before: the report's lock from the step to the scheduler thread,
  QThreadPool's lock (and the static atomic a thread sanitizer sees) from
  the scheduler thread to the next step. The GUI reads the level-0 records
  below the stream's published count.

## 6. Measured

Release build, headless app on the Dummy driver, timed from the log's first
"building" to its last "built". The project copies have no peak files.

| Project | Before (idle loop, 3 ms per tick) | Rounds, tmpfs (limit 1) | Rounds, NVMe SSD (limit 12, a pool of 6 threads) |
|---|---|---|---|
| Bendictus, 6 sources (180 MB WAV) | 0.25 s | 0.25 s | 0.13 s |
| Pirates, 22 sources, 175 clips | 0.44–0.70 s | 0.45–0.53 s | 0.25 s |

2026-10-22, the dispatcher (`traverso_measure_idle_builds`, Release, 54
files of 60 s stereo in six formats at three rates, warm cache, 12 cores):

| Pool, idle limit | Rounds | Dispatcher |
|---|---|---|
| 6 (rounds) / 7 (dispatcher: 6 + 1), 6 | 1.75 s, 58% used | 1.22 s, 72% used |
| 2 / 3 (2 + 1), 2 | 2.96 s | 2.76 s |
| 6 / 7, 1 | 4.30 s | 4.4–4.7 s (two thread wakes per step, about 0.1 ms) |

App, Bendictus copy on the NVMe SSD: 0.38 s on rounds, 0.31 s with the
dispatcher.

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
