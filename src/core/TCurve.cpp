/*
Copyright (C) 2005-2006 Remon Sijrier 

Original version from Ardour curve.cc, modified
in order to fit Traverso's lockless design

Copyright (C) 2001-2003 Paul Davis 

Contains ideas derived from "Constrained Cubic Spline Interpolation" 
by CJC Kruger (www.korf.co.uk/spline.pdf).

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

#include "TCurve.h"
#include <algorithm>
#include <cmath>

#include "AudioBus.h"
#include "TCurveNode.h"
#include "TSession.h"
#include "Utils.h"
#include <TAddRemoveCommand.h>
#include "TInformUser.h"
#include "TInputEventDispatcher.h"



#include "Debugger.h"

using namespace std;

TCurve::TCurve(TContextItem* parent)
    : TContextItem(parent)
    , m_snapshot([this]() { return create_snapshot(); })
{
    PENTERCONS;
    init();
}

TCurve::TCurve(TContextItem* parent, const QDomNode& node )
    : TContextItem(parent)
    , m_snapshot([this]() { return create_snapshot(); })
{
    init();
    TCurve::set_state(node);
}

TCurve::~TCurve()
{
    qDeleteAll(m_nodes);
}

void TCurve::init( )
{
    QObject::tr("TCurve");
    QObject::tr("TCurveNode");
    m_defaultValue = 1.0;
    m_session = nullptr;
}


QDomNode TCurve::get_state(QDomDocument doc, const QString& name)
{
    PENTER3;
    QDomElement domNode = doc.createElement(name);

    QStringList nodesList;

    for (TCurveNode* cn : std::as_const(m_nodes)) {
        nodesList << QString::number(cn->get_when(), 'g', 24).append(",").append(QString::number(cn->get_value()));
    }

    if (m_nodes.size() == 0) {
        nodesList << "1," + QString::number(m_defaultValue);
    }

    domNode.setAttribute("nodes",  nodesList.join(";"));
    domNode.setAttribute("defaulvalue",  m_defaultValue);
    domNode.setAttribute("id",  get_id());


    return domNode;
}

int TCurve::set_state( const QDomNode & node )
{
    PENTER;
    QDomElement e = node.toElement();

    QStringList nodesList = e.attribute( "nodes", "" ).split(";");
    m_defaultValue = e.attribute( "defaulvalue", "1.0" ).toDouble();
    set_id(e.attribute("id", "0" ).toLongLong());

    TRtTransaction transaction;

    for (int i=0; i<nodesList.size(); ++i) {
        QStringList whenValueList = nodesList.at(i).split(",");
        double when = whenValueList.at(0).toDouble();
        double value = whenValueList.at(1).toDouble();
        TCurveNode* node = new TCurveNode(this, when, value);
        private_add_node(node, transaction);
    }

    return 1;
}

bool TCurve::is_trivial()
{
    return m_nodes.size() <= 1;
}

float TCurve::get_trivial_gain()
{
    return (m_nodes.size() == 0) ? 1.0 : m_nodes.first()->get_value();
}

int TCurve::process(
    AudioBus* audioBus,
    const TTimeRef& startlocation,
    const TTimeRef& endlocation,
    nframes_t nframes,
    uint channels,
    audio_sample_t /*makeupgain*/ // Ignored: state is now driven exclusively by m_rtTargetMakeupGain
    )
{
    const TCurveSnapshot* snapshot = rt_snapshot();

    // Do nothing if there are no nodes active
    if (!snapshot || snapshot->is_empty()) {
        return 0;
    }

    // Real-time Safety: Evaluate if the manual fader/mute/solo component requires linear ramping
    bool requiresMakeupRamp = !TraversoDAW::Float::compare(m_rtCurrentMakeupGain, m_rtTargetMakeupGain);
    float makeupDelta = requiresMakeupRamp ? (m_rtTargetMakeupGain - m_rtCurrentMakeupGain) / nframes : 0.0f;

    // --- SCENARIO A: BEYOND THE LAST AUTOMATION NODE (Trivial static gain zone) ---
    if (endlocation > qint64(snapshot->get_range())) {
        audio_sample_t nodeGain = audio_sample_t(snapshot->points().back().value);

        if (requiresMakeupRamp) {
            // Apply click-free linear coefficient ramping to the fader component past curve boundaries
            for (uint chan = 0; chan < channels; ++chan) {
                audio_sample_t* buffer = audioBus->get_buffer(chan).get_data(nframes);
                float currentMakeupCache = m_rtCurrentMakeupGain;

                for (nframes_t n = 0; n < nframes; ++n) {
                    currentMakeupCache += makeupDelta;
                    buffer[n] *= (nodeGain * currentMakeupCache);
                }
            }
            m_rtCurrentMakeupGain = m_rtTargetMakeupGain;
        } else {
            // Performance Critical: If both parameters are static, evaluate absolute silence clearing
            float finalStaticGain = nodeGain * m_rtCurrentMakeupGain;
            if (TraversoDAW::Float::equals_0(finalStaticGain)) {
                for (uint chan = 0; chan < channels; ++chan) {
                    std::memset(audioBus->get_buffer(chan).get_data(nframes), 0, nframes * sizeof(audio_sample_t));
                }
            } else {
                // Apply fast vectorized block scaling when faders are completely resting
                audioBus->apply_gain_to_buffers(nframes, finalStaticGain);
            }
        }
        return 1;
    }

    // --- SCENARIO B: INSIDE THE ACTIVE AUTOMATION CURVE AREA ---
    TAudioBuffer &curveBuffer = m_session->get_curve_buffer();

    // Compute the spline interpolation curve vector into curveBuffer
    snapshot->get_vector(double(startlocation.universal_frame()), double(endlocation.universal_frame()),
                         curveBuffer.get_data(nframes), nframes, m_rtLookupCache);

    for (uint chan = 0; chan < channels; ++chan) {
        audio_sample_t* buffer = audioBus->get_buffer(chan).get_data(nframes);
        float* cBuffer = curveBuffer.get_data(nframes);
        float currentMakeupCache = m_rtCurrentMakeupGain;

        for (nframes_t n = 0; n < nframes; ++n) {
            if (requiresMakeupRamp) {
                currentMakeupCache += makeupDelta;
            }
            // THE PRECISE GEOMETRIC MULTIPLICATION:
            // Multiplies the audio, the curve, and the click-free manual fader inside a single L1-cache loop!
            buffer[n] *= (cBuffer[n] * currentMakeupCache);
        }
    }

    if (requiresMakeupRamp) {
        m_rtCurrentMakeupGain = m_rtTargetMakeupGain;
    } else {
        // Zero out residual memory frames during prolonged mute/solo intervals to block leakage noise
        if (TraversoDAW::Float::equals_0(m_rtCurrentMakeupGain)) {
            for (uint chan = 0; chan < channels; ++chan) {
                std::memset(audioBus->get_buffer(chan).get_data(nframes), 0, nframes * sizeof(audio_sample_t));
            }
        }
    }

    return 1;
}


std::shared_ptr<const TCurveSnapshot> TCurve::create_snapshot() const
{
    std::vector<TCurveSnapshot::Point> points;
    points.reserve(size_t(m_nodes.size()));

    for (const TCurveNode* node : m_nodes) {
        points.push_back({node->get_when(), node->get_value()});
    }

    return TCurveSnapshot::create(std::move(points), m_defaultValue);
}

std::shared_ptr<const TCurveSnapshot> TCurve::evaluation_snapshot() const
{
    return m_snapshot.current();
}

void TCurve::get_vector (double x0, double x1, const TAudioBuffer &audioBuffer, nframes_t veclen)
{
    evaluation_snapshot()->get_vector(x0, x1, audioBuffer.get_data(veclen), veclen, m_guiLookupCache);
}

void TCurve::set_range(double when, TRtTransaction& transaction)
{
	if (m_nodes.isEmpty()) {
		printf("Curve::set_range: no nodes!!");
		return;
	}
	
    TCurveNode* lastnode = m_nodes.last();
	
    if (TraversoDAW::Float::compare(lastnode->get_when(), when)) {
// 		printf("Curve::set_range: new range == current range!\n");
		return;
	}
	
	if (when < 0.0 ) {
		printf("Curve::set_range: error, when < 0.0 !  (%f)\n", when);
		return;
	}
	
	Q_ASSERT(when >= 0.0);
    Q_ASSERT(lastnode);
    Q_ASSERT(lastnode->get_when() != 0.0);
	
    double factor = when / lastnode->get_when();
	
    if (TraversoDAW::Float::equals_1(factor))
		return;
	
    x_scale(factor, transaction);
}

void TCurve::x_scale(double factor, TRtTransaction& transaction)
{
	Q_ASSERT(factor != 0.0);

    for (TCurveNode* node : std::as_const(m_nodes)) {
        move_node(node, node->get_when() * factor, node->get_value(), transaction);
	}
}

void TCurve::set_node(TCurveNode* node, double when, double value, TRtTransaction& transaction)
{
    Q_ASSERT(node->get_curve() == this);
    Q_ASSERT(m_nodes.contains(node));

    if (TraversoDAW::Float::compare(node->get_when(), when) && TraversoDAW::Float::compare(node->get_value(), value)) {
        return;
    }

    move_node(node, when, value, transaction);

    auto earlier = [](const TCurveNode* left, const TCurveNode* right) {
        return left->get_when() < right->get_when();
    };
    if (!std::is_sorted(m_nodes.cbegin(), m_nodes.cend(), earlier)) {
        std::stable_sort(m_nodes.begin(), m_nodes.end(), earlier);
    }

    emit nodePositionChanged();
}

void TCurve::move_node(TCurveNode* node, double when, double value, TRtTransaction& transaction)
{
    Q_ASSERT( ! std::isnan(when));
    Q_ASSERT( ! std::isnan(value));

    node->m_when = when;
    node->m_value = value;
    m_snapshot.mark_changed(transaction);
}


/**
 * @brief Adds a new automation node keyframe to this TCurve graph topology.
 *
 * @note This function must be called exclusively from the main GUI thread context!
 *
 * @param node The concrete CurveNode instance to append to the envelope graph.
 * @param historable Defines if the operation persists on Traverso's long-term history undo stack.
 * @return A command that adds the node, nullptr if there already is a node at this position.
 */
TCommand* TCurve::add_node(TCurveNode* node, bool historable)
{
    PENTER2;

    // Guard check preventing overlapping duplicate keyframe layout coordinates
    for (TCurveNode* cn : std::as_const(m_nodes)) {
        if (TraversoDAW::Float::compare(node->get_when(), cn->get_when()) && TraversoDAW::Float::compare(node->get_value(), cn->get_value())) {
            tInformUser().warning(tr("There is already a node at this exact position, not adding a new node"));
            delete node;
            node = nullptr;
            return nullptr;
        }
    }

    return new TAddRemoveCommand(
        this,
        nullptr,
        historable,
        [this, node](TRtTransaction& transaction) { private_add_node(node, transaction); },
        [this, node](TRtTransaction& transaction) { private_remove_node(node, transaction); },
        tr("Add CurveNode")
        );
}

/**
 * @brief Removes an existing automation node keyframe from this TCurve graph topology.
 *
 * @note This function must be called exclusively from the main GUI thread context!
 */
TCommand* TCurve::remove_node(TCurveNode* node, bool historable)
{
    PENTER2;

    return new TAddRemoveCommand(
        this,
        nullptr,
        historable,
        [this, node](TRtTransaction& transaction) { private_remove_node(node, transaction); },
        [this, node](TRtTransaction& transaction) { private_add_node(node, transaction); },
        tr("Remove CurveNode")
        );
}

void TCurve::private_add_node(TCurveNode* node, TRtTransaction& transaction)
{
    // After the nodes at the same position
    auto position = std::upper_bound(m_nodes.begin(), m_nodes.end(), node, [](const TCurveNode* left, const TCurveNode* right) {
        return left->get_when() < right->get_when();
    });
    m_nodes.insert(position, node);
    m_snapshot.mark_changed(transaction);

    emit nodeAdded(node);
}

void TCurve::private_remove_node(TCurveNode* node, TRtTransaction& transaction)
{
    m_nodes.removeAll(node);
    m_snapshot.mark_changed(transaction);

    emit nodeRemoved(node);
}

void TCurve::set_sheet(TSession * sheet)
{
    m_session = sheet;
    set_history_stack(m_session->get_history_stack());
}


double TCurve::get_range() const
{
    if (m_nodes.size() == 0) {
        return 0;
    }

    return m_nodes.last()->get_when();
}
