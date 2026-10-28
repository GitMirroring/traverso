# T20: buffer operations, measured (2026-10-28)

Question: can the portable loops of `TBufferOperations` replace the SSE
assembly (`sse_functions*.S`), with AVX2 through `target_clones`?

## Setup

- The benchmark (kept outside the repository, in
  `~/Development/Traverso/builds/t20-bench-src/`, with a copy of the removed
  `sse_functions_64bit.S`): `bench_variants.cpp` holds the candidates (the
  portable loops copied verbatim from `TBufferOperations.cpp`, in their own
  translation unit, called through function pointers as in Traverso);
  `bench_buffer_operations.cpp` checks each candidate against a scalar loop
  (all correct, every build) and times it; `bench.sh <dir>` builds GCC 15.2
  and Clang 21.1 at -O2 and -O3 with vectorisation reports, and runs each
  build twice on a P-core (CPU 2) and an E-core (CPU 6).
- Machine: i5-1245U (2 P-cores, 8 E-cores, AVX2). FTZ/DAZ on, as on the
  audio thread. Buffers in L1, 64-byte aligned and one sample off.
- Figure: the minimum ns per call over 2 × 101 repeats. The SSE column is
  the same assembly in every build and agrees within a few percent.
- Builds that matter: CMake Release is -O3; Debian packages are -O2
  (`dh` with `dpkg-buildflags`).

Candidates:
- `portable`: today's loops. `clones`: the same with
  `target_clones("avx2", "default")`.
- `restrict`: the mixes with `__restrict` on `dst` and `src`.
- Peak: `f_max` (today), `std::max` in one chain, and `lanesN`: N
  independent maxima, merged at the end (no reordering needed, so the
  compiler may vectorise without -ffast-math).

## Results, P-core, 1024 frames, aligned (ns per call)

| operation | candidate | GCC -O2 | GCC -O3 | Clang -O2 | Clang -O3 |
|---|---|---|---|---|---|
| peak | portable (f_max) | 2984 | 2984 | 2983 | 2984 |
| peak | std::max chain | 889 | 889 | 866 | 864 |
| peak | lanes16 | 346 | 138 | 61 | 61 |
| peak | clones(lanes16) | 364 | 44 | 61 | 61 |
| peak | **sse** | 170 | 169 | 170 | 170 |
| apply_gain | portable | 64 | 66 | 48 | 52 |
| apply_gain | clones | 36 | 33 | 24 | 23 |
| apply_gain | **sse** | 74 | 69 | 68 | 71 |
| mix_with_gain | portable | 240 | 79 | 60 | 60 |
| mix_with_gain | restrict | 78 | 79 | 60 | 60 |
| mix_with_gain | clones(restrict) | 47 | 48 | 39 | 39 |
| mix_with_gain | **sse** | 86 | 88 | 85 | 87 |
| mix | portable | 225 | 68 | 60 | 61 |
| mix | restrict | 66 | 65 | 62 | 61 |
| mix | clones(restrict) | 33 | 37 | 28 | 27 |
| mix | **sse** | 81 | 75 | 82 | 79 |

E-core, 64 and 256 frames and unaligned pointers: the same order. Unaligned
narrows the AVX2 gain (mix 1024: 54 vs 82 SSE instead of 33 vs 81). At 64
frames everything is 3-12 ns, the f_max peak 136 ns.

## What the reports say

- GCC -O2 vectorises only `apply_gain`. The mixes need a run-time overlap
  check of `dst` and `src`, which GCC's -O2 cost model ("very cheap")
  refuses: 3× slower than SSE. With `__restrict` they vectorise at -O2.
  `-fvect-cost-model=dynamic` also vectorises them (mix 75 ns), but makes
  the lanes peak worse (580 ns).
- No compiler vectorises a peak in one chain (f_max or std::max): a
  reduction may not be reordered without -ffast-math. The f_max trick is
  also a 4-operation dependency chain per sample: 17× slower than SSE, 3×
  slower than std::max.
- The lanes peak vectorises with Clang at -O2 (2.8× faster than SSE) and
  with GCC at -O3 (1.2×, cloned 3.9×); at GCC -O2 it stays 2× slower
  than SSE.

## Conclusions

1. `apply_gain`: the portable loop is already as fast as SSE in every build;
   cloned twice as fast.
2. The mixes: with `__restrict`, portable ≥ SSE in every build; cloned
   1.8-2.5× faster.
3. The peak: only a lanes loop beats SSE, and with GCC only at -O3 for that
   file.
4. So the assembly can go, provided: `__restrict` on the mixes,
   a lanes peak, and the file at -O3 for GCC (or the peak keeps SSE on GCC
   -O2 builds). AVX2 through `target_clones` is a further gain of about 2×
   on Linux/glibc only.

## Open for decision

- **NaN in the peak.** Measured, 1024 frames with one NaN at frame 0, 3,
  100 or 1023: `f_max` returns NaN every time; `std::max` and lanes16
  return 0.5 every time; the SSE assembly returns 0.5, except with the NaN
  at the last frame (`maxps` gives its second operand when one is NaN, so
  the next sample overwrites it). So on x86, where SSE is in use, the meter
  already hides a NaN today; only the portable path passes it on. If the
  peak must show NaN (T8), that is a new requirement for every candidate,
  SSE included; a lanes peak can do it with `(x > lane || x != x) ? x : lane`
  (not measured).
- **`__restrict` is a promise**: `dst` and `src` never overlap. All callers
  (`TChannelBuffer::mix_buffers_*`, from `TAudioClip.cpp:585-589` and
  `TTrack.cpp:489`) mix two different channels' buffers; whether a send can
  point at its own track's bus is not checked. A `Q_ASSERT(dst != src)` in
  `TChannelBuffer` would guard it in debug builds.
- **-O3 for one file under GCC -O2 builds**: a
  `set_source_files_properties(... COMPILE_OPTIONS -O3)` overrides the
  packager's choice for that file; the alternative is keeping SSE for the
  peak.
- **target_clones**: about 2× on top, only Linux with glibc (ifunc); macOS
  keeps Accelerate, Windows gets the portable loops.

Not measured: the effect in the whole mix (a cycle's mix work is a few
microseconds against a period of several milliseconds), AMD CPUs, AVX-512.

## Addendum: the peak without depending on -O3 (2026-10-28)

Remon: can we get the performance at -O2, so it does not depend on an -O3
flag? Peak, 1024 frames, P-core, ns per call (all equal to the scalar
reference); test code in `~/Development/Traverso/builds/t20-o2/`:

| candidate | GCC -O2 | GCC -O3 | Clang -O2 | Clang -O3 |
|---|---|---|---|---|
| SSE assembly | 169 | 168 | 180 | 175 |
| vector extension, 4 accumulators of 4 | 133 | 133 | 56 | 61 |
| the same, compiled with `-mavx2` | 56 | 56 | 56 | 56 |
| plain C++ "blocks" (4×4 per block) | 154 | 182 | 70 | 64 |
| plain C++ "blocks32" | 152 | 142 | 357 | 79 |
| `#pragma omp simd reduction(max)` | 426 | 426 | 868 | 866 |

Plain loops depend on compiler, level and wording (blocks32: 357 or 79 ns
at Clang -O2 or -O3). The GCC/Clang vector extension
(`float __attribute__((vector_size(16)))`) does not: the code is written in
vectors, the vectoriser decides nothing. It builds everywhere Traverso
builds (GCC and Clang on Linux, MinGW GCC on Windows, Clang on macOS; NEON
on ARM); not MSVC.

Decided (Remon, 2026-10-28): the peak in the vector extension; the mixes
and the gain stay plain loops, the mixes with `__restrict` after T64
(feedback loops excluded, `Q_ASSERT(&dest != &src)`); AVX2 in its own
translation unit with `-mavx2`, chosen through `TCpuFeatures`, not
`target_clones`; no -O3 per file; the SSE assembly goes; `f_max` goes (NaN
stays out of the buffers through T8). To check at implementation: whether
`-mavx2` per file survives `-flto=auto` (Ubuntu's flags).

## Result: the implementation, with Debian's flags (2026-10-28)

`TBufferOperations` as built (`TBufferOperationsLoops.h`, the portable file
and `TBufferOperationsAVX2.cpp`), with `dpkg-buildflags` (`-O2 -flto=auto`,
`CMAKE_BUILD_TYPE=None`), against the removed SSE assembly; P-core, minimum
of two runs, ns per call:

| n | implementation | peak | gain | mix with gain | mix |
|---|---|---|---|---|---|
| 64 | portable | 8.6 | 5.8 | 5.7 | 5.2 |
| 64 | AVX2 | 4.5 | 4.5 | 4.1 | 3.5 |
| 64 | old SSE assembly | 6.6 | 7.1 | 9.2 | 8.7 |
| 256 | portable | 33.0 | 22.1 | 21.2 | 17.8 |
| 256 | AVX2 | 13.7 | 10.7 | 11.3 | 9.3 |
| 256 | old SSE assembly | 26.4 | 18.5 | 24.4 | 20.7 |
| 1024 | portable | 138 | 67 | 80 | 66 |
| 1024 | AVX2 | 58 | 35 | 50 | 37 |
| 1024 | old SSE assembly | 171 | 75 | 99 | 86 |

E-core, 1024 frames, AVX2 against the assembly: peak 72 / 219, gain 70 /
168, mix with gain 90 / 169, mix 87 / 135.

AVX2 is faster than the assembly everywhere. The portable path is faster
except the peak of short buffers (256 frames: 33 against 26 ns; the tail
outside the blocks of 16), a few nanoseconds per cycle. With LTO the AVX2
file keeps its ymm code and the portable functions have no VEX instruction
(objdump). The test compares every implementation the machine runs with
its own scalar reference, at unaligned pointers too; it found a Clang
detail: `aligned(4)` in a `using` alias of a vector type is dropped (an
aligned load, a crash), so the load type is a typedef, as `__m128_u`.

In the application (Remon, 2026-10-28): Pirates, ALSA, 256 frames, both
binaries with Debian's flags (`-O2 -flto=auto`), master with the SSE
assembly against this branch with AVX2: DSP load 1.4 % and top 0.8 % for
both. As expected: the four operations are about a microsecond of a
5805 µs cycle here, below what the load meter shows. No regression; the
gain shows per operation, not in this project's load.

## Freewheel throughput (2026-10-28)

The load meter cannot show it; freewheel can (cycles back to back, no
waiting). Minidisk opname, Dummy driver (1024 frames), 10 s of freewheel
playback per run, both binaries with Debian's flags; a temporary hook
(never merged) turned freewheel on, played and stopped; throughput = audio
seconds (`stopped at ... universal frames`) per wall second, from the log.
One warm-up run each, then four rounds, the three alternating:

| round | master (SSE assembly) | T20 portable | T20 AVX2 |
|---|---|---|---|
| 1 | 902× | 977× | 1283× |
| 2 | 797× | 886× | 1142× |
| 3 | 804× | 865× | 1208× |
| 4 | 752× | 880× | 1121× |
| mean | 814× | 902× | 1189× |

Per round against master: AVX2 1.42 / 1.43 / 1.50 / 1.49 (about +46 %),
portable 1.08 / 1.11 / 1.08 / 1.17 (about +11 %). All three drift down
over the rounds (heat, clock), the order within each round never changes.
At about 800× a 1024-frame cycle takes about 29 µs, and the buffer
operations are a large part of it: there are far more calls per cycle
(clips, tracks, buses, meters) than the "a microsecond per cycle" estimate
above assumed.

## The portable peak with maxps (2026-10-28)

The portable peak lost to the assembly on short buffers (above: 33 against
26 ns at 256 frames). Not the tail (64 and 256 are multiples of 16): GCC
compiles the vector `a > b ? a : b` to `cmpltps`, `andps`, `andnps`,
`orps` in a chain instead of one `maxps`. `max4` now uses `_mm_max_ps` on
x86, which is `a > b ? a : b` per lane including NaN and the signed zeros,
so the results are bit for bit the same (the test is exact). Debian's
flags, P-core, peak in ns:

| n | portable before | portable with maxps | AVX2 | old SSE assembly |
|---|---|---|---|---|
| 64 | 8.6 | 5.0 | 4.4 | 6.4 |
| 256 | 33.0 | 14.3 | 13.3 | 26.4 |
| 1024 | 138 | 62 | 58 | 171 |

E-core: 64 frames 10.9 against 10.2 (noise), 256 frames 24.9 against
41.6, 1024 frames 78.9 against 219. No regression left; a tail in steps of
4 is not needed (periods are powers of two).

## Bit for bit (2026-10-28)

With Debian's flags, FTZ and DAZ on: the plain scalar loops (vectoriser
off), the old SSE assembly, the new portable and the AVX2 operations, and
the old f_max peak; 76 800 cases (24 lengths from 0 to 4096, every
alignment of src and dst, 200 trials with gains from -4 to 4, values near
1e-30, +0 and -0), compared with `memcmp`, 8 guard samples included: no
difference anywhere. The f_max peak gave no difference either (its exactness
is not guaranteed, but no case was found). No FMA on x86 (`-mavx2` does not
enable it), so `tests/dsp/test_buffer_operations.cpp` now asks for exact
results there; elsewhere 1e-6 stays (aarch64 GCC may join `d + s * g` into
an FMA, as it could before T20).
