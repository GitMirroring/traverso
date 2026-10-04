// THoldCursorRenderer.h
#pragma once
#include "TBaseCursorRenderer.h"
#include "TContextPointer.h"

/**
 * @class THoldCursorRenderer
 * @brief Unified vector renderer replacing legacy drag bitmaps with clean geometric vector glyphs and LCD feedback.
 */
class THoldCursorRenderer : public TBaseCursorRenderer
{
public:
    explicit THoldCursorRenderer(TContextPointer::CursorType dragType);
    virtual ~THoldCursorRenderer() override = default;

    void draw(QPainter* painter, const QVariant& liveData) override;
    QRectF get_bounding_rect() const override;

private:
    TContextPointer::CursorType m_dragType;
};
