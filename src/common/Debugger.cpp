/*
    Copyright (C) 2005-2006 Remon Sijrier 
 
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
 
    $Id: Debugger.cpp,v 1.3 2009/02/23 20:12:09 r_sijrier Exp $
*/

#include <cstdlib>
#include <cstdio>

#include "Debugger.h"

namespace TraversoDebugger
{
int ntabs = 0;
int debugLevel = OFF;
FILE* logFile = (FILE*) nullptr;
QString logFileName = nullptr;
bool logging = false;
}

void TraversoDebugger::fill_tabs()
{
        for (int i=0; i < ntabs; i++)
                printf("|   ");
}

QString TraversoDebugger::get_tabs()
{
        QString t="";
        for (int i=0; i < ntabs; i++)
                t=t.append("   ");
        return t;
}


void TraversoDebugger::more_tabs()
{
        ntabs++;
}


void TraversoDebugger::less_tabs()
{
        ntabs--;
}


void TraversoDebugger::set_debug_level(int l)
{
        debugLevel = l;
}


int TraversoDebugger::get_debug_level()
{
        return debugLevel;
}


void TraversoDebugger::create_log(const QString& fn)
{
        logFileName = QString(getenv("HOME")) + "/" + fn;
        logFile = fopen(logFileName.toLatin1(),"a+");
        if (!logFile) {
//                PERROR("Cannot create TraversoDebugger Log file (%s)",fn.toLatin1().data());
                logging=false;
        } else {
                fclose(logFile);
                logging=true;
        }
}



void TraversoDebugger::close_log()
{
        logging=false;
}



void TraversoDebugger::log(const QString& s)
{
//        const char* sc = s.toLatin1();
        int len = s.length();
        logFile = fopen(logFileName.toLatin1(),"a+");
        fwrite(s.toLatin1(),len,1,logFile);
        fclose(logFile);
}

bool TraversoDebugger::is_logging()
{
        return logging;
}


#ifdef USE_DEBUGGER

static void print_enter(int lvl, const char* file, const char* function)
{
        using namespace TraversoDebugger;

        if (get_debug_level()>=lvl) {
                if (is_logging()) {
                        QString output = get_tabs() + "ENTERING " + QString(file) + "::" + QString(function) + "\n";
                        log(output);
                        more_tabs();
                } else {
                        fill_tabs();
                        CHANGE_COLOR_GREEN;
                        std::cout << "ENTERING ";
                        CHANGE_COLOR_WHITE;
                        std::cout << QString("%1::%2").arg(file, function).toLatin1().data() << &std::endl;
                        more_tabs();
                }
        }
}

static void print_exit(int lvl, const char* file, const char* function)
{
        using namespace TraversoDebugger;

        if (get_debug_level()>=lvl) {
                if (is_logging()) {
                        less_tabs();
                        QString output = get_tabs() + "LEAVING " + QString(file) + "::" + QString(function) + "\n";
                        log(output);
                } else {
                        less_tabs();
                        fill_tabs();
                        CHANGE_COLOR_BLUE;
                        printf("LEAVING ");
                        CHANGE_COLOR_WHITE;
                        printf("%s::%s\n", file, function);
                }
        }
}

FunctionEnter::FunctionEnter(int level, const char* file, const char* function)
                : m_file(file), m_function(function), lvl(level)
{
        print_enter(lvl, m_file, m_function);
}

FunctionEnter::~ FunctionEnter( )
{
        print_exit(lvl, m_file, m_function);
}

ConstructorEnter::ConstructorEnter(int level, const char* /*file*/, const char* function)
                : m_function(function), lvl(level)
{
        using namespace TraversoDebugger;
        if (get_debug_level()>=lvl) {
                if (is_logging()) {
                        QString output = get_tabs() + "ENTERING " + QString(m_function) + " (CONSTRUCTOR)\n";
                        log(output);
                        more_tabs();
                } else {
                        fill_tabs();
                        CHANGE_COLOR_GREEN;
                        printf("ENTERING ");
                        CHANGE_COLOR_WHITE;
                        printf("%s",m_function);
                        CHANGE_COLOR_CYAN;
                        printf(" (CONSTRUCTOR)");
                        CHANGE_COLOR_WHITE;
                        printf("\n");
                        more_tabs();
                }
        }
}

ConstructorEnter::~ ConstructorEnter( )
{
        using namespace TraversoDebugger;
        if (get_debug_level()>=lvl) {
                if (is_logging()) {
                        less_tabs();
                        QString output = get_tabs() + "LEAVING " + QString(m_function) + " (CONSTRUCTOR)\n";
                        log(output);
                } else {
                        less_tabs();
                        fill_tabs();
                        CHANGE_COLOR_BLUE;
                        printf("LEAVING ");
                        CHANGE_COLOR_WHITE;
                        printf("%s", m_function);
                        CHANGE_COLOR_CYAN;
                        printf(" (CONSTRUCTOR)");
                        CHANGE_COLOR_WHITE;
                        printf("\n");
                }
        }
}

DestructorEnter::DestructorEnter(int level, const char* /*file*/, const char* function)
                : m_function(function), lvl(level)
{
        using namespace TraversoDebugger;
        if (get_debug_level()>=lvl) {
                if (is_logging()) {
                        QString output = get_tabs() + "ENTERING " + QString(m_function) + " (DESTRUCTOR)\n";
                        log(output);
                        more_tabs();
                } else {
                        fill_tabs();
                        CHANGE_COLOR_GREEN;
                        printf("ENTERING ");
                        CHANGE_COLOR_WHITE;
                        printf("%s", m_function);
                        CHANGE_COLOR_CYAN;
                        printf(" (DESTRUCTOR)");
                        CHANGE_COLOR_WHITE;
                        printf("\n");
                        more_tabs();
                }
        }
}

DestructorEnter::~ DestructorEnter( )
{
        using namespace TraversoDebugger;
        if (get_debug_level()>=lvl) {
                if (is_logging()) {
                        less_tabs();
                        QString output = get_tabs() + "LEAVING " + QString(m_function) + " (DESTRUCTOR)\n";
                        log(output);
                } else {
                        less_tabs();
                        fill_tabs();
                        CHANGE_COLOR_BLUE;
                        printf("LEAVING ");
                        CHANGE_COLOR_WHITE;
                        printf("%s", m_function);
                        CHANGE_COLOR_CYAN;
                        printf(" (DESTRUCTOR)");
                        CHANGE_COLOR_WHITE;
                        printf("\n");
                }
        }
}
#endif

//eof

