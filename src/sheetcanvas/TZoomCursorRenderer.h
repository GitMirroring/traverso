// TZoomCursorRenderer.h
#pragma once
#include "TBaseCursorRenderer.h"

/**
 * @class TZoomCursorRenderer
 * @brief Renders a dynamic vector-based magnifying glass cursor with live scale feedback readout.
 */
class TZoomCursorRenderer : public TBaseCursorRenderer
{
public:
    explicit TZoomCursorRenderer(bool isVertical);
    virtual ~TZoomCursorRenderer() override = default;

    void draw(QPainter* painter, const QVariant& liveData) override;
    QRectF get_bounding_rect() const override;

private:
    bool m_isVertical;
};
