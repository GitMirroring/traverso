/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommand.h"
#include <QVariantList>
#include <QPointF>
#include <QPoint>

class TSheetView;
class TTrackView;

/**
 * @class TZoomCommand
 * @brief Manages interactive timeline zooming, track height scaling, and viewport jog operations.
 * @note Modernized to leverage dynamic vector-based canvas cursors with real-time feedback data.
 */
class TZoomCommand : public TCommand
{
    Q_OBJECT

public:
    TZoomCommand(TSheetView* sv, const QVariantList &args);
    virtual ~TZoomCommand() override = default;

    int begin_hold() override;
    int finish_hold() override;
    int prepare_actions() override;
    int do_action() override;
    int undo_action() override;

    int jog() override;

    void set_cursor_shape(int useX, int useY) override;
    void process_collected_number(const QString &collected) override;
    bool supportsEnterFinishesHold() const override { return false; }

private:
    int m_horizontalJogZoomLastX{0};
    int m_verticalJogZoomLastY{0};
    int m_trackHeight{0};
    bool m_jogVertical{false};
    bool m_jogHorizontal{false};
    qreal m_xScalefactor{1.0};
    qreal m_yScalefactor{0.0};
    QPoint m_mousePos;
    QPointF m_origPos;

    int collected_number_to_track_height(const QString& collected) const;

    TSheetView* m_sv{nullptr};
    TTrackView* m_tv{nullptr};

public slots:
    void vzoom_in();
    void vzoom_out();
    void hzoom_in();
    void hzoom_out();
    void track_vzoom_in();
    void track_vzoom_out();
    void toggle_vertical_horizontal_jog_zoom();
    void toggle_expand_all_tracks();
    void numerical_input();
};

