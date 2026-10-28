<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# audiofileio: API review

Review of the public API of `traversoaudiofileio` (`TAudioReader.h`,
`TAudioWriter.h`, `TAudioFileError.h`, `TAudioEncodeSettings.h`,
`TFileIOBuffer.h`), 2026-10-27, on branch `audiofileio-errors` (after task
T9). By Fable, discussed with Remon. Findings become tasks in
`doc/status.md`; decisions are recorded here.

## Strong

1. A reader or writer exists only for a file it can use: private
   constructors, `open()` returns `std::expected`, so every getter is valid by
   construction.
2. One buffer contract, enforced in one place (`TAudioDecoder.h`, kept by
   `seek_landed()`/`seek_failed()`/`read_done()`): every frame of every
   channel filled, zeros past the file, short only at the end or on an
   error.
3. Failure is in the type, the same shape from the decoders and encoders to
   the reader and writer; dropping it is visible (`std::ignore`).
4. Time in, frames out, one rounding rule, stated and proven
   (`test_resample_reader`); all conversions through `TTimeRef`.
5. A failure leaves things as they were (the rate on a failed resampler; no
   empty file when the writer's resampler does not start; a second
   `close()` is no error).
6. Headless testable through the public headers only (scenario files);
   decoders and encoders are private.
7. "One reader per use" holds (`TResourcesManager::get_readsource()`
   deep-copies).

## Weak and smells

1. `TFileIOBuffer` shows the library's scratch space: the interleaved buffer
   and the packed `void*` are public but used only inside the library
   (`SFAudioReader::read()`, `TAudioWriter`); the caller's buffer is the
   writer's scratch, so `write()` takes it non-const; the packed buffer is
   memset on every non-float write.
2. `TAudioEncodeSettings`: a dead field (`blockSize`), and pairs that must
   agree without a check (`writerType`/`fileFormat`; the resampler backend
   and its quality, also loose in `set_output_rate_and_resampler()` and as
   raw integers in `TIOStream::Resampler`).
3. Hidden state and call order in the reader (two read modes on one object,
   three position states over two classes; `read()` needs nine comment
   lines). Judged cohesive for its one consumer: splitting would duplicate
   the seek logic.
4. Three ways to say "the end" in two units (`length()`,
   `file_frame_count()`, `output_frame_count()`, the last used by tests
   only); the writer's count is input frames, the stream stores it as the
   file's.
5. Error codes stretched: `CannotOpen` for a resampler at a rate change and
   for "no input rate"; a write after `close()` is `WriteFailed` (detect,
   not prevent); the count on a write error has two meanings.
6. "After an error the caller stops writing" is comment only: the writer
   accepts more writes, the stream keeps calling it (one log line per slot
   on a full disk).
7. Threading and ownership only in comments ("not for the audio thread";
   nothing says one thread at a time; the scheduler serializes the rate
   change, the library relies on it without saying so).
8. Public headers pull in `dsp` headers (`TAudioResampler.h`,
   `gdither_types.h`, `TAudioBuffer.h`); consumers link `dsp` themselves
   (consistent with the PRIVATE rule). `TAudioWriter.h` includes `<vector>`
   unused.
9. `TFileIOBuffer` hygiene: `.at()` (throws), `uint` where `nframes_t` is
   meant, a pre-allocated stereo footprint, no `channel_count()`;
   `TAudioBuffer` can be copied (raw pointer, destructor): a double free.
10. `TAudioWriter.h`: "a channel the buffer lacks is silence" holds only for
    a channel the buffer never had.
11. `open()` is the only way to ask a file's facts (the import constructor
    opens a reader for the channel count alone).
12. `.value_or(nullptr)` in tests and tools drops the reason.

## Improvements (Fable's ranking)

| # | Improvement | Cost | KISS |
|---|---|---|---|
| I1 | `TFileIOBuffer` channel buffers only; the library owns its scratch | small–medium | yes |
| I2 | One format field in `TAudioEncodeSettings` (M4A in `FileFormat`, the encoder chosen by it), no `writerType`, no `blockSize` | medium | yes |
| I3 | The writer stops after a failed write (later writes give the first error, `close()` completes the file); the count on an error always 0 | small | yes |
| I4 | The threading contract in one sentence per class | trivial | yes |
| I5 | `TFileIOBuffer` hygiene; `TAudioBuffer` not copyable | small | yes |
| I6 | The resampler choice as one value through dsp, ioscheduler and core | medium | borderline |
| I7 | Small header cleanups (`<vector>`, `output_frame_count()` private) | trivial | with I1/I5 |

## Decisions (Remon, 2026-10-27)

**I1 and the buffer names**, as one task in three steps:

1. **The scratch out of `TFileIOBuffer`, into audiofileio.** Sharing one
   piece of memory per thread was why it lived in the caller's buffer: the
   thread serializes the access, and a buffer per reader would cost too much
   memory (one reader per clip stream, open for the whole project). The
   sharing stays, inside the library:
   - `SFAudioReader` (and `WPAudioReader`, which allocates a vector per read
     today): a `static thread_local` scratch behind a `static` helper in the
     `.cpp`, used within one call only. The contract is enforced by its
     shape: per thread by `thread_local` (no concurrent use); not kept
     between calls (no member, nothing in a header, only reachable inside
     the function); no nesting on a thread (`sf_readf_float()` calls
     nothing of ours back). Not on the audio thread: `read()` is not
     `T_REALTIME_SAFE`, Clang reports such a call from a marked function.
     (A debug guard against nesting was proposed and dropped after Fable's
     work order review: the shape already excludes it; detecting it would
     go against "prevent, do not detect".)
   - `TAudioWriter`: its own members (few writers); `write()` takes the
     caller's buffer `const`.
   - `TAudioReader::m_decodeBuffer` (the resampled path, per reader) loses
     its interleaved part with it.
2. **`TAudioBuffer` → `TChannelBuffer`** (and `TRealTimeAudioBuffer` →
   `TRealTimeChannelBuffer`): the samples of one channel, the building block
   of the real-time path. Mechanical, about 26 files.
3. **`TFileIOBuffer` → `TAudioBuffer`**, in `dsp` next to `TChannelBuffer`:
   a list of `TChannelBuffer`s, one per channel, owning them, growing in one
   place, with the stable pointer array the resampler takes. Mechanical,
   about 40 files.

Step 2 before step 3, so no commit has a `TAudioBuffer` of both meanings;
the compiler catches every mix-up (the types differ). The commit messages
say that the name changed meaning.

Considered for the names:
- Merging the one-channel buffer and the list into one multichannel type
  (as JUCE's `AudioBuffer`): no. The one-channel buffer is used on the audio
  thread and does not allocate there; the list grows and allocates and
  lives on the IO threads only. One type would bring growing into the
  real-time type and touch the whole real-time path.
- `TSampleBuffer`: by the project's own naming rule a sample is a channel's
  value, but in a DAW "a sample" is also a sound snippet: ambiguous.
- "Port" (JACK, LV2, CLAP): a port is a connection point of a node in a
  graph, with one channel (JACK) or several (CLAP, VST3); a file has
  channels, no ports. "Port" stays for the routing and the processing
  graph to come; a port's buffers are `TChannelBuffer`s.
- `TAudioBufferList` (the Qt norm `XList`) or a plural: superseded by the
  swap above, which gives the multichannel buffer the domain's name.

**I2** joins task T15 (the format enums into audiofileio): the format
moves once, to where it belongs, as one field.

**I3 (approved): the writer stops itself after a failed write.** After the first
failed `write()` every later `write()` returns `{0, the first error}`
without touching the encoder; `close()` still completes the file. One
`std::optional<TAudioFileError> m_failed` in `TAudioWriter`; the count on an
error is always 0 (one meaning, not "what the encoder took" or 0). The
stream still logs each failed slot (`TBufferedAudioStreamWriter.cpp:168-172`)
until task T10 stops it. Prevent, not detect: the comment "the caller stops
writing" becomes what the writer does. Not a contradiction with the error
design, which rejected a sticky error for *reporting* (pattern 5); this is
*behaviour*. What the stream and the user do with that one error stays
task T10.

**I4 (approved): the threading contract in the class comments.** One sentence in
`TAudioReader` and `TAudioWriter`: "One thread at a time: the owner
serializes the calls; not for the audio thread"; the writer's comment adds
that its release-build close in the destructor runs on the owner's thread.
No mechanism: a mutex in the reader would only detect, at a cost on every
read. With W1, which touches the same headers.

## Work order

This document is the work order (see Status): the tasks below, in this
order (W0 first), each its own commits on one branch, tests green on GCC and Clang
before the review.

- **W0 (T44) A round-trip test of every writer and reader, first**
  (`tests/audiofileio/test_audio_round_trip.cpp`), so W1–W4 are checked by
  it. Today's coverage is partial: sample-exact reading only for WAV
  float/16, FLAC 24, WavPack float and the scenario; writing through
  `TAudioWriter` exact only for WAV and WavPack float, the integer formats
  only by a sine's level and phase in mono (`test_resample_writer`); never
  through `TAudioWriter`: FLAC, Ogg, MP3, 32 bit, 8 bit WAV, integer formats
  with more than one channel, WavPack's integer read paths
  (`WPAudioReader::unpack()`). The test writes, through `TAudioWriter`, every
  writer × data format it supports × 1, 2 and 6 channels (a channel count a
  format does not take is skipped with the reason: MP3 and Ogg in libsndfile
  take at most 2, AAC per FAAC) and reads back through `TAudioReader`; the
  source is the scenario decoder (each channel its own signal from
  `test_signals.h`: the ramp in channel 0, sweeps in the others, so a
  swapped or silent channel shows). Expected: float exact; 8/16/24 bit
  within about 2 LSB of their resolution (gdither); 32 bit within 1 LSB;
  lossy formats (Ogg, MP3, AAC) the level of each channel, its placement
  (`best_lag` as in `test_audio_decoders`) and the channel order; the frame
  count of every file. Built and run on the code before W1; what fails there
  is recorded (a KNOWN entry with the reason, or a task) before W1 starts;
  then run again after every W step.
- **W1 (I1 + I4) Buffers**, three steps, each its own commit:
  1. The interleaved and packed buffers out of `TFileIOBuffer` into
     audiofileio. `SFAudioReader.cpp`: a `static` helper returning a
     `static thread_local std::vector<float>` (interleaved) grown to the
     read's size; a channel buffer as interleaved scratch would contradict
     its name after step 2; the mono path's `TAudioBuffer::copy_data()`
     becomes `std::copy`. `WPAudioReader.cpp`: the same for its unpack
     buffer, a `static thread_local std::vector<int32_t>` (now made per
     read). `TAudioWriter`: the same
     rule, `static thread_local` scratch for the interleaved samples and the
     packed bytes (the packed buffer no longer memset per write), and
     `WPAudioWriter`'s `m_tmp_buffer` becomes a `static thread_local
     std::vector<int32_t>` too: used within one call only (WavPack copies
     the samples into its own blocks; `write_block()` uses only the bytes
     WavPack hands it), one thread at a time per writer. One rule for the
     library: scratch a call uses only within itself is `thread_local`
     (Remon, 2026-10-27). `write()` takes `const TFileIOBuffer&`; a
     buffer with fewer channels than the file is a precondition
     (`Q_ASSERT(buffer.channel_count() >= channel_count())`, every caller
     sizes it: `TBufferedAudioStreamWriter.cpp:138`, the merger, the
     tests); the header sentence "a channel the buffer lacks is silence"
     goes. The list gets a const `channel_buffers()` (the resampler takes
     `const TAudioBuffer* const*`, `TAudioResampler.h:72`) and a
     `channel_count()`. `test_file_io_buffer.cpp` loses its interleaved
     checks (`:99,111`).
  2. `TAudioBuffer` → `TChannelBuffer`, `TRealTimeAudioBuffer` →
     `TRealTimeChannelBuffer`, with the files: `src/dsp/TAudioBuffer.h/.cpp`
     → `TChannelBuffer.h/.cpp` (and the real-time one likewise), the CMake
     lists, includes, comments, and `doc/design/realtime-audio-path.md`
     (`:41,256,268-273`). Variable names stay. Behaviour unchanged; Clang's
     `-Wfunction-effects` count stays 7.
  3. `TFileIOBuffer` → `TAudioBuffer`, moved to `src/dsp/TAudioBuffer.h`
     (the name freed by step 2); method names unchanged
     (`get_channel_buffer()`, `channel_buffers()`, `check_capacity()`,
     `silence()`, `channel_count()`). `test_file_io_buffer.cpp` →
     `tests/dsp/test_audio_buffer.cpp` with its CMake entry. Comments and
     docs follow (`doc/design/*.md` that name `TFileIOBuffer`). A new
     `doc/design/renames.md` (a list of renamed names, for old documents,
     commits, other forks and agents): "`TAudioBuffer` meant one channel
     until 2026-10-28 (now `TChannelBuffer`); since then the list of
     channels (before `TFileIOBuffer`)".
  The commit messages of steps 2 and 3 say that the name `TAudioBuffer`
  changed meaning. Then the threading sentence in the class comments of
  `TAudioReader` and `TAudioWriter` (I4).
- **W2 (I3) The writer stops after a failed write.** `m_failed` in
  `TAudioWriter`; later writes give `{0, the first error}` without the
  encoder, `close()` completes the file. Test: on `/dev/full` (WavPack) a
  write after the failure returns the first error at once.
- **W3 (I5) The buffers' hygiene**, on the new names. `TAudioBuffer` (the
  list): `Q_ASSERT(channel < count)` and `[]` instead of `std::vector::at()`
  (which throws, `TFileIOBuffer.h:47,72`); `nframes_t` in
  `check_capacity()`; no channels made in advance (they come from
  `check_capacity()`, at the size asked; every decoder and `read_from()`
  call it before writing). `TChannelBuffer`: not copyable (`= delete` on the
  copy constructor and assignment: a copy shares the raw pointer and frees
  it twice). With the copy gone there is no implicit move either, and none
  is added: no container holds channel buffers by value (the list and the
  ring slots hold `unique_ptr`s, `TFileIOBuffer.h:120`,
  `TQueueBufferSlot.h:94`; the list needs them for its stable pointer
  array), none is copied or returned by value. A
  `static_assert(!std::is_copy_constructible_v<...>)` in the buffer test; the rule "size > 0, always" in the header as the
  class contract. It holds today: the constructor goes through `resize()`,
  which asserts `size > 0` (`TAudioBuffer.cpp:64`); no caller found passes
  0 (the real-time buffers take the device's buffer size, default 1024);
  the list made its channels with 1 frame in advance only because 0 is not
  allowed. No release-build conversion of 0: nothing gives 0; a future one
  hits the debug assert. With it (I7): `TAudioWriter.h` loses its unused
  `<vector>`. `TAudioReader::output_frame_count()` stays public (changed
  after Fable's work order review): the tests use it to check the reader's
  end rule against `TTimeRef` (`test_resample_reader.cpp:106`), and a
  getter only tests use costs nothing.
- **W4 (I6) The resampler choice as one typed value, in `support`.** A
  backend and its quality belong together (a soxr quality means nothing to
  libsamplerate) but travel apart in three shapes: two parameters of
  `set_output_rate_and_resampler()`, two fields of `TAudioEncodeSettings`,
  and raw integers in `TIOStream::Resampler`, cast back in core
  (`TBufferedAudioStreamReader.cpp:239`). The raw integers are there on
  purpose: the ioscheduler links `support` and Qt Core only, not `dsp`.
  Typed without that dependency, in `src/support/TResamplerChoice.h`:

  ```cpp
  struct TResamplerChoice
  {
      enum class Backend : qint8 { LibSampleRate, LibSoxr };
      Backend backend;
      qint16 quality;   // one of TAudioResampler::qualities(backend)
      bool operator==(const TResamplerChoice&) const = default;
  };
  ```

  The quality is a plain number, so the value from the configuration is
  checked against `qualities(backend)` where it enters, and only there:
  `configured_resampler()` (`TResamplerSettings.cpp:17`, falls back to the
  default) for reading, the export dialog (it offers only
  `qualities(backend)`, `ExportFormatOptionsWidget.cpp:57,199-200`) for
  writing; behind that the libraries trust it, and the asserts in
  `TAudioResampler`'s constructor and `TExportSpecification::set_resampler()`
  catch a mistake in a debug build. No default: every maker sets the choice (`TExportSpecification.cpp:346-347`,
  `TResamplerSettings`, the tests already do); a type in `support` cannot
  call `TAudioResampler::default_quality()`. In `TAudioEncodeSettings` it is
  a `std::optional<TResamplerChoice>` (Remon's review 2026-10-28: a zeroed
  choice is libsamplerate at its best, most expensive quality, silently);
  a writer that has to resample without a choice does not open
  (`CannotOpen`). `TIOStream::Resampler` becomes
  `TResamplerChoice choice; bool resample;` and stays 8 bytes and lock-free
  (the `static_assert` at `TIOStream.h:89` stays). `TAudioResampler` takes
  it; its `BackendType` goes; `qualities()`, `quality_name()`,
  `quality_description()`, `backend_name()`, `backend_from_name()`,
  `default_quality()`, `default_realtime_quality()` stay static on
  `TAudioResampler`, taking `TResamplerChoice::Backend`.
  `set_output_rate_and_resampler(rate, TResamplerChoice)`;
  `TAudioEncodeSettings` one field `resampler` (its `TAudioResampler.h`
  include goes; T15 then has only `gdither_types.h` left there);
  `TExportSpecification` one field and `set_resampler(TResamplerChoice)`;
  `configured_resampler_backend()` and the export dialog's member use
  `TResamplerChoice::Backend`; the config keys (`Conversion/Resampler`,
  `Conversion/RTResamplingQuality`, `Conversion/DynamicResampling`,
  `ExportFormatOptionsWidget/resampleQuality`) stay as they are; the cast
  in core goes. Shared vocabulary of four libraries (dsp, audiofileio,
  ioscheduler, core), so `support` is its place, unlike the format enums T15
  moves out (used by audiofileio and core only). Rejected: an untyped
  hand-on (no compile-time check); a `dsp` type in the ioscheduler (a
  dependency for a value it only hands on). Medium, mechanical.

I2 is part of task T15.

## Smells with no task

- #4, the stream part: `TBufferedAudioStreamWriter::m_writtenFrames` counts
  input frames at the device rate, while `TWaveform` and `TPeakBuildService`
  read it as the file's frames: a task in `doc/status.md` (not ranked yet).
- #5 (`CannotOpen` for a rate change and "no input rate"; a write after
  `close()` is `WriteFailed`), #11 (`open()` as the only way to ask a
  file's facts), #12 (`value_or(nullptr)` in tests and tools): no change.

## Status

All points discussed (2026-10-27). Fable reviewed this work order and
approved it with changes, which are in (the W4 types and no default, W2's
log claim, W1's channel precondition and file moves; the debug guard
dropped; `output_frame_count()` stays public). Remon's last review points
are in too: a channel buffer is not movable either (containers hold
`unique_ptr`s), the scratch is plain `std::vector`s, `renames.md`, and the
one place where a configured quality is checked. No rank: Remon approved this as a work
order once Fable has reviewed and approved the document; then it is built
after midnight, on a branch stacked on `audiofileio-errors` (it builds on
that API).
