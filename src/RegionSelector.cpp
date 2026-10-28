#include "RegionSelector.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScreen>
#include <QVBoxLayout>

RegionSelector::RegionSelector(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::Tool | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    // Fullscreen across all monitors so the user can drag anywhere.
    QRect unified;
    for (QScreen* s : QGuiApplication::screens())
        unified = unified.united(s->geometry());
    setGeometry(unified);

    // Place a hint label centered at the top telling the user what to do.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 40, 0, 0);
    layout->addStretch(1);
    auto* hint = new QLabel(QStringLiteral("拖动鼠标框选识别区域  ·  Esc 取消"), this);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(
        QStringLiteral("color:#F2F5FA;background:rgba(20,22,28,220);border-radius:10px;"
                       "padding:10px 18px;font-family:'Microsoft YaHei UI';font-size:13px;"));
    hint->adjustSize();
    hint->raise();
}

void RegionSelector::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Dim everything outside the drag rect.
    p.fillRect(rect(), QColor(0, 0, 0, 140));

    if (m_current.isValid() && m_current.width() > 4 && m_current.height() > 4) {
        // Clear inside the rect so the user sees the original content.
        QRect local = QRect(m_current.topLeft() - geometry().topLeft(), m_current.size());
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(local, Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        // 1px red border around the selection + 8 handles at each corner/edge.
        p.setPen(QPen(QColor(251, 191, 36, 230), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(local.adjusted(-0.5, -0.5, 0.5, 0.5));

        // Size hint on the edge.
        p.setPen(QColor(242, 245, 250));
        QFont font(QStringLiteral("Microsoft YaHei UI"));
        font.setPixelSize(11);
        p.setFont(font);
        const QString label = QStringLiteral("%1 × %2 px")
                                  .arg(m_current.width())
                                  .arg(m_current.height());
        const int labelW = p.fontMetrics().horizontalAdvance(label) + 10;
        const int labelH = p.fontMetrics().height() + 4;
        const int labelX = local.right() - labelW - 2;
        const int labelY = local.bottom() + 3;
        if (labelY + labelH > height()) {
            // Flip above the rect if below would fall offscreen.
            p.fillRect(labelX, local.top() - labelH - 3, labelW, labelH,
                       QColor(20, 22, 28, 230));
            p.drawText(labelX + 5, local.top() - 5, label);
        } else {
            p.fillRect(labelX, labelY, labelW, labelH, QColor(20, 22, 28, 230));
            p.drawText(labelX + 5, labelY + p.fontMetrics().ascent() + 2, label);
        }
    }
}

void RegionSelector::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }
    m_start = event->globalPosition().toPoint();
    m_current = QRect(m_start, m_start);
    update();
    event->accept();
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        QPoint end = event->globalPosition().toPoint();
        m_current = QRect(m_start, end).normalized();
        update();
    }
    event->accept();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }
    if (m_current.isValid() && m_current.width() > 4 && m_current.height() > 4) {
        emit regionSelected(m_current.intersected(geometry()));
        close();
    } else {
        // Click (no drag) = cancel.
        emit cancelled();
        close();
    }
    event->accept();
}

void RegionSelector::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        emit cancelled();
        close();
    } else {
        QWidget::keyPressEvent(event);
    }
}
