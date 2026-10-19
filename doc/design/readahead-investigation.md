<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Readahead and clip edges (investigation, 2026-10-19)

Task of the user, 2026-10-19: why `TAudioClip::process()` reads more frames
than it processes at a clip edge ("read 1024 frames of the 495 to
process"), whether clip boundaries are sample-exact, and why readers report
In Sync with the transport far away. Analysis only, no code changed.

Everything below follows from reading the code on master 9c8e3fa2. None of
it is verified at runtime yet. Section 6 has the check.

## 1. How a reader follows the transport

- Every clip has its own `TBufferedAudioStreamReader` with a queue of slots
  of one device buffer each. There are `rate / buffer size` slots, so the
  queue holds one second.
- The stream window is the clip's start and end on the timeline
  (`TAudioClip::sync_location_to_stream()`), plus a random readahead of 1 to
  4 seconds before the start, chosen once per reader to spread the disk
  load.
- At a seek, every reader runs `rb_seek_to_transport_location()`:
  - A transport that is outside the window gives OUT_OF_WINDOW.
  - A transport inside the clip gives a seek to that position.
  - A transport before the clip, within the readahead, gives a seek to the
    clip's start. The first slot is then aligned to the transport's buffer
    grid: it starts `offset` frames before the clip and is written from frame
    `offset` on. Then 70% of the slots are filled.
- During playback, a reader below 80% gets its next slots, contiguous with
  the last one. OUT_OF_WINDOW counts as out of sync, so while any reader is
  out of its window, every audio cycle starts a DiskIO round over all
  streams. In that round each such reader runs the seek again: that is how
  it notices the transport has come close.
- The audio thread takes the slot whose file position equals the cycle's file
  position exactly, and copies the whole slot into the clip bus.

The exact match only works if every cycle processes exactly one buffer
size. That holds for ALSA and JACK, and for PipeWire, because the driver
forces and locks the quantum (`node.force-quantum`, `node.lock-quantum`).

## 2. Findings

**F1. Clip start: up to one buffer of stale audio before the clip.**
- The first slot after a readahead seek is written from frame `offset` on.
  Its frames `[0, offset)` are not written: `TQueueBufferSlot::silence_buffers()`
  is never called.
- `ringbuffer_read()` copies the whole slot into the bus, and the clip mixes
  the whole bus. So the `offset` frames before the clip start carry whatever
  that slot held from its previous use: other audio of the same file.
- On the first play after loading, the slots are new and silent. After that
  this is likely every time playback runs into a clip from before its
  start: up to one buffer (21 ms at 1024 frames) of wrong audio just before
  the clip.
- A fade-in does not hide it: `TFadeCurve::process()` only touches
  `[offset, …)`.

**F2. Clip end: up to one buffer past the end.**
- `TAudioClip::process()` computes `framesToProcess` for a clip that ends
  inside the buffer, but `ringbuffer_read()` returns the slot's full size,
  and the whole bus is mixed.
- The frames after the clip's end are played. Their content depends on how
  the clip ends:
  - Trimmed on the right: the file's audio after the trim point, at the
    clip's gain. A fade-out does not cover it either, because the fade stops
    at the clip's end. So after a fade to zero comes a short burst at full
    level.
  - Ending at the file's end: silence, because `AbstractAudioReader::read()`
    zeroes the decode buffer first.
- At a split point both halves play the right half's first frames, so that
  part is doubled (+6 dB). On top of that, the right half's start has F1.
  This is likely a click at every split, in playback and in an export alike
  (freewheeling takes the same path).

**F3. Clip gain on the wrong frames in the first buffer.**
- The fader gets `readFrames` frames from frame 0: `apply_gain_to_buffers(readFrames)`.
  The FIXME in the code says so: "offset is no longer given to fader".
- At the clip start that is `[0, buffer − offset)`, so the last `offset`
  frames of the clip's own audio get no clip gain. With gain automation, the
  curve is also read from a position `offset` frames before the clip.
- This is only audible with a clip gain other than 1.

**F4. The warning.** "read 1024 frames of the 495 to process" is F2. It
comes at nearly every clip end, because a clip end almost never falls on a
buffer boundary. At a clip start the counts happen to agree: the first slot
reports `buffer − offset`.

**F5. In Sync with the transport far away.**
- A reader's window is only tested at a seek. A reader whose clip the
  transport has passed keeps its queue and its IN_SYNC status until the next
  seek: nothing takes slots from it anymore.
- So "In Sync" means only "the queue is full". A reader before its clip,
  inside the readahead, is In Sync too, and correctly so: its queue waits at
  the clip's start.
- This is harmless. The cost is up to one second of reading and decoding
  past every clip end, which is never played.

**F6. Getting back into the window depends on the per-cycle round.**
- An OUT_OF_WINDOW reader only notices the transport coming close because it
  makes every cycle a DiskIO round (section 1). That costs one
  `blockingMap` over all streams per audio cycle, as long as any clip lies
  ahead.
- Consequence for the DiskIO rounds task of tonight
  (`diskio-uniforme-taken-begrepen.md` §1): there an OUT_OF_WINDOW reader
  has `needs_work() == false`. Taken literally, it would never get a step
  again, and a clip ahead of the transport would stay silent.
- The fix belongs in that task: `needs_work()` of a reader out of its window
  becomes "the transport has entered my window". That is a comparison
  without disk access, the same test `rb_seek_to_transport_location()` does
  now. It still does not count for the idle work's lowest fill. Behaviour
  stays as today.

**F7. Data races on positions.**
- The audio thread writes `TDiskIOThread::m_transportLocation` every cycle,
  and DiskIO reads it.
- The GUI thread writes the reader's window (`m_streamStart`/`m_streamEnd`)
  and source start, and the DiskIO pool threads read them.
- All of these are plain `TTimeRef` values (one `qint64`). On x86-64 the
  values do not tear in practice, but it is a data race: a `TRAVERSO_TSAN`
  build reports it. Atomics (the universal frame as `std::atomic<qint64>`)
  close it.

**F8. Moving or trimming a clip while playing.**
- The queued slots keep the old file positions, so the exact match fails.
- The reader then goes OUT_OF_SYNC and DiskIO seeks it again: a short
  dropout, no wrong audio. This is acceptable; it is noted here, not proposed
  for change.

## 3. Are clip edges sample-exact?

No:
- before the start, up to one buffer of stale audio (F1);
- after the end, up to one buffer too much (F2);
- in the first buffer, the clip gain on the wrong frames (F3).

The positions themselves are right: the alignment in section 1 holds. With
fix A the edges become exact, to the rounding of a timeline position to an
output frame (`TTimeRef::to_frame()`).

## 4. Fixes

| | What | Covers | Size | Behaviour |
|---|---|---|---|---|
| A | In `TAudioClip::process()`: silence the bus outside `[offset, offset + framesToProcess)`; give the fader that range, starting at file position `fileLocation + offset` (as `TFadeCurve` does with `set_data_start_offset()`) | F1–F4 | ~15 lines, plus a pure helper for the range with a test | changes what you hear (fixes it): needs approval |
| B | Silence a slot's `[0, offset)` in the seek | F1 only | 1 line | not needed with A |
| C | Stop reading past the clip's source end: past it, slots get silence without a read | F5's cost | small; the reader needs the source end | no |
| D | `needs_work()` of an OUT_OF_WINDOW reader: the transport entered its window | F6 | part of tonight's rounds task | no (same as today) |
| E | Atomic transport position, window and source start | F7 | small | no |

About A:
- The test: core cannot be linked into the tests, so the arithmetic (the
  frame range of a clip within a cycle) goes into a pure function, tested
  at both edges, inside a buffer, and for a clip shorter than a buffer.
- `ringbuffer_read()` keeps its interface. Its return value then means "the
  slot was found", and the clip uses its own range.

## 5. Recommendation

1. Do D inside tonight's DiskIO rounds task. Without it, that task breaks
   playback of clips ahead of the transport.
2. A and E on a small branch `clip-edges`, after the user's approval of A
   (behaviour change).
3. B is not needed once A is in. C can wait: its cost is small.

## 6. Check for the user (before and after A)

Use a temporary copy of a project:
- A clip of a sine, trimmed on the right, with a fade-out, starting at a
  position that does not fall on a buffer boundary. Play into it from a few
  seconds before, twice. Before A: a burst after the fade, possibly a tick
  before the start on the second play. After A: clean.
- The same sine, split in the middle. Play across the split. Before A: a
  click at the split. After A: nothing to hear.
- The same with an export (freewheeling), inspected in an editor: the
  frames before the clip's start and after its end must be exactly zero.
