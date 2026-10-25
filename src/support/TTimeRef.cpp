#include <QTime>
#include "TTimeRef.h"

#include <QLocale>
#include <QRegularExpression>

#include <limits.h>
#include <cmath>
#include <numeric>

TTimeRef::TTimeRef() {
    m_universalFrame = 0;
}

TTimeRef::TTimeRef(nframes_t frame, uint rate)
    : m_universalFrame(universal_frames(frame, rate))
{
}

TTimeRef::TTimeRef(uint64_t frame, uint rate)
    : m_universalFrame(universal_frames(frame, rate))
{
}

// The whole seconds and the rest separately, so the multiplication of the
// rest cannot overflow (the whole seconds only after some ten thousand
// years); the rest rounded up, so frame_at_or_before() gives the frame back
qint64 TTimeRef::universal_frames(uint64_t frames, uint rate)
{
    Q_ASSERT(rate != 0);
    const uint64_t universal = uint64_t(UNIVERSAL_SAMPLE_RATE);
    const uint64_t seconds = frames / rate;
    const uint64_t rest = frames % rate;
    return qint64(seconds * universal + (rest * universal + rate - 1) / rate);
}

TTimeRef::TTimeRef(qreal frame, uint rate) {
    m_universalFrame = qint64((qreal(UNIVERSAL_SAMPLE_RATE) / rate) * frame);
}

qint64 TTimeRef::frame_at_or_before(const TTimeRef& time, uint rate, double* fraction)
{
    Q_ASSERT(rate != 0);
    const qint64 universal = time.m_universalFrame;
    Q_ASSERT_X(universal <= LLONG_MAX / qint64(rate) && universal >= LLONG_MIN / qint64(rate),
               "TTimeRef::frame_at_or_before", "time * rate does not fit in 64 bits");

    // frame = floor(universal * rate / UNIVERSAL_SAMPLE_RATE), remainder >= 0
    const qint64 scaled = universal * qint64(rate);
    qint64 frame = scaled / UNIVERSAL_SAMPLE_RATE;
    qint64 remainder = scaled % UNIVERSAL_SAMPLE_RATE;
    if (remainder < 0) {
        frame -= 1;
        remainder += UNIVERSAL_SAMPLE_RATE;
    }
    if (fraction) {
        *fraction = double(remainder) / double(UNIVERSAL_SAMPLE_RATE);
    }
    return frame;
}

qint64 TTimeRef::frame_at_or_after(const TTimeRef& time, uint rate)
{
    double fraction;
    const qint64 frame = frame_at_or_before(time, rate, &fraction);
    return fraction > 0.0 ? frame + 1 : frame;
}

qint64 TTimeRef::frame_nearest(const TTimeRef& time, uint rate)
{
    // floor(x + 1/2). The fraction is a remainder below UNIVERSAL_SAMPLE_RATE
    // divided by it, so its compare with a half is exact.
    double fraction;
    const qint64 frame = frame_at_or_before(time, rate, &fraction);
    return fraction >= 0.5 ? frame + 1 : frame;
}

double TTimeRef::to_frames(const TTimeRef& duration, uint rate)
{
    Q_ASSERT(rate != 0);
    return double(duration.m_universalFrame) * double(rate) / double(UNIVERSAL_SAMPLE_RATE);
}

qint64 TTimeRef::frames_covering(qint64 count, uint fromRate, uint toRate)
{
    Q_ASSERT(fromRate != 0 && count >= 0);
    Q_ASSERT_X(count <= LLONG_MAX / qint64(toRate == 0 ? 1 : toRate), "TTimeRef::frames_covering", "count * toRate does not fit in 64 bits");
    const qint64 scaled = count * qint64(toRate);
    return scaled / qint64(fromRate) + (scaled % qint64(fromRate) != 0 ? 1 : 0);
}

TTimeRef TTimeRef::common_frame_at_or_before(const TTimeRef& time, uint rateA, uint rateB)
{
    Q_ASSERT(rateA != 0 && rateB != 0);
    // The frames of both rates coincide every UNIVERSAL_SAMPLE_RATE / gcd
    // universal frames: rateA / gcd frames of the one, rateB / gcd of the
    // other
    const qint64 common = std::gcd(qint64(rateA), qint64(rateB));
    if (UNIVERSAL_SAMPLE_RATE % common != 0) {
        return time;
    }
    const qint64 period = UNIVERSAL_SAMPLE_RATE / common;
    qint64 rest = time.m_universalFrame % period;
    if (rest < 0) {
        rest += period;
    }
    return TTimeRef(time.m_universalFrame - rest);
}

// The whole seconds and the rest are converted separately: the
// multiplication of the rest cannot overflow, and the whole seconds only
// for more than ten thousand years
TTimeRef TTimeRef::from_nanoseconds(qint64 nanoseconds)
{
    const qint64 nanosecondsPerSecond = 1000000000;
    const qint64 seconds = nanoseconds / nanosecondsPerSecond;
    const qint64 rest = nanoseconds % nanosecondsPerSecond;

    return TTimeRef(seconds * UNIVERSAL_SAMPLE_RATE + rest * UNIVERSAL_SAMPLE_RATE / nanosecondsPerSecond);
}

TTimeRef TTimeRef::from_milliseconds(double milliseconds)
{
    return seconds_to_timeref(milliseconds / 1000.0);
}

TTimeRef TTimeRef::max_length()
{
    return TTimeRef(LLONG_MAX);
}

TTimeRef::TTimeRef(qint64 position)
    : m_universalFrame(position)
{

}

TTimeRef::TTimeRef(double position)
    : m_universalFrame(std::floor(position))
{

}
QString TTimeRef::timeref_to_hms(const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("-- : -- : --");
    }

    qint64 remainder;
    int hours, mins, secs;

    qint64 universalframe = ref.universal_frame();

    hours = (int) (universalframe / TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    remainder = qint64(universalframe - (hours * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE));
    mins = (int) (remainder / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE ));
    remainder -= mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE;
    secs = (int) (remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE);
    QString spos("%1:%2:%3");
    return spos.arg(hours, 2, 10, QLatin1Char('0')).arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));

}

QString TTimeRef::timeref_to_ms(const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("-- : --");
    }

    qint64 remainder;
    int mins, secs;

    qint64 universalframe = ref.universal_frame();

    mins = (int) (universalframe / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE ));
    remainder = (long unsigned int) (universalframe - (mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE));
    secs = (int) (remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE);
    QString spos("%1:%2");
    return spos.arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));
}

// TTimeRef to MM:SS.99 (hundredths)
QString TTimeRef::timeref_to_ms_2 (const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("-- : -- . --");
    }

    qint64 remainder;
    int mins, secs, frames;

    qint64 universalframe = ref.universal_frame();

    mins = universalframe / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = universalframe - ( mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    frames = remainder * 100 / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    QString spos("%1:%2%3%4");
    return spos.arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(QLocale::system().decimalPoint()).arg(frames, 2, 10, QLatin1Char('0'));
}

// TTimeRef to MM:SS.999 (ms)
QString TTimeRef::timeref_to_ms_3(const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("-- : -- . ---");
    }

    qint64 remainder;
    int mins, secs, frames;

    qint64 universalframe = ref.universal_frame();
    QString spos("%1:%2%3%4");
    if (universalframe < 0) {
        universalframe *= -1;
        spos.prepend("-");
    }

    mins = universalframe / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = universalframe - ( mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    frames = remainder * 1000 / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    return spos.arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(QLocale::system().decimalPoint()).arg(frames, 3, 10, QLatin1Char('0'));
}

// Frame to MM:SS,75 (75ths of a second, for CD burning)
QString TTimeRef::timeref_to_cd (const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("--:--:--");
    }

    qint64 remainder;
    int mins, secs, frames;

    qint64 universalframe = ref.universal_frame();

    mins = universalframe / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = universalframe - ( mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    frames = remainder * 75 / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    QString spos("%1:%2:%3");
    return spos.arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(frames, 2, 10, QLatin1Char('0'));
}

// Frame to HH:MM:SS,75 (75ths of a second, for CD burning)
QString TTimeRef::timeref_to_cd_including_hours (const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("-- : -- : -- , --");
    }

    qint64 remainder;
    int hours, mins, secs, frames;

    qint64 universalframe = ref.universal_frame();

    hours = int(universalframe / TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    remainder = qint64(universalframe - (hours * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE));
    mins = (int) (remainder / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE ));
    remainder -= mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE;
    secs = (int) (remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE);
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    frames = remainder * 75 / TTimeRef::UNIVERSAL_SAMPLE_RATE;

    QString spos("%1:%2:%3,%4");
    return spos.arg(hours, 2, 10, QLatin1Char('0')).arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(frames, 2, 10, QLatin1Char('0'));
}

QString TTimeRef::timeref_to_text(const TTimeRef & ref, qint64 scalefactor)
{
    if (scalefactor >= 512*640) {
        return TTimeRef::timeref_to_ms_2(ref);
    } else {
        return TTimeRef::timeref_to_ms_3(ref);
    }
}

TTimeRef TTimeRef::qtime_to_timeref(const QTime & time)
{
    TTimeRef ref(time.hour() * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE + time.minute() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE + time.second() * TTimeRef::UNIVERSAL_SAMPLE_RATE + (time.msec() * TTimeRef::UNIVERSAL_SAMPLE_RATE) / 1000);
    return ref;
}

TTimeRef TTimeRef::seconds_to_timeref(double seconds)
{
    Q_ASSERT(std::isfinite(seconds));
    if (!std::isfinite(seconds)) {
        return TTimeRef();
    }

    // double(LLONG_MAX) is exactly 2^63, every smaller double fits in a qint64.
    // LLONG_MIN is reserved for INVALID.
    const double universalFrame = seconds * UNIVERSAL_SAMPLE_RATE;
    if (universalFrame >= double(LLONG_MAX)) {
        return TTimeRef(qint64(LLONG_MAX));
    }
    if (universalFrame <= -double(LLONG_MAX)) {
        return TTimeRef(qint64(-LLONG_MAX));
    }

    return TTimeRef(qint64(std::llround(universalFrame)));
}

QTime TTimeRef::timeref_to_qtime(const TTimeRef& ref)
{
    qint64 remainder;
    int hours, mins, secs, msec;

    qint64 universalframe = ref.universal_frame();

    hours = universalframe / (TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    remainder = universalframe - (hours * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    mins = remainder / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = remainder - (mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    msec = remainder * 1000 / TTimeRef::UNIVERSAL_SAMPLE_RATE;

    QTime time(hours, mins, secs, msec);
    return time;
}


TTimeRef TTimeRef::msms_to_timeref(QString str)
{
    TTimeRef out;
    static QRegularExpression expression("[;,.:]");
    QStringList lst = str.simplified().split(expression, Qt::SkipEmptyParts);

    if (lst.size() >= 1) out += TTimeRef(lst.at(0).toInt() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 2) out += TTimeRef(lst.at(1).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 3) out += TTimeRef(lst.at(2).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE / 1000);

    return out;
}

TTimeRef TTimeRef::cd_to_timeref(QString str)
{
    TTimeRef out;
    static QRegularExpression expression("[;,.:]");
    QStringList lst = str.simplified().split(expression, Qt::SkipEmptyParts);

    if (lst.size() >= 1) out += TTimeRef(lst.at(0).toInt() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 2) out += TTimeRef(lst.at(1).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 3) out += TTimeRef(lst.at(2).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE / 75);

    return out;
}

TTimeRef TTimeRef::cd_to_timeref_including_hours(QString str)
{
    TTimeRef out;
    static QRegularExpression expression("[;,.:]");
    QStringList lst = str.simplified().split(expression, Qt::SkipEmptyParts);

    if (lst.size() >= 1) out += TTimeRef(lst.at(0).toInt() * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 2) out += TTimeRef(lst.at(1).toInt() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 3) out += TTimeRef(lst.at(2).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 4) out += TTimeRef(lst.at(3).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE / 75);

    return out;
}

