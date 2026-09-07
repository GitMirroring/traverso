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
#pragma once
#include <QPixmap>
#include <QDateTime>

#define QS_C(x) x.toUtf8().data()

class QString;

namespace TraversoDAW {
class Utils
{
public:
    static double randomNumberBetween(int start, int end);
    static inline unsigned int is_power_of_two (unsigned int n)
    {
        return !(n & (n - 1));
    }

    static QDateTime extract_date_time(qint64 id);

    static qint64 create_id();

    static QStringList find_qm_files();
    static QString language_name_from_qm_file(const QString& lang);


    static inline int cnt_bits(unsigned long val, int & highbit)
    {
        int cnt = 0;
        highbit = 0;
        while (val) {
            if (val & 1) cnt++;
            val>>=1;
            highbit++;
        }
        return cnt;
    }

    // returns the next power of two greater or equal to val
    static inline long nearest_power_of_two(unsigned long val, int& highbit)
    {
        if (cnt_bits(val, highbit) > 1) {
            return 1<<highbit;
        }
        return val;
    }

    static QPixmap find_pixmap(const QString& pixname);

};

class Float {
public:
     static inline bool fuzzy_compare(float a, float b)
    {
        bool az = qFuzzyIsNull(a);
        bool bz = qFuzzyIsNull(b);
        if (az && bz) {
            return true;
        }
        if (az || bz) {
            return false;
        }
        return qFuzzyCompare(a, b);
    }

    static inline bool fuzzy_compare(double a, double b)
    {
        bool az = qFuzzyIsNull(a);
        bool bz = qFuzzyIsNull(b);
        if (az && bz) {
            return true;
        }
        if (az || bz) {
            return false;
        }
        return qFuzzyCompare(a, b);
    }

    static inline bool fuzzy_equals_1(double a)
    {
        return qFuzzyCompare(a, 1.0);
    }

    static inline bool fuzzy_equals_1(float a)
    {
        return qFuzzyCompare(a, 1.0f);
    }

    static inline bool fuzzy_equals_0(double a)
    {
        return qFuzzyIsNull(a);
    }

    static inline bool fuzzy_equals_0(float a)
    {
        return qFuzzyIsNull(a);
    }

};

}

