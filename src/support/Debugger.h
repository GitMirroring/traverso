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

// The legacy debug macros on top of TLog: PERROR and PWARN are the session
// log's error and warning, PMESG and PDEBUG the developer log's debug,
// PMESG2..4 and PENTER* its trace. The category is the source file.

#ifndef TRAVERSO_DEBUGGER_H
#define TRAVERSO_DEBUGGER_H

#include "TLog.h"

// The file name without its directories, from __FILE__ at compile time
constexpr const char* tlog_file_basename(const char* path)
{
    const char* base = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

#define TLOG_FILE_CATEGORY tlog_file_basename(__FILE__)

// Always compiled: the session log
#define PERROR(message)      TLog::write_text(TLogLevel::Error, TLOG_FILE_CATEGORY, message)
#define PERROR2(...)         TLog::write_printf(TLogLevel::Error, TLOG_FILE_CATEGORY, __VA_ARGS__)
#define PWARN(message)       TLog::write_text(TLogLevel::Warning, TLOG_FILE_CATEGORY, message)
#define PWARN2(...)          TLog::write_printf(TLogLevel::Warning, TLOG_FILE_CATEGORY, __VA_ARGS__)

#ifdef TRAVERSO_DEV_LOG

// Logs entering and leaving a function at trace level, indented by depth
class FunctionEnter
{
public:
    FunctionEnter(const char* category, const char* function);
    ~FunctionEnter();

private:
    const char* m_category;
    const char* m_function;
    bool        m_active;
};

#define PENTER      FunctionEnter traversoFunctionEnter(TLOG_FILE_CATEGORY, __FUNCTION__)
#define PENTER2     PENTER
#define PENTER3     PENTER
#define PENTER4     PENTER
#define PENTERCONS  PENTER
#define PENTERDES   PENTER
#define PENTERCONS2 PENTER
#define PENTERDES2  PENTER
#define PENTERCONS3 PENTER
#define PENTERDES3  PENTER
#define PENTERCONS4 PENTER
#define PENTERDES4  PENTER

#define PMESG(...)   TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, __VA_ARGS__)
#define PDEBUG(...)  TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, __VA_ARGS__)
#define PMESG2(...)  TLog::write_printf(TLogLevel::Trace, TLOG_FILE_CATEGORY, __VA_ARGS__)
#define PMESG3(...)  TLog::write_printf(TLogLevel::Trace, TLOG_FILE_CATEGORY, __VA_ARGS__)
#define PMESG4(...)  TLog::write_printf(TLogLevel::Trace, TLOG_FILE_CATEGORY, __VA_ARGS__)

#else

#define PENTER      ((void)0)
#define PENTER2     ((void)0)
#define PENTER3     ((void)0)
#define PENTER4     ((void)0)
#define PENTERCONS  ((void)0)
#define PENTERDES   ((void)0)
#define PENTERCONS2 ((void)0)
#define PENTERDES2  ((void)0)
#define PENTERCONS3 ((void)0)
#define PENTERDES3  ((void)0)
#define PENTERCONS4 ((void)0)
#define PENTERDES4  ((void)0)

#define PMESG(...)   ((void)0)
#define PDEBUG(...)  ((void)0)
#define PMESG2(...)  ((void)0)
#define PMESG3(...)  ((void)0)
#define PMESG4(...)  ((void)0)

#endif

#define PMARK(...)        ((void)0)
#define PMESG_START(...)  ((void)0)
#define PMESG_END(...)    ((void)0)
#define PMESG2_START(...) ((void)0)
#define PMESG2_END(...)   ((void)0)

#endif
