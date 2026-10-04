// TAutomationCursorRenderer.h
#pragma once
#include "TBaseCursorRenderer.h"

/**
 * @class TAutomationCursorRenderer
 * @brief Renders a dedicated, high-precision automation vector cursor with an integrated LCD display.
 */
class TAutomationCursorRenderer : public TBaseCursorRenderer
{
public:
    TAutomationCursorRenderer() = default;
    virtual ~TAutomationCursorRenderer() override = default;

    void draw(QPainter* painter, const QVariant& liveData) override;
    QRectF get_bounding_rect() const override;
};
