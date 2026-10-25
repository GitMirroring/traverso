<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Real-time requirement and the audio processing path (design, 2026-10-25)

Real-time is a requirement; the audio processing path is the code it applies
to. This note defines both, names the markers that make the compiler and a
sanitizer enforce the requirement, and gives the order of work. Entry points
checked against master cb9786a0 and the stack branch review-stack 06f50ad5,
which the work is based on (it carries the IO scheduler and the stream reader
changes).

## 1. The requirement

A function is **real-time safe** when, every time it runs, it:

- does not allocate or free memory (`new`, `delete`, `malloc`, growing a
  container, `QString`/`QVariant`/`QByteArray` that allocate, implicit sharing
  that detaches, destroying a `std::function` or `std::move_only_function`
  whose target did not fit inline);
- does not take a lock that a non-real-time thread can hold, and does not wait
  (mutex, condition variable, semaphore, `sleep`, joining a thread);
- does not make a blocking system call (file or network IO, `printf`/`cout`,
  `qDebug`/`qWarning`/`qCritical`, anything that may page in from disk);
- does not emit a Qt signal or call into a `QObject` (a queued connection
  allocates, a direct connection runs any slot);
- does not throw;
- finishes in a bounded time that does not depend on data outside the cycle
  (no unbounded loops, no waiting for another thread to make progress);
- logs only with `TLOG_RT_*` (formatted in the drain thread; `TLOG_REALTIME_*`
  after step 6).

Allowed: lock-free single producer, single consumer structures; atomics that
are lock-free on every platform Traverso builds for (`static_assert` on
`std::atomic<T>::is_always_lock_free`, which matters for 64-bit types on 32-bit
ARM); memory that is allocated before the cycle and only reused.

Memory that is touched for the first time in the cycle can still fault a page
in. `TRealTimeAudioBuffer` locks its memory (`USE_MLOCK`, defined by the build
where the system has `mlock()`); a failed lock (`RLIMIT_MEMLOCK`) is only
logged. RealtimeSanitizer does not see page faults.

## 2. The audio processing path

Every driver waits for the device first: that wait is the cycle's clock, and
blocks by design (§5). The path is what runs after it:
**`TAudioDevice::run_cycle(nframes, delay)` and everything it reaches.** All
drivers end up there:

| Driver | The clock | Calls `TAudioDevice::run_cycle()` from |
|---|---|---|
| ALSA | `TAlsaDriver::wait()`, a poll on the device | `TAlsaDriver::_run_cycle()`, in `TAudioDeviceThread::run()` |
| null (also freewheel/export) | `TAudioDriver::_run_cycle()` sleeps until the next period | `TAudioDriver::_run_cycle()`, in `TAudioDeviceThread::run()` |
| PortAudio | the blocking API: `Pa_ReadStream()`/`Pa_WriteStream()` in `_read()`/`_write()`, inside the cycle | `TPortAudioDriver::_run_cycle()`, in `TAudioDeviceThread::run()` |
| JACK | libjack | `TJackDriver::process_callback()` (libjack's thread) |
| PipeWire | libpipewire | `TPipeWireDriver::process_callback()` (libpipewire's thread) |
| CoreAudio | macOS | the IOProc in `TCoreAudioDriver` (macOS's thread) |

From `run_cycle()` the path goes through `TAudioDevice::run_one_cycle()`, the
driver read/write callbacks (`TAudioDriverReadWriteCallBack`) and every
client's callbacks (`TProcessCallBack`, `TTransportControlCallBack`): the
sheets, tracks, buses, plugins, and the audio thread side of the disk streams.
Every function on this path must be real-time safe, except the clock of
PortAudio, which runs inside the cycle (§5).

`TRunCycleCallBack` is not on the path: it binds the driver's `_run_cycle()`,
which is the clock and then the path.

`TRealTimeCallBack` is where the path is wired together, so it is also where the
requirement can be made part of the type (§3.3).

## 3. Markers

One header, `src/support/TRealTime.h` (next to TLog, which the path uses
everywhere):

```cpp
// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The function meets the real-time requirement (doc/design/realtime-audio-path.md).
// Checked by Clang 20 and later; empty for other compilers.
#if defined(__clang__) && __clang_major__ >= 20
#define T_REALTIME_SAFE noexcept [[clang::nonblocking]]
#define T_BLOCKING      [[clang::blocking]]
#else
#define T_REALTIME_SAFE noexcept
#define T_BLOCKING
#endif
```

### 3.1 T_REALTIME_SAFE

`noexcept` with every compiler, so the function type is the same everywhere
and a real-time safe function cannot let an exception out (§1); Clang asks
for it too (`-Wperf-constraint-implies-noexcept`). An override of a marked
virtual has to be `noexcept` with every compiler.

On every function of the audio processing path: `TAudioDevice::run_cycle()`
and the functions it calls. With Clang, the body is checked at compile time
(`-Wfunction-effects`): allocation, `throw`, `thread_local`, static locals with
dynamic initialisation, and calls to functions not known to be real-time safe
are reported. Functions defined inline or in the same file are analysed by
Clang itself; a function in another file or library needs the marker too.

The first functions to mark are the ones the whole path calls: TLog's
real-time entry (`TLog::write_rt()`, `TLog::set_audio_cycle()`). `Q_ASSERT` needs
nothing: Clang does not report a call to a function that does not return
(`qt_assert()`), checked with Clang 21.

Virtual functions on the path: mark the base declaration; check with the first
one that Clang reports an override that does not keep the marker.

### 3.2 T_BLOCKING

On functions that block by design: the clocks of §2, disk reads and writes of
the IO scheduler, waits on locks, condition variables and semaphores, project
loading. Clang then reports a call from real-time safe code even when it cannot
see the body.

### 3.3 The callback type

`TRealTimeCallBack` binds member functions to a function pointer. When its
stub type and the bound method type carry `T_REALTIME_SAFE`, binding a method
that is not marked is reported where it is bound, and the call through the
callback is checked as real-time safe. This turns "every process callback must
be real-time safe" into a property of `TProcessCallBack`,
`TTransportControlCallBack` and `TAudioDriverReadWriteCallBack`, not of
`TRunCycleCallBack` (§2). Verify this with the first callback that is marked:
if Clang does not accept the marker on the method template parameter, mark
the stub type and the bound methods separately.

### 3.4 Naming

The markers name the requirement, not the place: a helper used by both the GUI
and the audio thread can be real-time safe. TLog follows the same term:
`TLOG_RT_*` becomes `TLOG_REALTIME_*`.

## 4. Checking

- **Compile time:** a Clang build (a second build in Qt Creator, later CI) with
  `-Wfunction-effects`. GCC and MSVC builds, including the Debian packages, are
  unaffected: the markers are empty there.
- **Run time:** `ENABLE_RTSAN` (Clang 20+, development only) adds
  `-fsanitize=realtime`. While a `T_REALTIME_SAFE` function runs, RealtimeSanitizer
  intercepts allocation, locks, sleeps and blocking calls anywhere below it, also
  inside Qt and system libraries, and reports them with a stack trace. Run the
  headless tests and the headless project runs on the null driver under it.

## 5. Statements the compiler cannot judge

Every marker is a statement about the code. Two families, each with a form
for a function and a form for one statement:

| | function | one statement |
|---|---|---|
| safe | `T_REALTIME_SAFE` | `T_REALTIME_SAFE_BY_CONTRACT(...)` |
| blocks | `T_BLOCKING` | `T_BLOCKING_BY_DESIGN(...)` |

The function markers are an agreement between caller and writer, checked by
Clang on both sides. The statement markers are for the writer only: the
places where Clang cannot see, and a person answers for the claim. "By
contract": the called code promises it is real-time safe. "By design": it
blocks, and that was chosen. Allowed only in a small named function, with a
comment that links here, and only around the one statement (`TRealTime.h`).
Four kinds:

- **The clock.** The driver's wait for the device (§2). Outside the path for
  ALSA and null; inside the cycle for PortAudio's blocking API, so there the
  read and write callbacks wrap it in `T_BLOCKING_BY_DESIGN(...)`: Clang's
  warning and RealtimeSanitizer off for that statement.
- **A bounded wait.** Known candidate: the audio thread waiting on the IO
  scheduler (C++20 semaphore with timeout), if the scheduler instrumentation
  shows it prevents dropouts. `T_BLOCKING_BY_DESIGN(...)` too: a bounded wait
  still blocks, by design; it is not real-time safe.
- **Made once, at start-up.** `tsmp()` and `audiodevice()` make their object
  on the first call, in the GUI thread before any audio thread runs; later
  calls only find it made. The making is in a function of its own, called in
  `T_BLOCKING_BY_DESIGN(...)`: RealtimeSanitizer counts every run of a
  `T_REALTIME_SAFE` function as real time, the first one too.
- **Code that is real-time safe by its contract** where Clang cannot see it:
  an LV2 plugin's `run()`, `jack_port_get_buffer()`,
  `pw_stream_dequeue_buffer()`, ALSA's mmap calls, the steady clock, a
  `thread_local` that is constant initialised. `T_REALTIME_SAFE_BY_CONTRACT(...)`:
  Clang's warning off for that statement; RealtimeSanitizer still checks at
  run time what it really does.

## 6. Order of work

1. `TRealTime.h`, `ENABLE_RTSAN` in `cmake/CompilerFlags.cmake`, and the Clang
   warning in the Clang build; the markers for TLog's real-time entry and
   `T_BLOCKING` on the known blocking functions (§3.2): they make the reports
   of step 2 right from the start.
2. Mark `TAudioDevice::run_cycle()` and `TRealTimeCallBack` (§3.3). Fix what
   Clang reports, file by file, each fix its own commit. Warnings, not errors,
   while this is ongoing.
3. Wrap what is real-time safe by contract and the PortAudio clock (§5).
4. Run the headless tests and project runs on the null driver with
   `ENABLE_RTSAN`; fix what it reports.
5. When the path is clean: `-Werror=function-effects` in the Clang build, so a
   new violation fails that build.
6. Rename `TLOG_RT_*` to `TLOG_REALTIME_*`.

## 7. Status on branch realtime-path (2026-10-25)

Steps 1 to 3 are done, and step 4 is done as far as the headless runs reach.
Clang 21 marks the whole path from `TAudioDevice::run_cycle()` down. It reports
7 calls, all of them the open points below. RealtimeSanitizer gives 0 reports
for the 26 tests and for all projects run headless on the null driver (71,
without blkafds). The headless runs load the projects and apply their
transactions, but they do not play.

Fixed on the way, each in its commit:

- `post_rt_task()` closures over 24 bytes were allocated in the GUI thread and
  freed in the audio thread. The check is three pointers now, and a closure has
  to be `T_REALTIME_SAFE`.
- Messages that allocated or formatted in the audio thread: `qCritical()` and
  `qDebug()` in `run_cycle()`/`run_one_cycle()`, ALSA's mmap error messages
  (a `QString`), and the ring read's debug messages (`QString` times and status,
  built before the call, also with debug off). TSheet's `PMESG` calls are now
  `TLOG_RT_*`.
- `is_gui_thread()` and asserts used `QThread::currentThread()`, which creates
  a `QThread` in a thread Qt did not start (JACK's, PipeWire's).
- `AudioBus` looped over a non-const `QList`, so `begin()` could detach it. Its
  monitors were passed by value.
- The slots used `std::vector::at()`, which throws.
- The dither seed was a static local.

Open, for the review (design choices, not made on the branch):

1. `transport_control()` serves the audio thread (the transport follows JACK)
   and the GUI thread. Its GUI branch calls `prepare_recording()` (files), and
   `start_transport_rolling()` and `set_recording()` emit their signals directly
   in their GUI branch. Proposal: the callback for the audio thread only, and a
   function of its own for the GUI.
2. The JACK driver's port pairs live in `QList`s that the audio thread appends
   to and removes from (`post_rt_task()`): one writer, no race, but `append()`
   allocates in the cycle. Proposal: a `TRtList` (a `std::vector` snapshot the
   GUI thread builds), like the rest of the path.
3. (Checked 2026-10-25, no problem: `TTrack::m_vumonitors` is filled in the
   constructor and never changed, the audio thread only reads it; the buses'
   and drivers' channel lists change only in `TAudioDevice::set_parameters()`
   between `shutdown()` and the new audio thread. Not checked: the old FIXME in
   `~TTrack` about monitors still in use when a track is deleted.)
4. The queue's closures are `std::move_only_function`, kept inline by a size
   check. A closure type of its own (an inline buffer, `noexcept` by type) would
   make that a property of the type.
5. Page faults and other stalls. `USE_MLOCK` is defined (CMakeLists.txt,
   where the system has `mlock()`), so `TRealTimeAudioBuffer`s are locked; a
   failed lock is only logged, and other memory of the path is not locked.
   Stalls that no check sees (they are not calls): a page fault on memory the
   cycle touches for the first time or that was swapped out; a TLB shootdown
   when another thread unmaps memory (a large `free()`, the allocator handing
   memory back, a mapped file closed), which interrupts the audio thread's
   core too. Against them: the path's buffers locked (and a failed lock made
   visible), and keep other threads from unmapping and mapping again and
   again (reuse buffers, keep the peak files' mappings open): a note for the
   next work on the peak and DiskIO side.
6. The buffers the path uses are `TRealTimeAudioBuffer`s (locked), and code off
   the path does not use them where a plain `TAudioBuffer` will do (user
   2026-10-25): check every `TAudioBuffer` the path reads or writes (the
   buses' channels, the slots, the curve buffer, the clip's and plugins'
   buffers, `TFileIOBuffer`s the audio thread touches) and every
   `TRealTimeAudioBuffer` outside it. `TAudioBuffer::is_memory_locked()`
   (review-stack 2b39ca5b) tells whether a lock was granted.
7. A run that plays: the clips' read path, recording, fades and plugins ran
   under RealtimeSanitizer only as far as the tests reach them. This waits for
   the headless runner.
8. Step 5 (`-Werror=function-effects`) once 1 and 2 are solved. Step 6 (the
   `TLOG_REALTIME_*` name) after the review.

Out of scope here: placing the audio thread on performance cores and thread
priorities (parked, see the notes on `run_on_cpu(0)` in `TAudioDeviceThread`).
