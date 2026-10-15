/*
    Copyright (C) 2005-2006 Remon Sijrier
    Original version from Ardour curve.cc, modified
    in order to fit Traverso's lockless design
*/

#ifndef TCURVE_H
#define TCURVE_H

#include "TContextItem.h"
#include <QString>
#include <QList>
#include <QDomDocument>
#include <atomic>
#include <memory>

#include "TAudioBuffer.h"
#include "TCurveSnapshot.h"
#include "TRtValue.h"
#include "TTimeRef.h"

class TSession;
class AudioBus;
class TCurveNode;
class TRtTransaction;

/**
 * @brief A curve of nodes, e.g. gain automation or a fade.
 *
 * The GUI thread owns the nodes and is the only thread that reads or changes
 * them. Every change takes a TRtTransaction, which publishes a new solved
 * TCurveSnapshot to the audio thread. The GUI thread evaluates the same
 * snapshot, see gui_snapshot().
 */
class TCurve : public TContextItem
{
    Q_OBJECT

public:
    TCurve(TContextItem* parent);
    TCurve(TContextItem* parent, const QDomNode& node);
    virtual ~TCurve();

    QDomNode get_state(QDomDocument doc, const QString& name);
    virtual int set_state( const QDomNode& node );

    // Audio thread
    // Modernized signature to process both vector curves and manual fader metrics simultaneously
    int process(AudioBus* audioBus, const TTimeRef& startlocation, const TTimeRef& endlocation, nframes_t nframes, uint channels, float makeupgain=1.0f);

    // GUI thread

    TCommand* add_node(TCurveNode* node, bool historable=true);
    TCommand* remove_node(TCurveNode* node, bool historable=true);
    // One command for all nodes, nullptr without nodes
    TCommand* remove_nodes(const QList<TCurveNode*>& nodes, const QString& description);

    // Moves node of this curve to when and value
    void set_node(TCurveNode* node, double when, double value, TRtTransaction& transaction);

    // Get functions
    double get_range() const;
    void get_vector (double x0, double x1, const TAudioBuffer &audioBuffer, nframes_t veclen);
    // Sorted by when
    const QList<TCurveNode*>& get_nodes() const {return m_nodes;}
    TSession* get_sheet() const {return m_session;}
    bool is_trivial();
    float get_trivial_gain();

    // The solved curve the GUI thread evaluates, the same one the audio thread
    // gets. Never nullptr.
    std::shared_ptr<const TCurveSnapshot> gui_snapshot() const;

    // Set functions
    virtual void set_range(double when, TRtTransaction& transaction);
    void set_sheet(TSession* sheet);

    void set_start_offset(TTimeRef offset) {m_startoffset = offset;}
    TTimeRef get_start_offset() const {return m_startoffset;}

protected:
    TSession* m_session{};

    // Adds node without a command, e.g. for the nodes a curve starts with
    void private_add_node(TCurveNode* node, TRtTransaction& transaction);

    // Moves node without sorting or signals, for subclasses that position all
    // nodes at once and keep them in order, see TFadeCurve
    void move_node(TCurveNode* node, double when, double value, TRtTransaction& transaction);

    // Audio thread: the snapshot to evaluate, nullptr until the first one is
    // published, and the lookup cache of the audio thread
    const TCurveSnapshot* rt_snapshot() const {return m_snapshot.rt_snapshot();}
    TCurveLookupCache& rt_lookup_cache() {return m_rtLookupCache;}

private :
    QList<TCurveNode*>          m_nodes;
    TRtValue<TCurveSnapshot>    m_snapshot;
    TCurveLookupCache           m_guiLookupCache;
    TCurveLookupCache           m_rtLookupCache;
    double          m_defaultValue{};
    TTimeRef		m_startoffset;

    float           m_rtCurrentMakeupGain{1.0f};
    float           m_rtTargetMakeupGain{1.0f};

    std::shared_ptr<const TCurveSnapshot> create_snapshot() const;
    void private_remove_node(TCurveNode* node, TRtTransaction& transaction);
    void x_scale(double factor, TRtTransaction& transaction);
    void init();

    friend class TAudioProcessingNode; // Grants thread-queue access to target gain states

signals :
    void stateChanged();
    void nodeAdded(TCurveNode*);
    void nodeRemoved(TCurveNode*);
    void nodePositionChanged();
};

#endif
