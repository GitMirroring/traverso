/*
Copyright (C) 2006 Remon Sijrier, Nicola Doebelin

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

$Id: TFadeCurve.cpp,v 1.36 2008/11/07 10:43:08 r_sijrier Exp $
*/

#include "TFadeCurve.h"

#include <QFile>
#include <algorithm>
#include <cmath>
#include "Fade.h"
#include "TCommand.h"
#include "TAddRemoveCommand.h"
#include "TAudioDevice.h"
#include "AudioBus.h"
#include "TFadeShape.h"
#include "TLocation.h"
#include "TRtTransaction.h"
#include "TTimeLineEntity.h"
#include "Utils.h"
#include "Debugger.h"

QStringList TFadeCurve::defaultShapes = QStringList() << "Fastest" << "Fast" << "Linear"  << "Slow" << "Slowest";

TFadeCurve::TFadeCurve(TContextItem *parent, FadeType fadeType, TRtTransaction& transaction)
    : TContextItem(parent)
    , m_type(fadeType)
    , m_state([this]() { return create_state(); })
{
    QObject::tr("TFadeCurve");

    connect(this, &TFadeCurve::bendValueChanged, this, &TFadeCurve::stateChanged);
    connect(this, &TFadeCurve::strengthValueChanged, this, &TFadeCurve::stateChanged);
    connect(this, &TFadeCurve::modeChanged, this, &TFadeCurve::stateChanged);

    solve_shape(transaction);
}

QDomNode TFadeCurve::get_state( QDomDocument doc )
{
    QDomElement node = doc.createElement(fade_type_to_string());
    node.setAttribute("bendfactor", m_bendFactor);
    node.setAttribute("strengthfactor", m_strenghtFactor);
    node.setAttribute("bypassed", m_bypass);
    node.setAttribute("range", m_length);
    node.setAttribute("mode", m_mode);
    node.setAttribute("raster", m_raster);

    QStringList controlPointsList;

    for (const QPointF& point : m_controlPoints) {
        controlPointsList << QString::number(point.x()).append(",").append(QString::number(point.y()));
    }

    node.setAttribute("controlpoints",  controlPointsList.join(";"));

    return node;
}

// The stored length, 0 when it is not a number
static double stored_length(const QDomElement& e)
{
    QString rangestring = e.attribute("range", "0");
    if (rangestring == "nan" || rangestring == "inf") {
        printf("FadeCurve::set_state: stored range was not a number!\n");
        return 0.0;
    }
    return rangestring.toDouble();
}

int TFadeCurve::set_state( const QDomNode & node )
{
    QDomElement e = node.toElement();
    m_bendFactor = e.attribute( "bendfactor", "0.0" ).toDouble();
    m_strenghtFactor = e.attribute( "strengthfactor", "0.5" ).toDouble();
    m_bypass = e.attribute( "bypassed", "0" ).toInt();
    m_mode = e.attribute( "mode", "2" ).toInt();
    m_raster = e.attribute( "raster", "0" ).toInt();

    // The control points follow from the mode, bend and strength, the stored
    // ones are only written for older versions

    TRtTransaction transaction;

    set_length(stored_length(e), transaction);
    solve_shape(transaction);

    emit stateChanged();

    return 1;
}


void TFadeCurve::process(const TAudioBuffer &curveBuffer, AudioBus *bus, const TTimeRef& startLocation, const TTimeRef& endLocation, nframes_t nframes)
{
    Q_ASSERT(bus->get_channel_count() == 2);
    Q_ASSERT(m_parentTimeLineEntity != nullptr);
    Q_ASSERT(m_parentTimeLineEntity->get_location() != nullptr);

    const TFadeState* state = m_state.rt_snapshot();
    if (!state || state->bypassed || state->length <= 0.0) {
        return;
    }

    TLocation* parentLocation = m_parentTimeLineEntity->get_location();
    uint outputRate = audiodevice().get_sample_rate();
    uint framesToProcess = nframes;
    uint channelCount = bus->get_channel_count();

    TTimeRef fadeStartLocation, fadeEndLocation, fadeLocation;
    TTimeRef fadeRange = TTimeRef(state->length);
    TTimeRef upperRange = startLocation + TTimeRef(nframes, outputRate);

    if (m_type == FadeIn) {
        fadeStartLocation = parentLocation->get_start();
    } else {
        fadeStartLocation = parentLocation->get_end() - fadeRange;
    }

    fadeEndLocation = fadeStartLocation + fadeRange;

    if ((startLocation >= fadeEndLocation || (endLocation <= fadeStartLocation))) {
        return;
    }

    nframes_t offset = 0;

    if (startLocation < fadeStartLocation) {
        offset = TTimeRef::to_frame(fadeStartLocation - startLocation, outputRate);
        Q_ASSERT(offset <= nframes);
        fadeLocation = TTimeRef();
        framesToProcess = framesToProcess - offset;
    } else {
        fadeLocation = (startLocation - fadeStartLocation);
    }

    if (fadeEndLocation < upperRange) {
        framesToProcess -= TTimeRef::to_frame(upperRange - fadeEndLocation, outputRate);
    }

    if (framesToProcess == 0) {
        return;
    }

    upperRange = fadeLocation + TTimeRef(framesToProcess, outputRate);

    // The shape runs from 0 to 1 over the length of the fade
    state->shape->get_vector(double(fadeLocation.universal_frame()) / state->length,
                             double(upperRange.universal_frame()) / state->length,
                             curveBuffer.get_data(framesToProcess), framesToProcess, m_rtLookupCache);

    for (uint chan = 0; chan < channelCount; ++chan) {
        TAudioBuffer &buf = bus->get_buffer(chan);
        buf.set_data_start_offset(offset);
        for (nframes_t frame = 0; frame < framesToProcess; ++frame) {
            buf[frame] *= curveBuffer.at(frame);
        }
        buf.set_data_start_offset(0);
    }
}

void TFadeCurve::set_shape(const QString& shapeName)
{
    QDomDocument doc("FadeShapes");

    if (defaultShapes.contains(shapeName)) {
        QFile file(":/fadeshapes");

        if (!file.open(QIODevice::ReadOnly)) {
            printf("Could not open fadeshapes file!!\n");
            return;
        }
        if (!doc.setContent(&file)) {
            file.close();
            printf("Could not set QDomDocument content!\n");
            return;
        }
        file.close();
    } else {
        // Load from custom saved fades
    }


    QDomElement root = doc.documentElement();
    QDomNode node = root.firstChildElement(fade_type_to_string());

    QDomElement fadeElement = node.firstChildElement(shapeName);

    if (fadeElement.isNull()) {
        printf("%s does not exist?????\n", shapeName.toLatin1().data());
        return;
    }

    const float bend = fadeElement.attribute( "bendfactor", "0.0" ).toFloat();
    const float strength = fadeElement.attribute( "strengthfactor", "0.5" ).toFloat();
    const float origBend = m_bendFactor;
    const float origStrength = m_strenghtFactor;

    TCommand::process_command(new TAddRemoveCommand(
        this,
        nullptr,
        true,
        [this, bend, strength](TRtTransaction& transaction) {
            set_bend_factor(bend, transaction);
            set_strength_factor(strength, transaction);
        },
        [this, origBend, origStrength](TRtTransaction& transaction) {
            set_bend_factor(origBend, transaction);
            set_strength_factor(origStrength, transaction);
        },
        tr("Fade Preset")));

    emit stateChanged();
}

void TFadeCurve::solve_shape(TRtTransaction& transaction)
{
    m_controlPoints = TFadeShape::control_points(m_type, m_mode, m_bendFactor, m_strenghtFactor);
    m_shape = TCurveSnapshot::create(TFadeShape::points(m_type, m_controlPoints), 1.0);
    m_state.mark_changed(transaction);
}

std::shared_ptr<const TFadeState> TFadeCurve::create_state() const
{
    return std::make_shared<TFadeState>(TFadeState{m_shape, m_length, m_bypass});
}

void TFadeCurve::set_length(double length, TRtTransaction& transaction)
{
    length = TFadeShape::effective_length(length);

    if (TraversoDAW::Float::compare(length, m_length)) {
        return;
    }

    m_length = length;
    m_state.mark_changed(transaction);

    emit lengthChanged();
}

void TFadeCurve::set_bend_factor(float factor, TRtTransaction& transaction)
{
    if (factor > 1.0)
        factor = 1.0;
    if (factor < 0.0)
        factor = 0.0;

    m_bendFactor = factor;
    solve_shape(transaction);

    emit bendValueChanged();
}

void TFadeCurve::set_strength_factor(float factor, TRtTransaction& transaction)
{
    if (factor > 1.0)
        factor = 1.0;
    if (factor < 0.0)
        factor = 0.0;

    m_strenghtFactor = factor;
    solve_shape(transaction);

    emit strengthValueChanged();
}

QList< QPointF > TFadeCurve::get_control_points( ) const
{
    return QList<QPointF>(m_controlPoints.begin(), m_controlPoints.end());
}

TCommand* TFadeCurve::set_mode( )
{
    if (m_mode < 2) {
        return new FadeMode(this, m_mode, m_mode+1);
    } else {
        return new FadeMode(this, m_mode, 0);
    }
}

void TFadeCurve::set_mode(int m, TRtTransaction& transaction)
{
    m_mode = m;
    solve_shape(transaction);

    emit modeChanged();
}

QString TFadeCurve::fade_type_to_string() const
{
    if(m_type == FadeIn) {
        return QString("FadeIn");
    }
    if (m_type == FadeOut) {
        return QString("FadeOut");
    }
    return QString("Unknown Fade Type to String Conversion");
}

void TFadeCurve::get_vector(double x0, double x1, const TAudioBuffer &audioBuffer, nframes_t veclen)
{
    float* vec = audioBuffer.get_data(veclen);

    if (m_length <= 0.0) {
        std::fill(vec, vec + veclen, 1.0f);
        return;
    }

    m_shape->get_vector(x0 / m_length, x1 / m_length, vec, veclen, m_guiLookupCache);
}

void TFadeCurve::get_columns(double x0, double columnWidth, int count, TCurveColumn* columns)
{
    if (m_length <= 0.0) {
        std::fill(columns, columns + count, TCurveColumn{1.0, 1.0, 0.0, 1.0, 0.0, 1.0});
        return;
    }

    // Positions inside a column are fractions of it, so they need no scaling
    m_shape->get_columns(x0 / m_length, columnWidth / m_length, count, columns, m_guiLookupCache);
}

TCommand* TFadeCurve::toggle_bypass( )
{
    m_bypass = !m_bypass;

    TRtTransaction transaction;
    m_state.mark_changed(transaction);

    emit stateChanged();
    return 0;
}

TCommand* TFadeCurve::toggle_raster( )
{
    m_raster = ! m_raster;

    emit rasterChanged();
    return 0;
}
