/*
    Copyright (C) 2005-2007 Remon Sijrier

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

#include "Utils.h"
#include "Mixer.h"

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QPixmapCache>
#include <QRegularExpression>
#include <QLocale>
#include <QChar>
#include <QTranslator>
#include <QDir>
#include <cmath>

TTimeRef msms_to_timeref(QString str)
{
    TTimeRef out;
    static QRegularExpression expression("[;,.:]");
    QStringList lst = str.simplified().split(expression, Qt::SkipEmptyParts);

    if (lst.size() >= 1) out += TTimeRef(lst.at(0).toInt() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 2) out += TTimeRef(lst.at(1).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 3) out += TTimeRef(lst.at(2).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE / 1000);

	return out;
}

TTimeRef cd_to_timeref(QString str)
{
    TTimeRef out;
    static QRegularExpression expression("[;,.:]");
    QStringList lst = str.simplified().split(expression, Qt::SkipEmptyParts);

    if (lst.size() >= 1) out += TTimeRef(lst.at(0).toInt() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 2) out += TTimeRef(lst.at(1).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 3) out += TTimeRef(lst.at(2).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE / 75);

	return out;
}

TTimeRef cd_to_timeref_including_hours(QString str)
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

QString coefficient_to_dbstring ( float coeff, int decimals)
{
	float db = coefficient_to_dB ( coeff );

	QString gainIndB;

	if (std::fabs(db) < (1/::pow(10, decimals))) {
		db = 0.0f;
	}

	if ( db < -99 )
		gainIndB = "- INF";
	else if ( db < 0 )
		gainIndB = "- " + QByteArray::number ( ( -1 * db ), 'f', decimals ) + " dB";
	else if ( db > 0 )
		gainIndB = "+ " + QByteArray::number ( db, 'f', decimals ) + " dB";
	else {
		gainIndB = "  " + QByteArray::number ( db, 'f', decimals ) + " dB";
	}

	return gainIndB;
}

qint64 create_id( )
{
	int r = rand();
	QDateTime time = QDateTime::currentDateTime();
    uint timeValue = time.toSecsSinceEpoch();
	qint64 id = timeValue;
	id *= 1000000000;
	id += r;

	return id;
}

QDateTime extract_date_time(qint64 id)
{
    QDateTime time = QDateTime::fromSecsSinceEpoch(id / 1000000000);
    return time;
}

QPixmap find_pixmap ( const QString & pixname )
{
	QPixmap pixmap;

    if ( ! QPixmapCache::find( pixname, &pixmap ) )
	{
		pixmap = QPixmap ( pixname );
		QPixmapCache::insert ( pixname, pixmap );
	}

	return pixmap;
}

QString timeref_to_hms(const TTimeRef& ref)
{
	qint64 remainder;
	int hours, mins, secs;

	qint64 universalframe = ref.universal_frame();

    hours = (int) (universalframe / TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    remainder = qint64(universalframe - (hours * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE));
    mins = (int) (remainder / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE ));
    remainder -= mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE;
    secs = (int) (remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE);
    QString spos("%1:%2%3");
    return spos.arg(hours, 2, 10, QLatin1Char('0')).arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));

}

QString timeref_to_ms(const TTimeRef& ref)
{
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
QString timeref_to_ms_2 (const TTimeRef& ref)
{
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
QString timeref_to_ms_3(const TTimeRef& ref)
{
	qint64 remainder;
	int mins, secs, frames;

	qint64 universalframe = ref.universal_frame();

    mins = universalframe / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = universalframe - ( mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    frames = remainder * 1000 / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    QString spos("%1:%2%3%4");
    return spos.arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(QLocale::system().decimalPoint()).arg(frames, 3, 10, QLatin1Char('0'));
}

// Frame to MM:SS:75 (75ths of a second, for CD burning)
QString timeref_to_cd (const TTimeRef& ref)
{
	qint64 remainder;
	int mins, secs, frames;

	qint64 universalframe = ref.universal_frame();

    mins = universalframe / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = universalframe - ( mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    frames = remainder * 75 / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    QString spos("%1:%2%3");
    return spos.arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(frames, 2, 10, QLatin1Char('0'));
}

// Frame to HH:MM:SS,75 (75ths of a second, for CD burning)
QString timeref_to_cd_including_hours (const TTimeRef& ref)
{
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

    QString spos("%1:%2%3%4");
    return spos.arg(hours, 2, 10, QLatin1Char('0')).arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(frames, 2, 10, QLatin1Char('0'));
}

QString timeref_to_text(const TTimeRef & ref, qint64 scalefactor)
{
	if (scalefactor >= 512*640) {
		return timeref_to_ms_2(ref);
	} else {
		return timeref_to_ms_3(ref);
	}
}


QStringList find_qm_files()
{
	QDir dir(":/translations");
	QStringList fileNames = dir.entryList(QStringList("*.qm"), QDir::Files, QDir::Name);
	QMutableStringListIterator i(fileNames);
	while (i.hasNext()) {
		i.next();
		i.setValue(dir.filePath(i.value()));
	}
	return fileNames;
}

QString language_name_from_qm_file(const QString& lang)
{
	QTranslator translator;
    if (translator.load(lang)) {
        return translator.translate("LanguageName", "English", "The name of this Language, e.g. German would be Deutch");
    }

    return QString("Failed to load language name from qm file");
}

bool t_MetaobjectInheritsClass(const QMetaObject *mo, const QString& className)
{
	while (mo) {
		if (mo->className() == className) {
			return true;
		}
		mo = mo->superClass();
	}
	return false;
}

