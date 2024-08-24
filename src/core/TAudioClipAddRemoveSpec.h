/*
Copyright (C) 2024 Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#ifndef TAUDIOCLIPADDREMOVESPEC_H
#define TAUDIOCLIPADDREMOVESPEC_H

#include "qassert.h"

class TAudioClip;

class TAudioClipAddRemoveSpec {

public:
    explicit TAudioClipAddRemoveSpec()
    {
    }

    void set_clip(TAudioClip* clip) {
        m_clip = clip;
        Q_ASSERT(m_clip);
    }
    constexpr  TAudioClip* get_clip() const {
        Q_ASSERT(m_clip);
        return m_clip;
    }

    void set_is_move(bool isMove) {
        m_isMove = isMove;
        m_moveWasSet = true;
    }

    void set_is_historable(bool historable) {
        m_isHistorable = historable;
        m_historableWasSet = true;
    }

    constexpr  bool is_move() const {
        Q_ASSERT(m_moveWasSet);
        return m_isMove;
    }
    constexpr bool is_historabel() const {
        Q_ASSERT(m_historableWasSet);
        return m_isHistorable;
    }

private:
    bool m_isMove = false;
    bool m_moveWasSet = false;
    bool m_isHistorable = false;
    bool m_historableWasSet = false;
    TAudioClip*  m_clip = nullptr;
};

#endif // TAUDIOCLIPADDREMOVESPEC_H
