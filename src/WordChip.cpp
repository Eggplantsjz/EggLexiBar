#include "WordChip.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

WordChip::WordChip(const QString& word, QWidget* parent)
    : QWidget(parent)
    , m_word(word)
{
    m_font.setFamily(QStringLiteral("Segoe UI"));
    m_font.setPixelSize(13);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setToolTip(QStringLiteral("单击复制 · 上下滑动标记为已掌握 · 右键更多操作"));

    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(280);
    connect(m_hoverTimer, &QTimer::timeout, this, [this] { emit hoverStarted(this); });
}

void WordChip::setCount(int count)
{
    if (m_count == count)
        return;
    m_count = count;
    updateGeometry();
    update();
}

void WordChip::setFresh(bool fresh)
{
    if (m_fresh == fresh)
        return;
    m_fresh = fresh;
    update();
}

void WordChip::markCopied()
{
    m_copied = true;
    update();
    QTimer::singleShot(380, this, [this] {
        m_copied = false;
        update();
    });
}

void WordChip::setChipFont(const QFont& font)
{
    m_font = font;
    m_padX = qMax(9, font.pixelSize() * 3 / 4);
    setFixedHeight(chipHeightFor(font));
    updateGeometry();
    update();
}

int WordChip::chipHeightFor(const QFont& font)
{
    const QFontMetrics fm(font);
    return qMax(font.pixelSize(), fm.height()) + 12;
}

QSize WordChip::sizeHint() const
{
    const QFontMetrics fm(m_font);
    int width = fm.horizontalAdvance(m_word) + 2 * m_padX;
    if (m_count > 1)
        width += fm.horizontalAdvance(QStringLiteral("×%1").arg(m_count)) + 8;
    return QSize(width, chipHeightFor(m_font));
}

void WordChip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = height() / 2.0;
    QColor background;
    QColor border;
    if (m_swiping) {
        background = QColor(74, 222, 128, 92);
        border = QColor(134, 239, 172, 210);
    } else if (m_copied) {
        background = QColor(74, 222, 128, 70);
        border = QColor(134, 239, 172, 180);
    } else if (m_fresh) {
        background = QColor(122, 162, 247, m_hover ? 88 : 64);
        border = QColor(122, 162, 247, 190);
    } else if (m_hover || m_pressed) {
        background = QColor(255, 255, 255, 46);
        border = QColor(255, 255, 255, 46);
    } else {
        background = QColor(255, 255, 255, 20);
        border = QColor(255, 255, 255, 26);
    }

    QBrush brush;
    if (m_swiping || m_copied || m_fresh) {
        // Momentary states stay flat for a crisp, unambiguous signal.
        brush = background;
    } else {
        // Idle + hover get a subtle top-lit gradient for a softer, less "flat"
        // pill look.
        QLinearGradient g(rect().topLeft(), rect().bottomLeft());
        QColor lighter = background;
        lighter.setAlpha(qMin(255, background.alpha() + 24));
        g.setColorAt(0.0, lighter);
        g.setColorAt(1.0, background);
        brush = QBrush(g);
    }

    p.setPen(QPen(border, 1));
    p.setBrush(brush);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    p.setFont(m_font);
    const QFontMetrics fm(m_font);
    const int baseline = (height() + fm.ascent() - fm.descent()) / 2;

    p.setPen(QColor(242, 245, 250));
    p.drawText(QPointF(m_padX, baseline), m_word);

    if (m_count > 1) {
        const QString counter = QStringLiteral("×%1").arg(m_count);
        p.setPen(QColor(156, 193, 255));
        p.drawText(QPointF(width() - m_padX - fm.horizontalAdvance(counter), baseline), counter);
    }
}

void WordChip::enterEvent(QEnterEvent* event)
{
    m_hover = true;
    update();
    if (m_hoverTimer)
        m_hoverTimer->start();
    QWidget::enterEvent(event);
}

void WordChip::leaveEvent(QEvent* event)
{
    m_hover = false;
    // Keep a pressed gesture alive after the pointer crosses the pill's edge:
    // a vertical swipe naturally leaves this small target before mouse release.
    if (!m_pressed)
        m_swiping = false;
    if (m_hoverTimer)
        m_hoverTimer->stop();
    emit hoverEnded(this);
    update();
    QWidget::leaveEvent(event);
}

void WordChip::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_swiping = false;
        m_pressPos = event->position().toPoint();
        // The pointer naturally exits a small pill during a swipe. Capture it
        // explicitly so its move and release events still come back here.
        grabMouse();
        if (m_hoverTimer)
            m_hoverTimer->stop();
        emit hoverEnded(this);
        update();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WordChip::mouseMoveEvent(QMouseEvent* event)
{
    if (m_pressed && (event->buttons() & Qt::LeftButton)) {
        // A vertical gesture is deliberately required: horizontal motion is
        // reserved for scrolling the row of chips in the overlay.
        constexpr int swipeThreshold = 16;
        const QPoint delta = event->position().toPoint() - m_pressPos;
        const bool isVertical = qAbs(delta.y()) >= swipeThreshold
            && qAbs(delta.y()) > qAbs(delta.x());
        if (m_swiping != isVertical) {
            m_swiping = isVertical;
            update();
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void WordChip::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        const bool markKnown = m_swiping;
        m_swiping = false;
        releaseMouse();
        update();
        if (markKnown) {
            emit knownBySwipe(m_word);
        } else if (rect().contains(event->position().toPoint())) {
            // Give the chip itself a brief "copied" flash so the user can confirm
            // it was this word (not an adjacent one) that entered the clipboard.
            markCopied();
            emit activated(m_word);
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void WordChip::contextMenuEvent(QContextMenuEvent* event)
{
    emit menuRequested(m_word, event->globalPos());
    event->accept();
}
