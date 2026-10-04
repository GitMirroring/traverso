// TBaseCursorRenderer.h
#pragma once
#include <QPainter>
#include <QVariant>

class TBaseCursorRenderer
{
public:
    virtual ~TBaseCursorRenderer() = default;

    virtual void draw(QPainter* painter, const QVariant& liveData) = 0;
    virtual QRectF get_bounding_rect() const = 0;
};
