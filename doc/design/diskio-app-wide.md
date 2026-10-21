<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# One DiskIO for the application (design, 2026-10-21)

A design for after the peak redesign (branch `peak-records`), worked out
with the user on 2026-10-21 under the KISS principle. Nothing of it is built
yet. It is a work order of its own; importing and merging through DiskIO is
another one, right after it.

## 1. Why

Today:
- every sheet has two DiskIO threads (one reads, one writes);
- importing and merging have threads of their own (`TAudioFileCopyConvert`,
  `TAudioFileMerger`, both a `QThread`);
- every DiskIO runs its rounds on the one global thread pool
  (`QtConcurrent::blockingMap`).

The threads that do the work are shared, but nobody decides which stream
comes first across them: every DiskIO plans only its own streams. One
DiskIO for the whole application, one thread that reads and one that
writes, lets one planner decide. Recorded audio still never waits behind a
slow read: writing keeps a thread of its own.

## 2. The concept: an album of songs

A project is an album and its sheets are songs. Who plays, and in which
time, is a choice of the project:

| Mode | What plays | Each song's time |
|---|---|---|
| Song | the active song | its own |
| Album | every song, one after another | album time minus the song's offset |
| All at once | every song | the same position for all |
| Independent | the songs that were started | each its own transport |

DiskIO knows none of these modes. They are the project's policy: it sets
the transports of the songs.

## 3. A group per song

A song is a group in DiskIO: its transport location, its seek (request and
location), whether it rolls, and later its play list (section 5). This is
the state every sheet has today, moved out of its own DiskIO threads into
the shared one. A stream belongs to a group.

A round, in this order:
1. **Refills of the streams of rolling groups**, the emptiest first.
2. **Seeks**, per group: a seek reads a lot at once, so on a hard disk it
   waits behind the refills of the groups that are audible.
3. **Idle work**, behind the hysteresis (only the readers of rolling groups
   count) and off while any group records:
   - the readers of parked songs (section 4);
   - peak post-processing and peak builds;
   - imports and merges (the next work order).

`TDiskIOPlanner` gets two flags per stream: it seeks, and it is parked. It
stays pure and tested headless.

The audio thread wakes DiskIO once per cycle, from the project, not once
per sheet.

## 4. Parked songs

A song that does not play keeps its readers full at its own position, as
idle work. Once its buffers are full it asks for nothing, as nobody
consumes. Switching to another song is instant, as it is today with a
DiskIO per sheet: the new song's readers are ready where it stood, and the
project drives that song's transport from there.

## 5. Play lists: loops and regions

Later, for playback only: a loop range, or jumps from one region to the next
(a composer compares bars 3–5 and 9–12). A play list is an ordered list of
time ranges, with a loop over the list or not. It belongs to the group,
published as an immutable snapshot like a curve (`TRtValue`). All clips
inside the ranges play. A loop range is a play list of one range.

The reader changes in two places only, and DiskIO not at all:
- **The next slot.** Every slot already holds its transport and file
  location; the next one is now "the last plus one slot"
  (`TBufferedAudioStreamReader::process_realtime_buffers()`). It becomes "what
  the play list has after the last". A slot holds frames in play order, so
  across a jump the ring already has the next audible second: no seek, no
  gap. A slot across a jump is filled from two reads.
- **The window.** `transport_in_window()` measures the distance in linear
  time now ("about one to four seconds"). It becomes the distance along the
  play order, so a clip in bars 9–12 prefills when the transport nears bar 5.

What else it needs:
- the audio cycle splits at a jump for what depends on time: curves and
  automation, fades, the playhead, the time information plugins get;
- a fade of a few milliseconds at a jump against clicks, by the reader,
  which knows where the jump is;
- a play list that changes while it plays (a loop end dragged) is a seek of
  that group to where it is;
- a minimum length of a range, at least an audio buffer;
- bars need a tempo map, which Traverso does not have yet; the play list
  itself is time ranges.

## 6. Punch in/out and loop recording

The same idea for recording, later. Playback runs on, and recording happens
only inside the punch ranges. The audio thread knows the transport and
starts and stops at the edges, to the sample. The writer writes what it
gets; with a loop, a new take per pass. DiskIO needs nothing for it, so
this design does not stand in its way.

## 7. JACK transport

JACK has one transport. Which groups it drives is project policy: the active
song (the likely use, a JACK session is a song) or all of them. Today
`TProject::transport_control()` passes it to every sheet.

## 8. Where it lives

A library `traversodiskio`: the DiskIO thread, `TDiskIOPlanner`,
`TDiskIOTask` and the stream interface, linking `traversosupport` and
`traversoaudiofileio`. The readers and writers in core implement the stream
interface.

It does not go into `audioruntime`, which is the real-time side (drivers,
device, buses, RT primitives) and would then depend on decoding files.

## 9. Disadvantages, from a DAW's point of view

1. **Memory.** A reader holds a ring of one second, about 384 KB of stereo
   at 48 kHz. A song of 175 clips is about 67 MB, and an album of twelve such
   songs about 800 MB. That is so today too: every sheet prepares the rings
   of all its clips. Later options:
   - a parked song gives its rings up, and a switch costs a seek;
   - a reader out of its window makes its ring only when the transport
     comes near.
2. **A seek beside playback** (independent mode): on a hard disk it competes
   with the refills of the song that is audible. The order of section 3
   protects that song, but two songs at once stay tight on a hard disk.
3. **Bandwidth:** more rolling songs, more streams. In album mode one song
   plays at a time but for the transitions.
4. **Gapless transitions and crossfades between songs:** groups with offsets
   allow overlap. Whether a transition has no gap depends on how far ahead
   the readers read, the open point "clip-edges fix A"
   (doc/design/readahead-investigation.md).
5. **Alignment to the sample** in album and all-at-once mode: every group
   moves on in the same audio cycle, so they stay aligned as long as the
   project starts them in the same cycle.
6. **Independent mode mixes the songs into the master:** two songs at once
   are heard together. There is a precedent: Reaper's "play background
   projects". It is a niche.
7. **The current fill meter and DiskIO CPU time per sheet** become one
   number for the application.

## 10. Steps

1. The library `traversodiskio` and the stream interface: moving code, no
   behaviour change.
2. One DiskIO pair owned by the application, a group per sheet with each
   sheet's transport as today (song mode, and independent mode as it is
   today). The priority across all sheets is one planner's; the fill meter
   is one number.
3. Parked groups as idle work.
4. Importing and merging as DiskIO tasks, with their peaks in the same step
   (the next work order).
5. Play lists, and later punch in/out.
6. Album mode and the project's mode policy.

Each step with headless tests on the planner (the order of section 3, the
flags) and the reader's play-order functions (pure).

## 11. Open

- Its own thread pool for DiskIO, or the global one: with its own pool,
  priority is DiskIO's alone and the global pool stays for the cache's
  eviction and the like.
- Does starting another song stop the one that plays (no independent mode
  by default), or does independent mode stay as it is today?
- What JACK drives by default: the active song.
