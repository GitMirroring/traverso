// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QList>
#include <QSet>
#include <QtGlobal>

// The routing as a graph: a node is a track or a bus (a bus track's process
// bus has the id of its track), an edge a send. Feedback loops are excluded
// (T64): a send is refused when it would close one.
class TRoutingGraph
{
public:
    // True when an edge from -> to closes a loop: to is from itself, or from
    // is reachable from to. targetsOf(id) gives the ids an id sends to.
    template <typename TargetsOf>
    static bool closes_loop(qint64 from, qint64 to, TargetsOf targetsOf)
    {
        QList<qint64> pending{to};
        QSet<qint64> visited;
        while (!pending.isEmpty()) {
            const qint64 id = pending.takeLast();
            if (id == from) {
                return true;
            }
            if (visited.contains(id)) {
                continue;
            }
            visited.insert(id);
            pending.append(targetsOf(id));
        }
        return false;
    }
};
