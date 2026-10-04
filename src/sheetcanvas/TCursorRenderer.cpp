// TCursorRenderer.cpp
#include "TCursorRenderer.h"
#include "TBaseCursorRenderer.h"
#include "TFaderCursorRenderer.h"
#include "TDefaultCursorRenderer.h"
#include "TPanningCursorRenderer.h"
#include "TZoomCursorRenderer.h"
#include "THoldCursorRenderer.h"
#include "TAutomationCursorRenderer.h"

TCursorRenderer::TCursorRenderer()
    : m_activeStrategy(nullptr)
    , m_currentType(TContextPointer::CursorType::Default)
{}

TCursorRenderer::~TCursorRenderer() = default;

void TCursorRenderer::update_strategy(TContextPointer::CursorType type)
{
    if (m_currentType == type && m_activeStrategy && type != TContextPointer::CursorType::Default) {
        return;
    }
    m_currentType = type;

    switch (type) {
    case TContextPointer::CursorType::GainFader:
        m_activeStrategy = std::make_unique<TFaderCursorRenderer>();
        break;
    case TContextPointer::CursorType::Panning:
        m_activeStrategy = std::make_unique<TPanningCursorRenderer>();
        break;
    case TContextPointer::CursorType::AutomationNode:
        m_activeStrategy = std::make_unique<TAutomationCursorRenderer>();
        break;
    case TContextPointer::CursorType::ZoomHorizontal:
        m_activeStrategy = std::make_unique<TZoomCursorRenderer>(false); // Horizontal strategy profile
        break;
    case TContextPointer::CursorType::ZoomVertical:
        m_activeStrategy = std::make_unique<TZoomCursorRenderer>(true);  // Vertical strategy profile
        break;
    case TContextPointer::CursorType::HoldLeftRight:
        m_activeStrategy = std::make_unique<THoldCursorRenderer>(TContextPointer::CursorType::HoldLeftRight);
        break;
    case TContextPointer::CursorType::HoldUpDown:
        m_activeStrategy = std::make_unique<THoldCursorRenderer>(TContextPointer::CursorType::HoldUpDown);
        break;
    case TContextPointer::CursorType::HoldOmni:
        m_activeStrategy = std::make_unique<THoldCursorRenderer>(TContextPointer::CursorType::HoldOmni);
        break;
    case TContextPointer::CursorType::Default:
    default:
        m_activeStrategy = std::make_unique<TDefaultCursorRenderer>();
        break;
    }
}

void TCursorRenderer::render(QPainter* painter, const QVariant& liveData)
{
    if (!m_activeStrategy) {
        return;
    }

    m_activeStrategy->draw(painter, liveData);
}

QRectF TCursorRenderer::get_active_bounding_rect() const
{
    if (!m_activeStrategy) {
        return QRectF(-14, -20, 28, 40);
    }
    return m_activeStrategy->get_bounding_rect();
}
