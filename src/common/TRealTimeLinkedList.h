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

#ifndef TREALTIMELINKEDLIST_H
#define TREALTIMELINKEDLIST_H


#include "qassert.h"
template<class T>
class TRealTimeLinkedList
{

public:
    TRealTimeLinkedList() : m_size(0), m_head(nullptr), m_last(nullptr) {}
    ~TRealTimeLinkedList() {}


    T first() const {return m_head;}
    T last() const {return m_last;}
    int size() const {return m_size;}
    void clear() {m_head = nullptr; m_last = nullptr; m_size=0;}
    bool isEmpty() {return m_size == 0 ? true : false;}


    // T = O(1)
    inline void prepend(T item)
    {
        item->next = m_head;
        m_head = item;
        if (!m_size) {
            m_last = item;
            m_last->next = nullptr;
        }
        m_size++;
        Q_ASSERT(m_last == slow_last());
    }

    // T = O(1)
    inline void append(T item)
    {
        if(m_head) {
            m_last->next = item;
            m_last = item;
        } else {
            m_head = item;
            m_last = item;
        }

        m_last->next = nullptr;
        m_size++;

        Q_ASSERT(m_last == slow_last());
    }

    // T = O(n)
    inline int remove(T item)
    {
        Q_ASSERT(item);

        if (!m_size) {
            return 0;
        }

        if(m_head == item) {
            m_head = m_head->next;
            m_size--;
            if (m_size == 0) {
                m_last = m_head = nullptr;
            }
            Q_ASSERT(m_last == slow_last());
            return 1;
        }

        T q, r;
        Q_ASSERT(m_head);
        r = m_head;
        q = m_head->next;

        while( q!=nullptr ) {
            if( q == item ) {
                r->next = q->next;
                m_size--;
                if (!q->next) {
                    m_last = r;
                    m_last->next = nullptr;
                }
                Q_ASSERT(m_last == slow_last());
                return 1;
            }

            r = q;
            q = q->next;
        }

        return 0;
    }

    // T = O(1)
    inline void insert(T after, T item)
    {
        Q_ASSERT(after);
        Q_ASSERT(item);

        T temp;

        temp = item;
        temp->next = after->next;
        after->next = temp;

        if (after == m_last) {
            m_last = item;
            m_last->next = nullptr;
        }
        m_size++;

        Q_ASSERT(m_last == slow_last());
    }

    // T = O(n)
    inline T slow_last() const
    {
        if (!m_size) {
            return nullptr;
        }

        T last = m_head;

        while(last->next) {
            last = last->next;
        }

        return last;
    }

    // T = O(n)
    inline void add_and_sort(T node)
    {
        if (!m_size) {
            append(node);
        } else {
            T q = m_head;
            if (*node < *q) {
                prepend(node);
            } else {
                T afternode = q;
                while (q) {
                    if ( ! (*node < *q) ) {
                        afternode = q;
                    } else {
                        break;
                    }
                    q = q->next;
                }
                insert(afternode, node);
            }
        }

        Q_ASSERT(m_last == slow_last());
    }

    inline int indexOf(T node)
    {
        Q_ASSERT(node);
        int index = 0;

        T q = m_head;
        while (q) {
            if (q == node) {
                return index;
            }
            ++index;
            q = q->next;
        }
        return -1;
    }

    inline T at(int i)
    {
        Q_ASSERT(i >= 0);
        Q_ASSERT(i < m_size);

        int loopcounter = 0;
        T q = m_head;
        while (q) {
            if (loopcounter == i) {
                return q;
            }
            q = q->next;
            ++loopcounter;
        }
        return nullptr;
    }

    inline void sort(T node)
    {
        if (remove(node)) {
            add_and_sort(node);
        }
    }
private:
    int m_size;
    T m_head;
    T m_last;

};

#endif // TREALTIMELINKEDLIST_H
