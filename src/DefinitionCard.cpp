#include "DefinitionCard.h"

#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace {

constexpr int kContentWidth = 360;
constexpr int kActionHeight = 26;

QPushButton* makeActionButton(QWidget* parent, const QString& text, const QString& tooltip,
                               const QString& objectName)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    button->setToolTip(tooltip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedHeight(kActionHeight);
    return button;
}

} // namespace

DefinitionCard::DefinitionCard(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setWindowTitle(QStringLiteral("WordLens · 释义"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 12, 15, 12);
    layout->setSpacing(8);

    m_text = new QLabel(this);
    m_text->setTextFormat(Qt::RichText);
    m_text->setWordWrap(true);
    m_text->setAttribute(Qt::WA_TranslucentBackground, true);
    m_text->setStyleSheet(QStringLiteral("background:transparent;"));
    QFont baseFont(QStringLiteral("Microsoft YaHei UI"));
    baseFont.setPixelSize(13);
    m_text->setFont(baseFont);
    layout->addWidget(m_text);

    m_actions = new QWidget(this);
    m_actions->setFixedHeight(kActionHeight);
    auto* actions = new QHBoxLayout(m_actions);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(6);

    auto* knownButton = makeActionButton(m_actions, QStringLiteral("✓ 认识"),
                                         QStringLiteral("标记为已掌握，以后不再出现"),
                                         QStringLiteral("cardKnown"));
    auto* blockButton = makeActionButton(m_actions, QStringLiteral("✕ 忽略"),
                                         QStringLiteral("不再显示这个词（可能不是真正的单词）"),
                                         QStringLiteral("cardBlock"));
    auto* copyButton = makeActionButton(m_actions, QStringLiteral("复制"),
                                        QStringLiteral("复制单词到剪贴板"),
                                        QStringLiteral("cardCopy"));
    actions->addWidget(knownButton);
    actions->addWidget(blockButton);
    actions->addStretch(1);
    actions->addWidget(copyButton);
    layout->addWidget(m_actions);

    setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(255,255,255,16);border:1px solid rgba(255,255,255,28);"
        "border-radius:6px;color:#C7CEDC;padding:0 10px;font-family:'Microsoft YaHei UI';"
        "font-size:12px;}"
        "QPushButton:hover{background:rgba(122,162,247,55);color:#F2F5FA;}"
        "QPushButton#cardKnown{background:rgba(74,222,128,38);"
        "border-color:rgba(74,222,128,90);color:#B9F6CA;}"
        "QPushButton#cardKnown:hover{background:rgba(74,222,128,85);color:#EAFFF1;}"
        "QPushButton#cardBlock{color:#E0A3A3;}"
        "QPushButton#cardBlock:hover{background:rgba(248,113,113,55);color:#FFE3E3;}"));

    connect(knownButton, &QPushButton::clicked, this, [this] {
        if (!m_word.isEmpty())
            emit markKnownRequested(m_word);
        hide();
    });
    connect(blockButton, &QPushButton::clicked, this, [this] {
        if (!m_word.isEmpty())
            emit blockRequested(m_word);
        hide();
    });
    connect(copyButton, &QPushButton::clicked, this, [this] {
        if (!m_word.isEmpty())
            emit copyRequested(m_word);
        hide();
    });
}

void DefinitionCard::showFor(const WordDefinition& definition, const QRect& anchor)
{
    if (!definition.isValid())
        return;

    m_word = definition.word;

    QStringList senses;
    for (const QString& sense : definition.senses) {
        if (sense.startsWith(QLatin1String("原形："))) {
            senses.append(QStringLiteral("<span style=\"color:#8A93A6;font-size:11px;\">%1</span>")
                              .arg(sense.toHtmlEscaped()));
        } else {
            senses.append(QStringLiteral("<span style=\"color:#D6DDEA;\">%1</span>")
                              .arg(sense.toHtmlEscaped()));
        }
    }

    QString html = QStringLiteral(
                       "<div style=\"font-family:'Segoe UI','Microsoft YaHei UI';white-space:normal;\">"
                       "<span style=\"font-size:16px;font-weight:600;color:#F2F5FA;\">%1</span>")
                       .arg(definition.word.toHtmlEscaped());

    if (!definition.phonetic.isEmpty()) {
        html += QStringLiteral(
                    "&nbsp;&nbsp;<span style=\"font-size:12px;color:#9CC1FF;\">/%1/</span>")
                    .arg(definition.phonetic.toHtmlEscaped());
    }

    html += QStringLiteral("<br><span style=\"font-size:13px;line-height:150%;\">%1</span></div>")
                .arg(senses.join(QStringLiteral("<br>")));

    m_text->setText(html);

    m_text->setFixedWidth(kContentWidth);
    const int textHeight = qMax(24, m_text->heightForWidth(kContentWidth));
    m_text->setFixedHeight(textHeight);
    setFixedSize(kContentWidth + 30, 12 + textHeight + 8 + kActionHeight + 12);

    QScreen* screen = QGuiApplication::screenAt(anchor.center());
    if (!screen)
        screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    QRect available(0, 0, 1920, 1080);
    if (screen)
        available = screen->availableGeometry();

    int x = anchor.center().x() - width() / 2;
    int y = anchor.top() - height() - 8;
    if (y < available.top() + 4) {
        y = anchor.bottom() + 8;
        m_anchorBelow = false;  // card sits BELOW anchor → triangle points UP
    } else {
        m_anchorBelow = true;   // card sits ABOVE anchor → triangle points DOWN
    }

    m_pointerX = anchor.center().x() - x;
    // Clamp pointer so it never draws off the card's rounded side
    m_pointerX = qBound(10, m_pointerX, width() - 10);

    x = qBound(available.left() + 6, x, qMax(available.left() + 6, available.right() - width() - 6));
    y = qBound(available.top() + 4, y, qMax(available.top() + 4, available.bottom() - height() - 4));

    move(x, y);
    show();
    raise();
}

void DefinitionCard::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // Draw the triangle pointer first (behind the body fill is fine —
    // we paint the rounded body over it on the opposite side, leaving only
    // the triangle on the visible anchor-facing edge).
    constexpr int kTriangleHalf = 7;      // half-width of the pointer base
    constexpr int kTriangleHeight = 8;    // height of the pointer
    const QColor bg(22, 25, 32, 246);
    const QColor borderColor(255, 255, 255, 34);

    QPointF tip(m_pointerX, m_anchorBelow ? height() : 0);
    QPointF left(m_pointerX - kTriangleHalf, m_anchorBelow ? height() - kTriangleHeight : kTriangleHeight);
    QPointF right(m_pointerX + kTriangleHalf, m_anchorBelow ? height() - kTriangleHeight : kTriangleHeight);
    QPolygonF triangle;
    triangle << left << tip << right;
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawPolygon(triangle);
    // Outline on the triangle's two exposed edges
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(left, tip);
    painter.drawLine(right, tip);

    // Now paint the rounded card body (same fill as the triangle).
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(bg);
    painter.drawRoundedRect(body, 12, 12);
}

void DefinitionCard::enterEvent(QEnterEvent* event)
{
    emit hoverChanged(true);
    QWidget::enterEvent(event);
}

void DefinitionCard::leaveEvent(QEvent* event)
{
    emit hoverChanged(false);
    QWidget::leaveEvent(event);
}
