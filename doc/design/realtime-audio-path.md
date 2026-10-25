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
in. `USE_MLOCK` (`TRealTimeAudioBuffer` locks its memory) is not defined by the
build today; whether to define it is part of step 4. RealtimeSanitizer does
not see page faults.

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

One header, `src/support/TRealtime.h` (next to TLog, which the path uses
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

## 5. Deliberate exceptions

Allowed only in a small named function, with a comment that links here, and
only around the one statement (`TRealtime.h`). Three kinds:

- **The clock.** The driver's wait for the device (§2). Outside the path for
  ALSA and null; inside the cycle for PortAudio's blocking API, so there the
  read and write callbacks wrap it in `T_REALTIME_EXCEPTION(...)`: Clang's
  warning and RealtimeSanitizer off for that statement.
- **A bounded wait.** Known candidate: the audio thread waiting on the IO
  scheduler (C++20 semaphore with timeout), if the scheduler instrumentation
  shows it prevents dropouts. `T_REALTIME_EXCEPTION(...)` too: a bounded wait
  still blocks; it is an exception, not real-time safe.
- **Code that is real-time safe by its contract** where Clang cannot see it:
  an LV2 plugin's `run()`, `jack_port_get_buffer()`,
  `pw_stream_dequeue_buffer()`, ALSA's mmap calls, the steady clock, a
  `thread_local` that is constant initialised. `T_REALTIME_TRUSTED(...)`:
  Clang's warning off for that statement; RealtimeSanitizer still checks at
  run time what it really does.

## 6. Order of work

1. `TRealtime.h`, `ENABLE_RTSAN` in `cmake/CompilerFlags.cmake`, and the Clang
   warning in the Clang build; the markers for TLog's real-time entry and
   `T_BLOCKING` on the known blocking functions (§3.2): they make the reports
   of step 2 right from the start.
2. Mark `TAudioDevice::run_cycle()` and `TRealTimeCallBack` (§3.3). Fix what
   Clang reports, file by file, each fix its own commit. Warnings, not errors,
   while this is ongoing.
3. Wrap what is real-time safe by contract and the PortAudio clock (§5).
4. Run the headless tests and project runs on the null driver with
   `ENABLE_RTSAN`; fix what it reports. Decide `USE_MLOCK` (§1).
5. When the path is clean: `-Werror=function-effects` in the Clang build, so a
   new violation fails that build.
6. Rename `TLOG_RT_*` to `TLOG_REALTIME_*`.

## 7. Found while checking (2026-10-25)

What steps 2 and 4 will report first, at least:

- `TAudioThreadMessageQueue::post_rt_task()` accepts closures up to 64 bytes
  (trivially copyable), but libstdc++'s `std::move_only_function` keeps a target
  inline only up to 24 bytes (the size of its `_Delegate`). A closure of 25 to
  64 bytes is allocated in the GUI thread and freed in the audio thread
  (`event.rtTask = nullptr` in `process_posted_gui_events()`).
- `qCritical()` in `TAudioDevice::run_cycle()` and `qDebug()` in
  `run_one_cycle()`, on their failure paths.

Out of scope here: placing the audio thread on performance cores and thread
priorities (parked, see the notes on `run_on_cpu(0)` in `TAudioDeviceThread`).
