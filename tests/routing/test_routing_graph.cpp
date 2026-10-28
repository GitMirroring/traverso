// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TRoutingGraph::closes_loop() (T64): a send to the own
// bus, a loop through two and through three bus tracks are refused; a send
// to a hardware bus, a send next to an existing one and a diamond (two paths
// to one bus, no loop) are allowed; a loop that already exists further on
// does not hang the check.

#include "TRoutingGraph.h"

#include <QHash>
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
    // Ids: 1-4 bus tracks, 10 an audio track, 100 the hardware out
    QHash<qint64, QList<qint64>> sends;
    auto targetsOf = [&sends](qint64 id) { return sends.value(id); };

    check(TRoutingGraph::closes_loop(1, 1, targetsOf), "a send to the own bus closes a loop");
    check(!TRoutingGraph::closes_loop(1, 100, targetsOf), "a send to the hardware out does not");

    sends[1] = {2};
    check(TRoutingGraph::closes_loop(2, 1, targetsOf), "a send back to the bus that sends here closes a loop");
    check(!TRoutingGraph::closes_loop(10, 1, targetsOf), "an audio track to a bus does not");

    sends[2] = {3};
    check(TRoutingGraph::closes_loop(3, 1, targetsOf), "a loop through three bus tracks is found");
    check(!TRoutingGraph::closes_loop(1, 3, targetsOf), "a second path to a bus already reached is no loop");

    sends[10] = {1, 4};
    sends[4] = {3};
    check(!TRoutingGraph::closes_loop(3, 100, targetsOf), "a diamond into one bus and on to the out is no loop");
    // The graph alone: in the application nothing can send to an audio
    // track (its process bus is the sheet's render bus, never a target)
    check(TRoutingGraph::closes_loop(3, 10, targetsOf), "graph only: an edge back to the source at the top closes a loop");

    // A loop that got in anyway (2 and 3 send to each other) must not hang
    // a check that passes through it
    sends[3] = {2};
    check(!TRoutingGraph::closes_loop(1, 2, targetsOf), "a check through an existing loop ends, no loop back to the start");
    check(TRoutingGraph::closes_loop(2, 3, targetsOf), "a send inside the existing loop is found");

    std::printf("%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
