#pragma once

#include "TBaseCursorRenderer.h"
#include <QString>

class TDefaultCursorRenderer : public TBaseCursorRenderer
{
public:
    explicit TDefaultCursorRenderer();
    virtual ~TDefaultCursorRenderer() override = default;

    // Polymorphic behavioral overrides
    void draw(QPainter* painter, const QVariant& liveData) override;
    QRectF get_bounding_rect() const override;

private:
    QString m_shapeName;
};
