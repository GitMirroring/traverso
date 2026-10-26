// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TRealTimeClosure and TGuiClosure: it runs its captures, a
// copy runs the same, and an empty one says so. What may not be captured is checked by the
// compiler (static_assert), not here.

#include "TRealTimeClosure.h"

#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

int main()
{
    TRealTimeClosure empty;
    check(!empty, "a default closure is empty");
    TRealTimeClosure fromNull = nullptr;
    check(!fromNull, "a closure from nullptr is empty");

    int target = 0;
    int* where = &target;
    const int add = 5;
    TRealTimeClosure closure([where, add]() T_REALTIME_SAFE { *where += add; });
    check(bool(closure), "a closure with captures is not empty");
    closure();
    check(target == 5, "it runs with its captures");

    TRealTimeClosure copy = closure;
    copy();
    check(target == 10, "a copy runs the same captures");

    // Three pointers fit
    int a = 0, b = 0, c = 0;
    int *pa = &a, *pb = &b, *pc = &c;
    TRealTimeClosure three([pa, pb, pc]() T_REALTIME_SAFE { *pa = 1; *pb = 2; *pc = 3; });
    three();
    check(a == 1 && b == 2 && c == 3, "three pointers fit");

    closure = nullptr;
    check(!closure, "cleared, it is empty");

    check(sizeof(TRealTimeClosure) == 4 * sizeof(void*), "the closure is four pointers: storage and the call");

    // The GUI kind: its call need not be real-time safe (not noexcept here)
    int guiTarget = 0;
    int* guiWhere = &guiTarget;
    TGuiClosure gui([guiWhere]() { *guiWhere = 7; });
    TGuiClosure guiCopy = gui;
    guiCopy();
    check(guiTarget == 7, "a GUI closure runs its captures, also as a copy");
    TGuiClosure guiEmpty = nullptr;
    check(!guiEmpty, "a GUI closure from nullptr is empty");

    return failures == 0 ? 0 : 1;
}
