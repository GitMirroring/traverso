/*
    Copyright (C) 2005-2026 Remon Sijrier

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

#include "Debugger.h"

#ifdef TRAVERSO_DEV_LOG

static thread_local int t_depth = 0;

FunctionEnter::FunctionEnter(const char* category, const char* function)
    : m_category(category)
    , m_function(function)
    , m_active(TLog::enabled(TLogLevel::Trace))
{
    if (m_active) {
        TLog::write_printf(TLogLevel::Trace, m_category, "%*s> %s", std::min(t_depth, 40) * 2, "", m_function);
        ++t_depth;
    }
}

FunctionEnter::~FunctionEnter()
{
    if (m_active) {
        --t_depth;
        TLog::write_printf(TLogLevel::Trace, m_category, "%*s< %s", std::min(t_depth, 40) * 2, "", m_function);
    }
}

#endif
