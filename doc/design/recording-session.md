<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Recording sessions (design, 2026-10-19)

Decisions are the user's (2026-10-18); open points are marked.

## 1. Decisions

1. A recording session runs from the start of recording until the
   transport stops. It owns all its takes: one per armed track, and more
   when the recording continues elsewhere.
2. A seek during a recording ends the takes at the old location and starts
   new ones at the new location; recording goes on. (Before, the take went
   on growing at its old place, so the audio after the seek sat at the
   wrong time.) A rate change does the same, through the seek that
   follows every device restart (section 4).
3. The whole session is one undo step. The takes appear in their tracks as
   they are recorded; the step is pushed when the session ends, and its
   first redo does nothing.
4. Takes that can never come back are discarded with one routine, whatever
   the trigger:
   - the session's step leaves the undo stack while undone (a new edit
     dropped the redo branch),
   - the explicit action Edit > Discard Last Recording (the last step must
     be the recording),
   - the project closes with the step undone.
   The setting Recording > Discarded recordings chooses what happens to
   their files: ask (the default), delete, keep.
5. Only what the session created is deleted (its take clips, their audio
   sources, audio files and peak files), and only takes whose source no
   clip in the project uses.
6. A discarded take leaves the project at once, its files only when the
   project is saved without it. A project file saved before the undo still
   names the take; closing without saving keeps it and its audio together.
   (Older incremental backups still name such takes.)

## 2. Pieces

- `TSheet` keeps the session while it runs: the take clips and the steps
  that added them (`create_command_mutate_clips()`, applied without the
  undo stack). `prepare_recording()` starts the first takes,
  `seek_finished()` ends the running ones and starts new ones before the
  transport rolls again, `clip_finished_recording()` pushes the session's
  step once the last take is written.
- `TRecordingCommand` (QUndoCommand): the session's step. Undo and redo
  run the steps; when it is destroyed undone, it hands its takes to
  `TResourcesManager::discard_takes()` (queued: the stack is in the middle
  of a push then).
- `TResourcesManager::discard_takes()`: applies the setting. Deleting
  takes the clips and sources out of the project (they are not saved
  anymore) and keeps the objects until the project closes: a clip's reader
  leaves its DiskIO asynchronously, so freeing it at once could pull it
  from under DiskIO. `TProject::save()` deletes the audio and peak files
  once the project file is written.
- Asking: core does not show dialogs. `TProjectManager` holds a question
  function the application installs (a QMessageBox in `TMainWindow`; a
  Qt Quick UI installs its own). Without one the files are kept.
- At project close `TProjectManager` lets every sheet discard its undone
  sessions before the project is saved, so the project file does not keep
  them.

## 3. What does not change

Recording itself (writers, peaks, DiskIO), the take names, the files of
takes that are kept, and undo of everything else.

## 4. Rate changes

A change of the device's rate restarts the device, and
`TSheet::audiodevice_params_changed()` then seeks one buffer ahead so
DiskIO refills its buffers at the new rate. While recording, that seek
ends the takes and starts new ones (decision 2): their writers and peak
recordings take the device's rate when they start, so the new takes are
written at the new rate. Before, the take went on with audio at the new
rate in a file of the old one. Follows from the code; not tested at
runtime yet.

## 5. Open

- The explicit discard needs the recording to be the last step; a discard
  of an older session (with edits after it) is not offered.
- Runtime tests: a recording with a seek in it (one undo step, takes at
  both places), undo plus a new edit (the question), Discard Last
  Recording, closing the project with a recording undone, a rate change
  while recording.
