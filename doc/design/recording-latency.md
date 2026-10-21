<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Latency compensation for recordings (plan, 2026-10-19)

Task of the user, 2026-10-19: compensate recordings for latency.

## 1. The problem

A take is placed at the transport position where recording started, and
every frame the audio thread captures goes to the timeline position the
transport has in that cycle. But a performer plays to what they hear, and
what they hear comes out late; what they play goes in late too:

- cycle k processes transport position P;
- its output leaves the device L_out later (playback latency: the queued
  periods plus what the driver measures, `get_playback_latency_frames()`);
- the performer plays the note that belongs to P when they hear P;
- the input buffer of a cycle holds what arrived L_in before the cycle
  started (capture latency: one period plus what the driver measures).

So the sample captured while the transport is at Q belongs to the timeline
at Q − (L_in + L_out). Today every take sits L_in + L_out late: 3 periods
or more (with 256 frames at 48 kHz, ALSA: about 16 ms plus the
converters), audible against the material it was played to.

## 2. The compensation

A take is shifted earlier by L = L_in + L_out (+ the user's extra
latency, section 4), by dropping its first L frames (the user's choice,
2026-10-19):

- The clip stays at the position where recording started; its writer does
  not write the first L frames, audio and peaks alike. They came in before
  anything could be heard. The file starts with the frame that belongs to
  the take's position: the file is what the clip shows, nothing is hidden,
  and the file lines up in any other program too.
- The drop happens in the writer's step when a slot is written
  (`TBufferedAudioStreamWriter::rb_file_write()`): whole slots are skipped,
  in the slot the drop ends in the rest moves to the front (once per take).
  The audio thread is not touched. A counter that goes to zero; checking
  it costs one comparison per slot afterwards. (Should more of these
  per-slot requests come, a list of pending requests keeps it O(1).)
- The clip's length during the recording counts the frames past the first
  L, the same count the writer keeps, at the device's rate (the file's), so
  clip and file stay the same length; after the recording it comes from
  the file as before.
- At the end, the last L frames of what was played arrive after the
  transport stopped, and are not recorded: a few milliseconds after the
  end of what the performer played to. Ardour does the same.

Considered and not taken: keeping all frames and giving the clip a source
start of L (a hidden offset; the frames before the punch-in could be
revealed by trimming, but the clip would carry an exception in its length
and in the recording view). A wrong L is corrected the same way in both:
by moving the clip.

No negative positions (recording from 0 works), no change in the file
format or the peak files.

A take shorter than L has an empty file. Its clip has no readable source
and a length of 0, so it is not visible, yet it sits in the track, in the
undo step and in the project file. Dropping such takes belongs to the
recording session (branch `recording-session`).

Not compensated:
- takes from a software input (the bounce track records the master in the
  same cycle; an internal bus is aligned already): L is 0;
- the latency of plugins on the tracks heard while recording (Traverso has
  no plugin delay compensation).

## 3. What each driver reports

`TAudioDriver` gets `get_capture_latency_frames()`, next to the existing
`get_playback_latency_frames()`; `TAudioDevice` passes both on, and
`TRecordingLatency::compensation_frames()` adds them.

| Driver | Capture latency | Source |
|---|---|---|
| ALSA | one period | the input of a cycle is the period captured before it |
| JACK | the input port's capture latency range (max) | `jack_port_get_latency_range(JackCaptureLatency)` in the latency callback, like the playback side |
| PipeWire | what the capture stream reports (delay + queued + buffered), plus the cycles the buffer waited | the capture and playback streams are two graph nodes in no fixed order: when the capture node runs after the playback node, a cycle processes the buffer captured a cycle earlier. The graph times (`pw_time.ticks`) of the two callbacks give that cycle. Whether `delay` counts from the first or the last frame of the quantum is not verified (the loopback test tells) |
| PortAudio | the stream's input latency | `Pa_GetStreamInfo()->inputLatency`; one period when the host API reports none |
| CoreAudio | one period | device and safety-offset properties not read yet (no Mac to test) |
| Dummy | one period | captures silence |

None of these knows the converters' latency (about 0.5–2 ms) or external
gear: that is the extra latency of section 4.

## 4. Settings

Recording settings page:
- Latency compensation: on (default) / off. Off places takes as today
  (for e.g. aligning to an external clock).
- Extra latency in ms (default 0, may be negative), added to what the
  driver reports. Measured with a loopback: play a click out, record it
  back in, and adjust until the recorded click lines up.
- The latency the driver reports now, as a read-only line (capture,
  playback, total).

The compensation is fixed when a take starts (with the branch
`recording-session` also each take a seek during the recording starts).

## 5. Steps on branch `recording-latency`

1. `get_capture_latency_frames()` per driver and on the device; the
   latency shown in the info widget becomes the whole round trip.
2. A pure function for the compensation (driver latencies, extra
   milliseconds, rate, hardware input, on/off → frames), with a test.
3. `TAudioClip::init_recording()` gives the writer L to drop;
   `process_capture()` lengthens the clip only past the first L frames.
4. The settings.
5. Tests: the pure functions (the compensation, the frames a slot drops,
   the clip length equal to the file's); the app headless. The real check needs
   the user: a loopback recording against a click (section 6).

Status 2026-10-19 (branch `recording-latency`, on master 9c8e3fa2): steps
1–4 done, reworked to dropping frames the same day. Tested: the
compensation, the drop per slot and the clip length against the file as
pure functions (ctest `recording_latency`), `TTimeRef::from_milliseconds()` (ctest
`timeref`), builds with GCC and Clang, the app starts headless. Not tested:
a real recording (no loopback here); PortAudio and JACK capture latency
(no such device here). The branch touches the recording settings page,
like `recording-session`: a small merge conflict there.

Review (Fable, 2026-10-19 evening), fixed:
- the PipeWire cycle order;
- the PortAudio fallback when a host API reports no latency;
- JACK's capture latency read at start;
- the recording length at the device rate (with a project at 44.1 kHz and
  a device at 48 kHz it drifted 8.8% against the file during the
  recording);
- the latency line on the settings page follows the driver;
- comments and this document.

Left open (from before this branch):
- JACK: Traverso does not set the latency of its own ports in the latency
  callback, so other JACK clients see Traverso as having none.
- Commit 3935f703's message describes the source-start design that
  bcd744d9 replaced. 2b4219df calls `get_roundtrip_latency()` unused, but
  master used it for the info widget, which the branch gives the whole
  round trip in its place.

## 6. Runtime test for the user

Connect an output to an input (cable, or the interface's loopback). Put a
click (a short sharp sample) on a track, arm a second track on that input,
record. Without compensation the recorded click is L late; with it, it
should sit on the original within about 1 ms (the converters). Adjust the
extra latency until it does; that number belongs to the interface.
