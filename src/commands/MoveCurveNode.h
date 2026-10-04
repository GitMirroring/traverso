/*
    Copyright (C) 2010-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TMoveCommand.h"
#include "TTimeRef.h"
#include <QRectF>
#include <QList>
#include <QPoint>
#include <memory>

class TCurveView;
class TCurveNode;
class TCurve;

/**
 * @class MoveCurveNode
 * @brief Manages spatial dragging and coordinate updates for automation curve envelope keyframes.
 */
class MoveCurveNode : public TMoveCommand
{
    Q_OBJECT

public:
    MoveCurveNode(TCurve* curve,
                  QList<TCurveNode*> nodes,
                  float height,
                  qint64 scalefactor,
                  TTimeRef minWhenDiff,
                  TTimeRef maxWhenDiff,
                  double minValueDiff,
                  double maxValueDiff,
                  const QString& des);

    virtual ~MoveCurveNode() override = default;

    int prepare_actions() override;
    int do_action() override;
    int undo_action() override;
    int finish_hold() override;
    void cancel_action() override;
    int begin_hold() override;
    int jog() override;
    void set_cursor_shape(int useX, int useY) override;

    void set_height(int height) {
        if (mcnd) mcnd->height = static_cast<float>(height);
    }

    int get_height() const {
        return mcnd ? static_cast<int>(mcnd->height) : 0;
    }

private:
    struct MoveCurveNodeData {
        qint64   scalefactor{0};
        QPoint   mousepos;
        bool     verticalOnly{false};
        float    height{0.0f};
        double   maxValueDiff{0.0};
        double   minValueDiff{0.0};
        TTimeRef maxWhenDiff;
        TTimeRef minWhenDiff;
    };

    // Managed resource that will be explicitly freed when moving to the history stack
    std::unique_ptr<MoveCurveNodeData> mcnd;

    struct CurveNodeData {
        TCurveNode* node{nullptr};
        double      origWhen{0.0};
        double      origValue{0.0};
    };

    double   m_valueDiff{0.0};
    TTimeRef m_whenDiff;

    std::vector<CurveNodeData> m_nodeDatas;

    int check_and_apply_when_and_value_diffs();

public slots:
    void move_up() final;
    void move_down() final;
    void move_left() final;
    void move_right() final;
    void toggle_vertical_only();
};
