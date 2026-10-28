<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Renamed names

Names that changed, with the date, for older documents, commits, other
forks and agents: a name in an older text means what it meant then.

| Date | Before | Now | Note |
|---|---|---|---|
| 2026-10-28 | `TAudioBuffer` (one channel's samples, `src/dsp`) | `TChannelBuffer` | the name `TAudioBuffer` changed meaning |
| 2026-10-28 | `TRealTimeAudioBuffer` | `TRealTimeChannelBuffer` | |
| 2026-10-28 | `TFileIOBuffer` (`src/audiofileio`) | `TAudioBuffer` (`src/dsp`) | a list of `TChannelBuffer`s, one per channel |
| 2026-10-27 | task numbers R*n* (the rank, renumbered on insertion) | fixed task IDs T*n* | same number on that day; `doc/status.md` |
