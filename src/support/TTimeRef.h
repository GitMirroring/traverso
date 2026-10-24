#pragma once

#include <chrono>
#include <limits>
#include "Utils.h"
#include "defines.h"
#include <QMetaType>
#include <QString>

class QTime;



class TTimeRef {

public:
    // Universal samplerate for the frequences 22050, 8000, 32000, 44100, 48000, 88200, 96000 and 192000 Hz
    static const qint64 UNIVERSAL_SAMPLE_RATE = 28224000;
    static const qint64 ONE_HOUR_UNIVERSAL_SAMPLE_RATE = 101606400000;
    static const qint64 ONE_MINUTE_UNIVERSAL_SAMPLE_RATE = 1693440000;
    static const long long INVALID = LLONG_MIN;

    TTimeRef();
    explicit TTimeRef(int position) : TTimeRef(qint64(position)) {}
    explicit TTimeRef(qint64 position);
    explicit TTimeRef(double position);
    // The time of frame frame at rate rate: exact where rate divides
    // UNIVERSAL_SAMPLE_RATE (every device rate), else the first universal
    // frame at or after it, so frame_at_or_before() gives frame back at any
    // rate. Fits 64 bits for some ten thousand years of frames.
    explicit TTimeRef(nframes_t frame, uint rate);
    explicit TTimeRef(uint64_t frame, uint rate);

    explicit TTimeRef(qreal frame, uint rate);

    static TTimeRef max_length();

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

    static TTimeRef qtime_to_timeref(const QTime& time);
    // Rounds to the nearest universal frame. Out of range values are clamped.
    static TTimeRef seconds_to_timeref(double seconds);
    // Exact for any qint64 (truncates towards zero), e.g. for elapsed time
    static TTimeRef from_nanoseconds(qint64 nanoseconds);
    // A duration in milliseconds (a setting), like seconds_to_timeref()
    static TTimeRef from_milliseconds(double milliseconds);
    static QTime timeref_to_qtime(const TTimeRef& ref);


    static inline long get_milliseconds_since_epoch()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() / 1000000;
    }

    static inline long get_microseconds_since_epoch()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() / 1000;
    }

    static inline long get_nanoseconds_since_epoch()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }

    static nframes_t to_frame(const TTimeRef& timeRef, uint sampleRate) {
        long double duration = timeRef.universal_frame() * sampleRate;
        return  std::round(duration / UNIVERSAL_SAMPLE_RATE);
    }

    // Exact conversions for any sample rate (also rates that do not divide
    // UNIVERSAL_SAMPLE_RATE), for negative times too. Frame n of a stream at
    // rate r sits at time n / r.

    // The last frame at or before time; fraction (optional) is how far time is
    // past that frame, in frames, 0 <= fraction < 1
    static qint64 frame_at_or_before(const TTimeRef& time, uint rate, double* fraction = nullptr);
    // The first frame at or after time
    static qint64 frame_at_or_after(const TTimeRef& time, uint rate);
    // The length of duration in frames, with fraction. For decisions and
    // ratios, not for positions.
    static double to_frames(const TTimeRef& duration, uint rate);
    // The last time at or before time where a frame of rateA and a frame of
    // rateB coincide (every 147 frames of 44.1 kHz and 160 of 48 kHz):
    // resampling from one rate to the other can start there without a
    // fraction of a frame. Exact when UNIVERSAL_SAMPLE_RATE is a multiple of
    // the greatest common divisor of the rates, which holds for every rate
    // it divides and for 384 kHz with most others; else time itself.
    static TTimeRef common_frame_at_or_before(const TTimeRef& time, uint rateA, uint rateB);
    // The fewest frames at toRate that cover count frames at fromRate:
    // count * toRate / fromRate, rounded up. Exact; 0 for 0.
    static qint64 frames_covering(qint64 count, uint fromRate, uint toRate);
    // A frame, or a count of frames, from the 64-bit functions above as a
    // stream's 32-bit nframes_t; a debug build checks that it fits
    static nframes_t to_nframes(qint64 frames) {
        Q_ASSERT_X(frames >= 0 && frames <= qint64(std::numeric_limits<nframes_t>::max()), "TTimeRef::to_nframes",
                   "the frames do not fit in nframes_t");
        return nframes_t(frames);
    }


    void add_frames(nframes_t frames, uint rate) {
        // Rounded per call where rate does not divide UNIVERSAL_SAMPLE_RATE,
        // so the sum of many calls drifts there; every device rate divides it
        m_universalFrame += universal_frames(frames, rate);
    }

    qint64 universal_frame() const {return m_universalFrame;}

    TTimeRef& operator =(const qint64 value) {
        m_universalFrame = value;
        return *this;
    }

    TTimeRef& operator =(double value) {
        m_universalFrame = qint64(value);
        return *this;
    }

    friend int operator!=(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame != right.m_universalFrame;
    }

    friend int operator!=(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame != right;
    }

    friend int operator!=(const TTimeRef& left, double right) {
        return left.m_universalFrame != qint64(right);
    }

    friend TTimeRef operator-(const TTimeRef& left, const TTimeRef& right) {
        return TTimeRef(left.m_universalFrame - right.m_universalFrame);
    }

    friend TTimeRef operator-(const TTimeRef& left, qint64 right) {
        return TTimeRef(left.m_universalFrame - right);
    }

    friend TTimeRef operator-(const TTimeRef& left, double right) {
        return TTimeRef(left.m_universalFrame - qint64(right));
    }

    friend TTimeRef& operator-=(TTimeRef& left, const TTimeRef& right) {
        left.m_universalFrame -= right.m_universalFrame;
        return left;
    }

    friend TTimeRef& operator-=(TTimeRef& left, qint64 right) {
        left.m_universalFrame -= right;
        return left;
    }

    friend TTimeRef& operator-=(TTimeRef& left, double right) {
        left.m_universalFrame -= qint64(right);
        return left;
    }

    friend TTimeRef operator+(const TTimeRef& left, const TTimeRef& right) {
        return TTimeRef(left.m_universalFrame + right.m_universalFrame);
    }

    friend TTimeRef operator+(const TTimeRef& left, qint64 right) {
        return TTimeRef(left.m_universalFrame + right);
    }

    friend TTimeRef operator+(const TTimeRef& left, double right) {
        return TTimeRef(left.m_universalFrame + qint64(right));
    }

    friend TTimeRef& operator+=(TTimeRef& left, const TTimeRef& right) {
        left.m_universalFrame += right.m_universalFrame;
        return left;
    }

    friend TTimeRef& operator+=(TTimeRef& left, qint64 right) {
        left.m_universalFrame += right;
        return left;
    }

    friend TTimeRef& operator+=(TTimeRef& left, double right) {
        left.m_universalFrame += qint64(right);
        return left;
    }

    friend qreal operator/(const TTimeRef& left, const TTimeRef& right) {
        Q_ASSERT(right.m_universalFrame != 0);
        return qreal(left.m_universalFrame) / right.m_universalFrame;
    }

    friend qreal operator/(const TTimeRef& left, const qint64 right) {
        Q_ASSERT(right != 0);
        return qreal(left.m_universalFrame) / right;
    }

    friend qreal operator/(const TTimeRef& left, double right) {
        Q_ASSERT(!TraversoDAW::Float::equals_0(right));
        return qreal(left.m_universalFrame) / qint64(right);
    }

    friend TTimeRef operator*(const qint64 left, TTimeRef& right) {
        return TTimeRef(left * right.m_universalFrame);
    }

    friend TTimeRef operator*(const TTimeRef& left, const TTimeRef& right) {
        return TTimeRef(left.m_universalFrame * right.m_universalFrame);
    }

    friend TTimeRef operator*(const TTimeRef& left, double right) {
        return TTimeRef(left.m_universalFrame * qint64(right));
    }

    friend int operator<(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame < right.m_universalFrame;
    }

    friend int operator<(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame < right;
    }

    friend int operator<(const TTimeRef& left, double right) {
        return left.m_universalFrame < qint64(right);
    }

    friend int operator<(double left, const TTimeRef& right) {
        return (qint64(left) < right.m_universalFrame);
    }

    friend int operator>(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame > right.m_universalFrame;
    }

    friend int operator>(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame > right;
    }

    friend int operator>(const TTimeRef& left, double right) {
        return left.m_universalFrame > qint64(right);
    }

    friend int operator>(double left, const TTimeRef& right) {
        return left > right.m_universalFrame;
    }

    friend int operator<=(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame <= right.m_universalFrame;
    }

    friend int operator<=(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame <= right;
    }

    friend int operator<=(const TTimeRef& left, double right) {
        return left.m_universalFrame <= qint64(right);
    }

    friend int operator>=(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame >= right.m_universalFrame;
    }

    friend int operator>=(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame >= right;
    }

    friend int operator>=(const TTimeRef& left, double right) {
        return left.m_universalFrame >= qint64(right);
    }

    friend int operator==(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame == right.m_universalFrame;
    }

    friend int operator==(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame == right;
    }

    friend int operator==(const TTimeRef& left, double right) {
        return left.m_universalFrame == qint64(right);
    }

private:
    static qint64 universal_frames(uint64_t frames, uint rate);

    qint64 m_universalFrame;
};

Q_DECLARE_METATYPE(TTimeRef);

