// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The real-time requirement as a property of a function, checked by Clang 20
// and later (-Wfunction-effects, RealtimeSanitizer); empty for other
// compilers. What it means and where it applies:
// doc/design/realtime-audio-path.md.

#if defined(__clang__) && __clang_major__ >= 20

// The function meets the real-time requirement: it does not allocate, lock,
// wait, block or throw. Clang checks its body and every call it makes.
#define T_REALTIME_SAFE [[clang::nonblocking]]
// The function blocks by design (a wait, disk IO): Clang reports a call to it
// from a real-time safe function, also where it cannot see its body.
#define T_BLOCKING [[clang::blocking]]

// One statement that is real-time safe by its contract where Clang cannot
// see it (§5): a call into a library (the clock, an LV2 plugin's run()), a
// thread_local that is constant initialised. Clang's warning is off for that
// statement only; RealtimeSanitizer still checks what it does.
#define T_REALTIME_TRUSTED(...) \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wfunction-effects\"") __VA_ARGS__ _Pragma("clang diagnostic pop")

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#include <sanitizer/rtsan_interface.h>
#define T_REALTIME_SANITIZER_OFF __rtsan::ScopedDisabler realtimeSanitizerOff;
#endif
#endif

#else

#define T_REALTIME_SAFE
#define T_BLOCKING
#define T_REALTIME_TRUSTED(...) __VA_ARGS__

#endif

#if !defined(T_REALTIME_SANITIZER_OFF)
#define T_REALTIME_SANITIZER_OFF
#endif

// One statement that blocks by design inside the audio processing path (§5):
// the clock of a driver that waits inside the cycle, a bounded wait. Neither
// Clang nor RealtimeSanitizer reports it. Only in a small named function,
// with a comment that says why.
#define T_REALTIME_EXCEPTION(...) \
    { \
        T_REALTIME_SANITIZER_OFF \
        T_REALTIME_TRUSTED(__VA_ARGS__) \
    }
