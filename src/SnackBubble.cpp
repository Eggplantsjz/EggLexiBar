#include "SnackBubble.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QPainter>
#include <QPolygonF>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kPadX = 14;
constexpr int kPadTop = 18;    // room for the pointer triangle
constexpr int kPadBottom = 12;
constexpr int kPointer = 8;    // height of the triangle
constexpr int kMinWidth = 190;
constexpr int kMaxWidth = 380;
constexpr int kHoldMs = 2400;  // long enough to read one or two senses

/// The compact meaning: at most two senses, each shortened so the bubble stays
/// a bubble and not a dictionary page. "原形：…" hints are dropped because the
/// reader only has a couple of seconds.
QString compactSenses(const WordDefinition& definition)
{
    QStringList picked;
    for (const QString& sense : definition.senses) {
        if (sense.startsWith(QStringLiteral("原形：")))
            continue;
        QString line = sense.trimmed();
        if (line.isEmpty())
            continue;
        if (line.size() > 44)
            line = line.left(44) + QStringLiteral("…");
        picked.append(line);
        if (picked.size() >= 2)
            break;
    }
    return picked.join(QStringLiteral("；"));
}

} // namespace

SnackBubble::SnackBubble(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    // Passive by design: it must never swallow a click that was meant for the
    // window underneath, nor count as "the user is interacting with the bar".
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    setWindowTitle(QStringLiteral("WordLens · 茄子吃掉的词"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPadX, kPadTop, kPadX, kPadBottom);
    layout->setSpacing(0);

    m_text = new QLabel(this);
    m_text->setTextFormat(Qt::RichText);
    m_text->setWordWrap(true);
    m_text->setAttribute(Qt::WA_TranslucentBackground, true);
    m_text->setStyleSheet(QStringLiteral("background:transparent;"));
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(12);
    m_text->setFont(font);
    layout->addWidget(m_text);

    m_holdTimer = new QTimer(this);
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(kHoldMs);
    connect(m_holdTimer, &QTimer::timeout, this, [this] { fadeTo(0.0, 220); });

    m_fade = new QPropertyAnimation(this, "windowOpacity", this);
    m_fade->setDuration(150);
    connect(m_fade, &QPropertyAnimation::finished, this, [this] {
        if (windowOpacity() < 0.01)
            hide();
    });
}

void SnackBubble::showFor(const WordDefinition& definition, const QRect& anchor)
{
    if (!definition.isValid())
        return;

    const QString senses = compactSenses(definition);
    if (senses.isEmpty())
        return;

    QString html = QStringLiteral(
                       "<div style=\"font-family:'Segoe UI','Microsoft YaHei UI';\">"
                       "<span style=\"font-size:16px;font-weight:600;color:#F3F6FB;\">%1</span>")
                       .arg(definition.word.toHtmlEscaped());
    if (!definition.phonetic.isEmpty()) {
        html += QStringLiteral(
                    "&nbsp;&nbsp;<span style=\"font-size:11px;color:#9CC1FF;\">/%1/</span>")
                    .arg(definition.phonetic.toHtmlEscaped());
    }
    html += QStringLiteral(
                "<br><span style=\"font-size:12px;color:#CDD5E3;line-height:145%;\">%1</span></div>")
                .arg(senses.toHtmlEscaped());
    m_text->setText(html);

    // Hug the text, but keep a readable minimum and never grow into a paragraph.
    QFont wordFont(QStringLiteral("Segoe UI"));
    wordFont.setPixelSize(16);
    wordFont.setWeight(QFont::DemiBold);
    QFont senseFont(QStringLiteral("Microsoft YaHei UI"));
    senseFont.setPixelSize(12);
    const QFontMetrics wordMetrics(wordFont);
    const QFontMetrics senseMetrics(senseFont);
    const int wanted = qMax(wordMetrics.horizontalAdvance(definition.word),
                            senseMetrics.horizontalAdvance(senses));
    const int contentWidth = qBound(kMinWidth, wanted + 10, kMaxWidth);

    m_text->setFixedWidth(contentWidth);
    int textHeight = m_text->heightForWidth(contentWidth);
    if (textHeight <= 0)
        textHeight = m_text->sizeHint().height();
    m_text->setFixedHeight(textHeight);

    setFixedSize(contentWidth + 2 * kPadX, kPadTop + textHeight + kPadBottom);

    QScreen* screen = QGuiApplication::screenAt(anchor.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    QRect available(0, 0, 1920, 1080);
    if (screen)
        available = screen->availableGeometry();

    int x = anchor.center().x() - width() / 2;
    x = qBound(available.left() + 6, x,
               qMax(available.left() + 6, available.right() - width() - 6));

    // Below the pet by default; flip above when the bar lives at the bottom of
    // the screen (it is draggable, so this really happens).
    int y = anchor.bottom() + 6;
    m_pointerUp = true;
    if (y + height() > available.bottom() - 4) {
        y = anchor.top() - height() - 6;
        m_pointerUp = false;
    }
    y = qBound(available.top() + 4, y,
               qMax(available.top() + 4, available.bottom() - height() - 4));

    m_pointerX = qBound(14, anchor.center().x() - x, width() - 14);

    move(x, y);
    setWindowOpacity(0.0);
    show();
    raise();
    fadeTo(0.96, 150);
    m_holdTimer->start();
}

void SnackBubble::hideNow()
{
    if (m_holdTimer)
        m_holdTimer->stop();
    if (m_fade)
        m_fade->stop();
    hide();
}

void SnackBubble::fadeTo(qreal target, int durationMs)
{
    m_fade->stop();
    m_fade->setStartValue(windowOpacity());
    m_fade->setEndValue(target);
    m_fade->setDuration(durationMs);
    m_fade->start();
}

void SnackBubble::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor bg(26, 29, 37, 246);
    const QColor borderColor(255, 255, 255, 34);
    constexpr int kHalf = 7;

    const qreal tipY = m_pointerUp ? 0.0 : qreal(height());
    const qreal baseY = m_pointerUp ? qreal(kPointer) : qreal(height() - kPointer);
    const QPointF tip(m_pointerX, tipY);
    const QPointF left(m_pointerX - kHalf, baseY);
    const QPointF right(m_pointerX + kHalf, baseY);

    QPolygonF triangle;
    triangle << left << tip << right;
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawPolygon(triangle);
    p.setPen(QPen(borderColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawLine(left, tip);
    p.drawLine(right, tip);

    // The body stops short of the pointer so the triangle stays visible.
    const QRectF body = m_pointerUp
                            ? QRectF(0.5, kPointer - 0.5, width() - 1.0, height() - kPointer)
                            : QRectF(0.5, 0.5, width() - 1.0, height() - kPointer);
    p.setPen(QPen(borderColor, 1));
    p.setBrush(bg);
    p.drawRoundedRect(body, 12, 12);
}
