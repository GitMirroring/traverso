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

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QPixmapCache>
#include <QChar>
#include <QTranslator>
#include <QDir>
#include <cmath>
#include <random>
#include "TTimeRef.h"

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

double TUtils::randomNumberBetween(int start, int end)
{
    std::mt19937_64 rng;
    // initialize the random number generator with time-dependent seed
    uint64_t timeSeed = TTimeRef::get_nanoseconds_since_epoch();
    std::seed_seq ss{uint32_t(timeSeed & 0xffffffff), uint32_t(timeSeed>>32)};
    rng.seed(ss);
    // initialize a uniform distribution between start and end
    std::uniform_real_distribution<double> unif(start, end);
    return unif(rng);
}
