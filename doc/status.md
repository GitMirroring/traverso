<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Traverso status

Where each foundation stands, what was verified how, and the open tasks by
priority. The agent updates this file at the end of every session; Remon
checks it when reviewing. It replaces the draft outside the repository
(`../status.md`, 2026-10-22) and the task list in the agent's notes.

First version 2026-10-27, made from git (branches, worktrees, log), the old
draft and the agent's notes. Lines marked **(check)** could not be verified
from the repository: Remon confirms or corrects them.

Rule: at most three areas "in progress" at the same time. A fourth one waits,
or another one is finished or parked first.

## Legend

- **done**: tests green, design doc current, merged into master
- **in progress**: being worked on now
- **awaiting review**: built, Remon reviews before the merge
- **parked**: started, stopped on purpose; the note says where it stands
- **planned**: designed or decided, not started

## Repository

- `master`: `realtime-path` and `scenario-decoder` merged 2026-10-27, not
  pushed (`origin/master` = `6dc4fe2b`).
- Worktrees: `traverso` (master), `traverso-input-controllers`,
  `traverso-curve-gpu-bench`. The merged branches `realtime-path` and
  `scenario-decoder` are deleted, with their backups and build directories.

## Foundations

| Area | Status | Branch | Design doc | Next step |
|---|---|---|---|---|
| Library split (support, dsp, audioruntime, audiofileio, waveform, ioscheduler) | done | master | — | R14: each lib and a headless test on its own |
| RT state (TRtList, TRtValue, TRtTransaction, curves, fades, owners) | done | master (2026-10-14…17) | `rt-transaction-design` (agent notes; not in the repo) **(check)** | processing-node graph design (not now); JACK port lists with the JACK revision |
| Session log (TLog) | done | master (2026-10-17) | — | — |
| GUI frame clock | done | master (2026-10-17) | — | — |
| Audio file I/O: `TAudioReader` / `TAudioWriter`, decoders sample exact, resampler choice | done | master (review stack, 2026-10-25) | — (decisions in commits and agent notes) | R7–R10 (from the scenario-decoder review), R11, R20, R21, R23–R26 |
| Waveform library, peak format v4, waveform grid | done | master (review stack, 2026-10-25) | `peak-records.md`, `waveform-grid.md` | post-merge items (below); Remon's in-app grid check **(check)** |
| IO scheduler (lib, thread, app-wide schedulers, dispatcher) | done | master (review stack, 2026-10-25) | `diskio-app-wide.md`, `diskio-rounds.md` | R4 (stream-window race); requirements R1–R18 of `diskio-app-wide.md` §0 still marked concept **(check)** |
| Recording latency compensation | done, not runtime-tested with a loopback | master (review stack) | `recording-latency.md` | loopback test |
| Recording session (`TRecordingSession`) | done | master (review stack) | `recording-session.md` | writer ownership by the session (`ioscheduler-taak.md` "Later") **(check)** |
| Real-time path (markers, `TRealTimeClosure` / `TGuiClosure`, RTSan, mlock) | done (reviewed 2026-10-25; the commits after that review, incl. f7a782d6 and 654144c6 of 2026-10-27, not yet seen by Remon) | master (2026-10-27, `654144c6`) | `doc/design/realtime-audio-path.md` | Clang: 7 `-Wfunction-effects` reports left (split `transport_control()`: 3; JACK port lists: 4), then step 5 (`-Werror=function-effects`) and 6 of the note |
| Headless core tests: scenario decoder | steps 1–4 done (reviewed by Remon 2026-10-27, merged `184eca11`); also peak builds through the reader tested | master | `Work order fake decoder for headless core tests (revised).md` (outside the repo; status note and "Step 3 check" inside) | step 5 waits for O1 (wanted behaviour per fault); steps 6–8 wait for R6, `TTestProject`, a scheduler seam |
| Controllers / input layer (branch 3) | awaiting answers | `input-controllers` (design only, 2026-10-18) | `input-controllers.md` (on the branch), `actions-and-controllers.md` (outside) | Remon answers §10 (11 questions; 1, 2, 3, 5, 6, 8 block the fader reference port) |
| Shortcut system | first part done (action registry, 2026-10-14) | master | — | the rest after branch 3 (explicit items, marker classes, hold-tool factories, QML) |
| Test fixture (`TTestProject`) | planned | — | `actions-and-controllers.md` §6 | own task, after R6 |
| TimeRef enforcement and timebase | planned; interim fix done (`aaa94dbc`) | — | `design-principles.md` §5 (outside) | R11 first; universal rate 112,896,000 and the upgrade path for old projects before a release |
| Organic interface (Qt Quick timeline) | planned | `curve-gpu-bench` (reference only) | `curve-gpu-benchmark.md` (on that branch) | after controllers and the fixture |

Two areas are in progress: the scenario decoder and branch 3 (waiting for
answers).

## Review of 2026-10-25, per branch

How each branch of the review stack and `realtime-path` was verified (from the
retrospective of 2026-10-26).

| # | Branch (commits) | Time | How verified | Found | Result |
|---|---|---|---|---|---|
| 1 | realtime-path (11) | 15 min | runtime test (Minidisk: play, stop, seek, move, split, fade), the markers in Qt Creator, §7 of the note | marker placement (rejected after explanation), the closure fault, TLB/kernel fences | §5 extended; that evening the renames and all §7 decisions |
| 2 | the agent's fix commits on the stack (±35) | 50 min | line by line | double `to_nframes()`, `write()` vs `planar_write`, libsndfile types in core, `multiply()` used once, `write_buffer` double work, `friend` and a private `m_size` | 5 commits (2f218074, 0581685a, 06cb3110, d31e5049, 26a780f8), a task, "buffers on the path" |
| 3 | resampler-choice (4) | 45 min, kdiff3 | dependencies for headless tests | settings read in the stream reader, `config()` below the app | `TResamplerSettings` (48d48c84, d0e0a1fa); R5 part |
| 4 | app-scheduler (23) | 12 min | read | no issues; two wake-ups per process cycle | note |
| 5 | recording-latency (15) | 12 min | read | member names `m_readSource` / `m_writer` | rename in R5 |
| 6 | recording-session (9) | 7 min | read, judged on risk | `TRecordingCommand` looks like a copy of `TCommand` | task with the controller refactor; accepted: removable on its own without effect on the pipeline |
| 7 | peak-format-v4 (8) | 16 min | runtime test by ear | nothing; an exact headless test would tell more | fast-forward |

Open from this review: the largest and most concurrent branch (app-scheduler)
got the least time; it is reviewed against the thread contract
(`diskio-rounds.md` §2) when the scheduler is touched next.

## Tasks, by priority

The R number is the rank (R1 highest). A new task is inserted at its rank and
the ones below are renumbered; older commit messages and notes keep the old
numbers. Code problems found in a review come here, with facts and a
proposal, and are not decided or built during the review; Remon ranks them.
R1–R3 are done (2026-10-24: reader layering and names, writer finish, lossy
decoders sample exact), and so is the old R8 (MAD and FAAD after a failed
seek, 2026-10-27). Renumbered 2026-10-27: old R5–R7 → R6, R7, R9; old
R9–R13 → R10–R14; old R14–R17 → R18–R21; old R18 → R27.

- **R4 (high): race in the stream windows.** The GUI writes `m_streamStart`,
  `m_streamEnd` and `m_sourceStartLocation` of a reader in place
  (`TAudioClip.cpp:805`, split/trim at `:263,344,356,414,761`) while the
  scheduler thread (`transport_in_window()`) and a seek step
  (`TBufferedAudioStreamReader.cpp:318-339,405`) read them. Only while editing
  during playback. Proposal (awaiting Remon): a `Window{start, end,
  sourceStart}` behind a mutex, set in one call, copied once per step; a TSan
  test. Describe first what happens to audio already in the ring.
- **R5 (high): imports and merges resampled at the device rate**
  (`TBufferedAudioStreamWriter.cpp:60`; Fable review 2026-10-25): with the
  device rate ≠ the source rate a converted import comes out at the wrong
  length and pitch. Runtime test first (New Project dialog converting a file,
  device rate ≠ source rate).
- **R6 (medium-high): split `TBufferedAudioStreamReader` into the source
  record and the live stream.** Record always present per clip; `open(record)`
  gives a stream or nullptr; `m_error`, `init()`, `m_isReadSourceValid` go;
  relinking a missing file becomes possible; fixes the unchecked dereference
  in `AudioClipEditDialog.cpp:125`. Also: no `config()` below the app (the
  recording settings first; removes the link cycle that blocks core tests);
  renames `m_readSource` → `m_playbackStream`, `m_writer` →
  `m_recordingStream`, `m_readSourceId` → `m_sourceId`; with it the
  `TAudioWriter` close result and `TAudioFileMerger`'s missing
  `finish_file()` (review finding #18).
- **R7 (medium): NaN to the output.** Prevent at the reader boundary on the
  decoder's output (before the resampler); a safety net in
  `TAudioDevice::run_one_cycle()` before `m_driver->write()` (NaN also comes
  from plugins: a named exception to "prevent, do not detect"). Export is not
  covered by the net: a third place, or out of scope.
- **R8 (medium; built on `audiofileio-errors` 2026-10-27, test and review pending): audiofileio errors** (`doc/design/audiofileio-errors.md`):
  one error type, `std::expected` for open, close and the rate, a result
  with the count and the error for every block call, decoders and encoders
  included, `close()` required (with it the merger's missing close,
  `TAudioFileMerger.cpp:121-124`), `TAudioResampler::valid()`. With it:
  `MadAudioReader` checks its `seek(0)` at open (`:587`), FAAD's dead
  `if (!d)` and its size narrowing (`:600`, `:256`). Before R9, which needs
  its error.
- **R9 (medium): read errors are hidden in core.** The reader reports them
  now (a short count on both paths, done in `scenario-decoder`); the stream
  logs or ignores the count and sets IN_SYNC
  (`TBufferedAudioStreamReader.cpp:381-384,465-469,501`), and the peak build
  stops at a damaged part (`TAudioFileBuildSource::read()` returning -1 on
  an error, as its contract says, is decided here, not in R8). The error
  reaches the stream through R8. Wanted behaviour first (open question O1,
  parked 2026-10-27): silence and one log line per stream, or a read-error
  state and a signal to the clip.
- **R10 (medium): MP3 without libmad goes to libsndfile, whose seeks are not
  exact.** Proposal: refuse MP3 without MAD and tell the user. Behaviour
  change: Remon confirms.
- **R11 (medium): TTimeRef frame-conversion cleanup.** One symmetric style
  (`from_frames` ↔ `frame_at_or_before/after`), one call for rate-to-rate
  counts, one call for `to_nframes(frame_*(...))` (8 sites). Text/QTime family
  later.
- **R12 (medium): clocks.** One function per domain: monotonic for intervals
  (driver cycles, scheduler, rings, autorepeat, profiling), wall time only
  where stored; stored times in UTC (backup file name, session log header).
  Also: `TTimeRef::get_*seconds_since_epoch()` use `high_resolution_clock`
  (the wall clock in libstdc++) for intervals, and return `long`, which is
  32 bits on Windows: nanoseconds since 1970 overflow there (`qint64` or
  `trav_time_t`).
- **R13 (medium): strength of the unique IDs.** `create_id()` = seconds × 10⁹
  + `rand()` seeded by seconds; collisions possible across seconds and between
  two instances. Investigate where uniqueness is assumed, then a stronger
  scheme (format change through the project converter).
- **R14 (medium): each lib and a headless test on its own.** File format
  enums out of `support/defines.h` into audiofileio; the encode settings
  without dsp headers; the app without ALSA and lilv headers.
  With it (API review I2, 2026-10-27): `TAudioEncodeSettings` gets one format
  field: `M4A` in `FileFormat`, `create_audio_encoder()` chooses by
  `fileFormat`, `writerType` and the dead `blockSize` leave the settings
  (`writerType`/`fileFormat` must agree today without a check).
- **R15 (medium): `transport_position` test failed 3 times under ctest load**
  (2026-10-27, Clang and GCC; not reproduced in 72 parallel runs). First find
  out which check: a torn read is a real race in the frame clock's seqlock, a
  starved reader only a test too tight for a loaded machine.
- **R16 (medium-low): a WavPack file of unknown length** reports 2^32-1
  frames (`WavpackGetNumSamples()` -1, `WPAudioReader.cpp:45`): a clip of
  27 hours, and an error state at its real end.
- **R17 (fixed on `audiofileio-errors` with R8, awaiting review): `WPAudioWriter::write_block()`** closes the file and
  nulls `m_wp` on a failed write without a log line, and
  `WavpackCloseFile()` is never called afterwards (`WPAudioWriter.cpp:137-141`,
  `close()` returns at `:240-242`): a leak and a silent failure.
- **R18 (medium-low): parallel readers for a long compressed file's peak
  build**; then `TIOIdleTask::weight()` goes.
- **R19 (low): `TAudioBuffer` operations auto-vectorised, hand SIMD
  retired.** Measure first.
- **R20 (low, Ben): Traverso's AAC writer** drops the first 2048 frames and
  writes raw ADTS into `.m4a`.
- **R21 (low): the export dialog's MP3 and Ogg options** do nothing (fixed
  VBR quality); map them to libsndfile or drop them.
- **R22 (low): the waveform library's error style** (`TPeakFile::Status` +
  `QString*` + sticky `error()`, `TPeakStream.h:55-70`) follows R8, so one
  style remains.
- **R23 (low): an MP3 whose last frame is cut** is now an error at its tail,
  and every read there costs a seek with pre-roll (MAD counts the header,
  the decode fails).
- **R24 (low): FAAD's seek pre-roll assumes AAC-LC**: HE-AAC may decode
  wrongly after a seek; needs a test file, or document the limit.
- **R25 (low): `sf_strerror(nullptr)`** reads libsndfile's global last-open
  error (`SFAudioReader.cpp:46`): with readers opened at the same time on
  worker threads the message can belong to another file. Verify, with R8.
- **R26 (low): `TAudioReader::output_frame_count()`** at another rate can be
  one frame high for file rates that do not divide the universal rate (37.8,
  50 kHz): the length is rounded up by less than one unit. With the timebase
  task.
- **R27 (very low): recovering a damaged file** (what to do when the decoder
  keeps returning errors).

### Not ranked yet (Remon places them)

From the audiofileio API review (`doc/design/audiofileio-api-review.md`,
2026-10-27):
- **The API review's work order** (no rank: approved by Remon once Fable
  approves the document, built after midnight): W1 buffers (the scratch
  into the library, `TAudioBuffer` → `TChannelBuffer`, `TFileIOBuffer` →
  `TAudioBuffer`, the threading sentence), W2 the writer stops after a
  failed write, W3 the buffers' hygiene and small header cleanups, W4 the
  resampler choice as one typed value in `support`. I2 joined R14.

From the Fable review of the `audiofileio-errors` branch (2026-10-27, existing
code):
- **A recording on a full disk** keeps calling `rb_file_write()` per slot
  after a write error (`TBufferedAudioStreamWriter.cpp:158-162`): one log
  line per slot, and the stream does not stop (with R9).
- **`FaacAudioWriter::write()`** returns 0 after a failure mid-loop although
  earlier chunks were written (`:218-246`).
- **`WPAudioWriter::write_block()`**: a short `fwrite` without `ferror`
  leaves a stale `errno` in the detail (`:145-149`).

- **Discuss first:** `TAudioReader`'s resume after `read_file_frames()` (KISS:
  keep or plain reposition) and `set_output_rate_and_resampler()` repositioning
  with an index at the old rate.

Other open tasks:
- **Freewheel as its own task**: export and the bounce track run on it;
  bounded waits; close while freewheeling hangs (reader waits up to 1 s per
  stream per cycle, then a modal dialog in `TProject::disconnect_from_audio_device()`).
- **Split `TSheet::transport_control()`** into an audio-thread callback and a
  GUI function (removes 3 of the 7 Clang `-Wfunction-effects` reports).
- **Clip fader offset in the start cycle**: the gain curve is shifted by
  `offset` frames in a clip's first cycle (FIXME in `TAudioClip.cpp`).
- **Readahead and sample-exact clip edges** (reads more frames than processed
  at a clip edge; stream windows that do not hold the clip's window).
- **Recording data safety**: header length 0 until close; risk analysis and
  ROI first (`recording-data-safety.md`).
- **Zoom state into a controller** (branch 3); **PlayHeadMove as preview →
  commit**; **feedback for targeted items**; **waveform presentation logic to
  the model** (with the Qt Quick canvas); **waveform display modes**.
- **Internal routing and routing via JACK/PipeWire** (incl. JACK transport
  slave); **PipeWire as a graph client**; **JACK revision** (port lists as
  `TRtList`).
- **Export through the bounce track** (own branch).
- **Project file converter**: upgrade when the version is bumped; removes the
  default fades of older versions.
- **Gain ramp**: click-free mute, solo and fader (ramp state per transport).
- Post-merge leftovers of the peak work: remove the recording post-processing
  glue, check mapping a growing file on Windows and macOS, fix the sentence in
  `diskio-rounds.md` §5 **(check whether done)**.
- Small known bugs: `TViewPort::leaveEvent` FIXME (crash on Enter after a
  context menu); `TConfig::load_configuration` runs twice on first start;
  `TAudioDriver.cpp:64` shadows `sleepTime` (the subtraction on `:68` does
  nothing): fix with the test audio driver (work order step 8).

## Decisions waiting for Remon

- Scenario decoder O1: the wanted core behaviour per fault (after step 3).
- Branch 3: the questions in §10 of `input-controllers.md`.
- Shutdown: an explicit `shutdown()` or the order in the destructors (as
  built)? **(check: still open)**
- Flush-and-wait on the schedulers before saving a project: a requirement?
- Disk-load meter next to or instead of the CPU meter?
- Upgrade path for old projects after the timebase change: before a release.

## Parked / reference

- **Multi-transport** (`doc/design/multi-transport.md`, Ben's plan): reference
  only, not a leading design. Ask before a design choice that makes it
  significantly harder.
- **Gain ramp per transport**: the ramp state belongs to the transport
  (multi-transport §9.3), not to the track.
- **Multichannel playback**: clips with more than 2 channels read and draw,
  playback plays the first 2 (`7cc72041`); idea: a channel mapping onto mono
  or stereo.
- **Curve GPU benchmark** (`curve-gpu-bench`): kept as reference until the new
  canvas proves itself, then merge its docs and delete it.
- **Processing-node graph**: one root snapshot per batch, clips without
  automation not publishing; not now (load cost harmless).

## Documents

- **In the repo, `doc/design/`:** `diskio-app-wide.md`, `diskio-rounds.md`,
  `peak-records.md`, `waveform-grid.md`, `recording-latency.md`,
  `recording-session.md`, `recording-data-safety.md`, `multi-transport.md`,
  `curves-and-cursors.md`, `input-and-viewport-flow.md`,
  `readahead-investigation.md`; older peak notes (`peak-redesign.md`,
  `peak-diskio.md`, `peaks-v6-report.md`).
- **Next to the repository (`git/`), still to be brought in or retired (learning
  plan step 1):** `actions-and-controllers.md`, `design-principles.md`,
  `realtime-audio-path.md` (old copy; the repo has the current one), the
  scenario-decoder work orders, `fable-review-2026-10-25.md`,
  `fable-review-2-2026-10-25.md`, `leerplan-ontwerpproces.md`.
