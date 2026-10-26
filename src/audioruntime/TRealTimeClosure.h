// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TRealTime.h"

#include <QtGlobal>

#include <cstddef>
#include <new>
#include <type_traits>

// A closure whose captures are kept inline, at most three pointers, so making,
// copying and clearing it never allocates or frees; they are trivially
// copyable and destructible, so a copy is a memcpy and nothing runs when it
// goes away. This holds with every compiler and standard library. A closure
// that does not fit is a compile error, not a heap allocation. Two kinds:
//
// - TRealTimeClosure runs in the audio thread (TAudioThreadMessageQueue::
//   post_rt_task()). Calling it is real-time safe too: the closure must be
//   noexcept, or it does not compile; with Clang it is also checked as
//   real-time safe where it is made. A lambda that is not marked
//   T_REALTIME_SAFE is a Clang warning (-Wfunction-effects), an error once
//   step 5 of doc/design/realtime-audio-path.md is done.
// - TGuiClosure passes through the audio thread, which may drop it, and runs
//   in the message queue worker (TAudioThreadMessageQueueEvent::guiCallback):
//   making, copying and dropping it are real-time safe, calling it may block
//   (emit a signal, free memory).
//
// The type guarantees what its kind says about making, copying and calling,
// not that the pointers it captures are still valid when it runs: that is the
// business of the queue and of the owner of what they point to.
template <bool CallIsRealTimeSafe>
class TInlineClosure
{
public:
    TInlineClosure() = default;
    TInlineClosure(std::nullptr_t) {}

    // The requires clause only keeps the copy and the nullptr constructors
    // for their own types; what a closure must be is checked by the
    // static_asserts, whose messages say what is wrong
    template <typename Closure>
        requires (!std::is_same_v<std::decay_t<Closure>, TInlineClosure>
                  && !std::is_same_v<std::decay_t<Closure>, std::nullptr_t>)
    TInlineClosure(Closure closure) noexcept
    {
        static_assert(sizeof(Closure) <= Capacity && alignof(Closure) <= alignof(void*),
                      "the closure's captures must fit in three pointers");
        static_assert(std::is_trivially_copyable_v<Closure> && std::is_trivially_destructible_v<Closure>,
                      "the closure may capture pointers and plain values only, nothing that owns memory");
        static_assert(!CallIsRealTimeSafe || std::is_nothrow_invocable_v<Closure&>,
                      "TRealTimeClosure: it runs in the audio thread, mark the lambda T_REALTIME_SAFE");

        ::new (static_cast<void*>(m_storage)) Closure(closure);
        m_invoke = [](void* storage) T_REALTIME_SAFE_IF(CallIsRealTimeSafe) {
            (*std::launder(static_cast<Closure*>(storage)))();
        };
    }

    void operator()() T_REALTIME_SAFE_IF(CallIsRealTimeSafe)
    {
        Q_ASSERT(m_invoke);
        m_invoke(m_storage);
    }

    explicit operator bool() const T_REALTIME_SAFE
    {
        return m_invoke != nullptr;
    }

private:
    static constexpr std::size_t Capacity = 3 * sizeof(void*);

    alignas(void*) unsigned char m_storage[Capacity]{};
    void (*m_invoke)(void*) T_REALTIME_SAFE_IF(CallIsRealTimeSafe) {nullptr};
};

using TRealTimeClosure = TInlineClosure<true>;
using TGuiClosure = TInlineClosure<false>;

static_assert(std::is_trivially_copyable_v<TRealTimeClosure> && std::is_trivially_destructible_v<TRealTimeClosure>);
static_assert(std::is_trivially_copyable_v<TGuiClosure> && std::is_trivially_destructible_v<TGuiClosure>);
