#ifndef TTIMEREF_H
#define TTIMEREF_H

#include <QtGlobal>
#include <QMetaType>
#include "defines.h"



class TTimeRef {

public:
    // Universal samplerate for the frequences 22050, 32000, 44100, 88200, 96000 and 192000 Hz
    static const qint64 UNIVERSAL_SAMPLE_RATE = 28224000;
    static const qint64 ONE_HOUR_UNIVERSAL_SAMPLE_RATE = 101606400000;
    static const qint64 ONE_MINUTE_UNIVERSAL_SAMPLE_RATE = 1693440000;

    TTimeRef();
    explicit TTimeRef(qint64 position);
    explicit TTimeRef(double position);
    TTimeRef(nframes_t frame, uint rate);
    TTimeRef(qreal frame, uint rate) {
        m_position = qint64((qreal(UNIVERSAL_SAMPLE_RATE) / rate) * frame);
    }

    static TTimeRef max_length();
    static TTimeRef negative_max_length();

    static QString timeref_to_ms(const TTimeRef& ref);
    static QString timeref_to_ms_2 (const TTimeRef& ref);
    static QString timeref_to_ms_3 (const TTimeRef& ref);
    static QString timeref_to_hms(const TTimeRef& ref);

    static QString timeref_to_text(const TTimeRef& ref, qint64 scalefactor);

    static QString timeref_to_cd(const TTimeRef& ref);
    static QString timeref_to_cd_including_hours(const TTimeRef& ref);

    static TTimeRef msms_to_timeref(QString str);
    static TTimeRef cd_to_timeref(QString str);
    static TTimeRef cd_to_timeref_including_hours(QString str);


    void add_frames(nframes_t frames, uint rate) {
        m_position += ((UNIVERSAL_SAMPLE_RATE / rate) * frames);
    }

    nframes_t to_frame(uint rate) {
        Q_ASSERT(rate);
        return nframes_t(m_position / (UNIVERSAL_SAMPLE_RATE / rate));
    }

    qint64 universal_frame() const {return m_position;}

    TTimeRef& operator =(const qint64 value) {
        m_position = value;
        return *this;
    }

    TTimeRef& operator =(double value) {
        m_position = qint64(value);
        return *this;
    }

    friend int operator!=(const TTimeRef& left, const TTimeRef& right) {
        return left.m_position != right.m_position;
    }

    friend int operator!=(const TTimeRef& left, qint64 right) {
        return left.m_position != right;
    }

    friend int operator!=(const TTimeRef& left, double right) {
        return left.m_position != qint64(right);
    }

    friend TTimeRef operator-(const TTimeRef& left, const TTimeRef& right) {
        return TTimeRef(left.m_position - right.m_position);
    }

    friend TTimeRef operator-(const TTimeRef& left, qint64 right) {
        return TTimeRef(left.m_position - right);
    }

    friend TTimeRef operator-(const TTimeRef& left, double right) {
        return TTimeRef(left.m_position - qint64(right));
    }

    friend TTimeRef& operator-=(TTimeRef& left, const TTimeRef& right) {
        left.m_position -= right.m_position;
        return left;
    }

    friend TTimeRef& operator-=(TTimeRef& left, qint64 right) {
        left.m_position -= right;
        return left;
    }

    friend TTimeRef& operator-=(TTimeRef& left, double right) {
        left.m_position -= qint64(right);
        return left;
    }

    friend TTimeRef operator+(const TTimeRef& left, const TTimeRef& right) {
        return TTimeRef(left.m_position + right.m_position);
    }

    friend TTimeRef operator+(const TTimeRef& left, qint64 right) {
        return TTimeRef(left.m_position + right);
    }

    friend TTimeRef operator+(const TTimeRef& left, double right) {
        return TTimeRef(left.m_position + qint64(right));
    }

    friend TTimeRef& operator+=(TTimeRef& left, const TTimeRef& right) {
        left.m_position += right.m_position;
        return left;
    }

    friend TTimeRef& operator+=(TTimeRef& left, qint64 right) {
        left.m_position += right;
        return left;
    }

    friend TTimeRef& operator+=(TTimeRef& left, double right) {
        left.m_position += qint64(right);
        return left;
    }

    friend qreal operator/(const TTimeRef& left, const TTimeRef& right) {
        Q_ASSERT(right.m_position != 0);
        return qreal(left.m_position) / right.m_position;
    }

    friend qreal operator/(const TTimeRef& left, const qint64 right) {
        Q_ASSERT(right != 0);
        return qreal(left.m_position) / right;
    }

    friend qreal operator/(const TTimeRef& left, double right) {
        Q_ASSERT(!qFuzzyCompare(right, 0.0));
        return qreal(left.m_position) / qint64(right);
    }

    friend TTimeRef operator*(const qint64 left, TTimeRef& right) {
        return TTimeRef(left * right.m_position);
    }

    friend TTimeRef operator*(const TTimeRef& left, const TTimeRef& right) {
        return TTimeRef(left.m_position * right.m_position);
    }

    friend TTimeRef operator*(const TTimeRef& left, double right) {
        return TTimeRef(left.m_position * qint64(right));
    }

    friend int operator<(const TTimeRef& left, const TTimeRef& right) {
        return left.m_position < right.m_position;
    }

    friend int operator<(const TTimeRef& left, qint64 right) {
        return left.m_position < right;
    }

    friend int operator<(const TTimeRef& left, double right) {
        return left.m_position < qint64(right);
    }

    friend int operator<(double left, const TTimeRef& right) {
        return (qint64(left) < right.m_position);
    }

    friend int operator>(const TTimeRef& left, const TTimeRef& right) {
        return left.m_position > right.m_position;
    }

    friend int operator>(const TTimeRef& left, qint64 right) {
        return left.m_position > right;
    }

    friend int operator>(const TTimeRef& left, double right) {
        return left.m_position > qint64(right);
    }

    friend int operator>(double left, const TTimeRef& right) {
        return left > right.m_position;
    }

    friend int operator<=(const TTimeRef& left, const TTimeRef& right) {
        return left.m_position <= right.m_position;
    }

    friend int operator<=(const TTimeRef& left, qint64 right) {
        return left.m_position <= right;
    }

    friend int operator<=(const TTimeRef& left, double right) {
        return left.m_position <= qint64(right);
    }

    friend int operator>=(const TTimeRef& left, const TTimeRef& right) {
        return left.m_position >= right.m_position;
    }

    friend int operator>=(const TTimeRef& left, qint64 right) {
        return left.m_position >= right;
    }

    friend int operator>=(const TTimeRef& left, double right) {
        return left.m_position >= qint64(right);
    }

    friend int operator==(const TTimeRef& left, const TTimeRef& right) {
        return left.m_position == right.m_position;
    }

    friend int operator==(const TTimeRef& left, qint64 right) {
        return left.m_position == right;
    }

    friend int operator==(const TTimeRef& left, double right) {
        return left.m_position == qint64(right);
    }

private:
    qint64 m_position;
};

Q_DECLARE_METATYPE(TTimeRef);

#endif // TTIMEREF_H
