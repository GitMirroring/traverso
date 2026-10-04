// TCursorRenderer.h
#pragma once
#include "TContextPointer.h"
#include <QRectF>

class QPainter;
class TBaseCursorRenderer;

class TCursorRenderer
{
public:
    TCursorRenderer();
    ~TCursorRenderer();

    void update_strategy(TContextPointer::CursorType type);
    void render(QPainter* painter, const QVariant& liveData);
    QRectF get_active_bounding_rect() const;

private:
    std::unique_ptr<TBaseCursorRenderer> m_activeStrategy;
    TContextPointer::CursorType          m_currentType;
};
