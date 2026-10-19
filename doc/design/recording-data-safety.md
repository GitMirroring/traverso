<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Safety of recorded audio: risks and ROI (analysis, 2026-10-19)

Task of the user, 2026-10-19: peak files are a cache, recorded audio is
data. Before any fsync or similar measure, a risk analysis with costs and
benefits. Analysis only, no code changed. Code facts are from master
6954a214 and the branch `recording-session`. The libsndfile facts are
measured with libsndfile 1.2.2 (section 2).

## 1. How a take reaches the disk today

- The audio thread copies every cycle into a free slot of the take's
  writer. The writer has `rate / buffer size` slots: one second.
- The write DiskIO thread is woken every cycle. It writes the queued slots
  with libsndfile: WAV, 32-bit float by default (W64 and WavPack are
  settings).
- libsndfile writes the header at open, with a data length of 0, and the
  real length at `sf_close()`, after the transport stops. Nothing calls
  fsync. The kernel writes dirty pages back after about 30 s
  (`dirty_expire_centisecs` 3000, writeback every 5 s).
- The project file is saved (autosave) once every take of the recording is
  closed (`TSheet::clip_finished_recording()`). Before that the project
  file does not know the takes. The project save itself is atomic since
  9c8e3fa2.
- With the branch `recording-session`, undone takes are deleted with
  `QFile::remove()` once the project is saved without them, when the
  setting says delete or the user answers yes.

## 2. Measured: what a crash leaves behind

A child process wrote 10 s of stereo float and exited without `sf_close()`
(an application crash; the kernel still has the data). Afterwards, the
frames each reader sees:

| File | libsndfile (Traverso, Ardour, Audacity) | ffmpeg | sox |
|---|---|---|---|
| WAV, header never updated | all (479232) | all | 0 |
| WAV, header updated every ~1 s | up to the last update (433152) | up to the last update | up to the last update |
| W64 | all | cannot open | — |
| RF64 | 0 | all | 0 |

- libsndfile and ffmpeg take a WAV data length of 0 as "up to the end of
  the file". So the crashed WAV is already recoverable as it is.
- Updating the header during the recording (`SFC_UPDATE_HEADER_NOW`)
  makes recovery worse: every reader then trusts the last length written,
  and what came after it is lost. The call itself is cheap (about 1.8 µs a
  block).
- Opening the crashed WAV read-write, updating the header and closing it
  writes the right length. After that sox reads all frames too.
- A WAV past 4 GiB of data (8-bit mono, 4.4 G frames): libsndfile writes
  it and closes without an error. On reading, it gives 4 GiB of data and
  stops there; ffmpeg reads all of it. For 32-bit float at 48 kHz, that
  limit is 3.1 hours stereo and 6.2 hours mono.

## 3. Risks

Likelihood and impact are estimates; "today" means master.

**R1. Application crash during a recording.**
- Likelihood: medium (development builds, asserts).
- Impact:
  - The takes of the recording are not in the project file. Their files
    are on disk, readable (section 2), minus the last second that was
    still in the ring.
  - The user does not see them, and their position on the timeline is lost.
- Detection: none. They have to be found in the audio sources directory
  and imported by hand.
- Also crash between stop and autosave: the same, with the header complete.

**R2. Power loss or a USB disk pulled during or shortly after a recording.**
- Likelihood: low for a desktop. Higher with USB disks and laptops: "stopped,
  pulled the drive".
- Impact: the last ~35 s of every take that the kernel had not written
  back, plus R1.
- Detection: none.

**R3. A disk stall longer than the write ring (1 s).**
- Likelihood: medium on HDD, USB and network disks (spin-up, journal
  commits, other programs), low on SSD.
- Impact:
  - The audio thread finds no free slot and drops the buffer. Only the log
    shows it.
  - The clip and the file skip it alike. So all audio after the dropout
    sits earlier on the timeline by the length of the dropout, silently.
    For multitrack takes, the tracks drift apart.
- Detection: the log only.

**R4. Disk full during a recording.**
- Likelihood: low to medium (long sessions, small disks).
- Impact:
  - The write fails and the recording goes on: the clip grows, the file
    does not.
  - After the stop the take is shorter than what was played, and
    everything after the full disk is lost.
- Detection: the log only. There is no free-space check at the start.

**R5. A take longer than 4 GiB in WAV.**
- Likelihood: low (live recordings, long sessions).
- Impact: Traverso reads only the first 4 GiB of it (section 2): stereo
  float past 3.1 h is cut off when the project is loaded again.
- Detection: none.

**R6. Deleting discarded takes (branch `recording-session`).**
- Likelihood: low (only takes of the session, only after a save, asking is
  the default).
- Impact: permanent. A bug or a wrong answer cannot be undone.

**R7. The project file.** Atomic since 9c8e3fa2: old or new, never half.
No further risk found.

## 4. Options, cost and benefit

| | Option | Against | Cost | Benefit | ROI |
|---|---|---|---|---|---|
| A | Writer ring of several seconds for recording (e.g. 8 s; 1.5 MB per mono float track, 48 MB for 32 tracks) | R3 | a constant for writers | stalls up to 8 s cost nothing | high |
| B | A dropped buffer is written as silence: the audio thread counts the frames it could not queue, the write DiskIO writes that many zeros first; the clip counts them too | R3 | ~20 lines, a pure helper with a test | the take stays aligned; a gap of silence instead of a shift | high |
| C | Report dropouts and write errors to the user (posted to the GUI, a message after the stop) | R3, R4 | small | the user knows the take has a hole | high |
| D | Save the project when the recording starts, with the takes in it at length 0. On a load after a crash, a length of 0 already means "the length of the source", and libsndfile reads the crashed WAV up to its end (section 2) | R1 | a save at the start (atomic, no fsync; time on a large project to be measured); a runtime test with kill -9 | after a crash the takes are in the project, at their place | high |
| E | `QFile::moveToTrash()` instead of `QFile::remove()` for discarded takes; when there is no trash (some external disks), keep the files | R6 | a few lines | a wrong delete can be undone | high |
| F | Free space at the start: warn when the armed tracks fill the disk within e.g. 15 minutes (`QStorageInfo`); stop the take on a write error (with C) | R4 | small | no silent loss on a full disk | medium–high |
| G | fdatasync of each take after `sf_close()`, on the write DiskIO, before the autosave | R2 after the stop | a few lines; 10–100 ms per take on an HDD, not on the audio thread | "stopped" means "on the disk"; covers the pulled USB disk | medium |
| H | fdatasync every ~5 s during the recording | R2 during | small, but each sync stalls the write thread: needs A | loses at most ~5 s on a power loss | low–medium |
| I | A hint for long recordings: W64 (libsndfile recovers it after a crash too), or warn when a take nears 4 GiB | R5 | small | no truncated long takes | low–medium |
| J | Repair the header of a crashed WAV on load (read-write open, update, close) | other programs after R1 | small, but it writes into audio on load | sox and similar read the take | low |
| — | Update the header during the recording | R1 | — | measured: makes recovery worse | none: do not do |
| — | RF64 | R5 | — | a crashed RF64 is unreadable for libsndfile | none: do not do |

## 5. Recommendation

1. **A + B + C** as one branch `recording-dropouts`. This is the most
   likely loss, it is silent today, and the fix is cheap. B and C change
   behaviour (silence instead of a shift, a message), so they need approval.
2. **D**, after measuring how long a save takes on a large project, with a
   runtime test: kill -9 during a recording, reopen the project, and check
   that the takes are there at their place and complete up to the last
   second.
3. **E** on `recording-session`, before it is merged: it is part of the
   delete it introduces.
4. **F** and **G** together on a small branch. G is the only fsync this
   analysis recommends: once per take, at the end, off the audio thread.
5. **I** as a hint in the settings. H and J only if the user wants them.

Do not do periodic header updates or RF64 (section 2).

## 6. Tests

- Headless:
  - The dropout accounting of B as a pure helper: frames dropped, zeros
    written, clip length equal to the file's.
  - A ctest that keeps the assumption of D true: libsndfile reads a WAV
    whose header says 0 up to the end of the file (as measured in section
    2), so a newer libsndfile that changes this is noticed.
- For the user:
  - D: kill -9 during a recording.
  - A/B: a recording to a slow USB disk, with a stall forced by `ionice` or
    by copying a large file to the same disk; the takes must stay aligned.
  - F: a recording to a nearly full disk (a small loop device).
