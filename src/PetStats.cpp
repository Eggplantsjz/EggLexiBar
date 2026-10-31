#include "PetStats.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>

namespace {

// 3x5 pixel glyphs, one quint8 per row, bits 2..0 = left..right column.
const quint8 kGlyphs[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
    {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
    {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
    {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
    {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
    {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
    {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
    {0b111, 0b001, 0b001, 0b001, 0b001}, // 7
    {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
    {0b111, 0b101, 0b111, 0b001, 0b111}, // 9
};

constexpr int kPad = 6;
constexpr int kDigitScale = 2;      // 3x5 cells → 6x10 px
constexpr int kDigitGap = 2;
constexpr int kDigitsTop = 5;
constexpr int kGaugeTop = 19;
constexpr int kGaugeCells = 10;
constexpr int kGaugeCellW = 4;
constexpr int kGaugeCellH = 7;
constexpr int kGaugeGap = 2;
constexpr int kGaugeWidth =
    kGaugeCells * kGaugeCellW + (kGaugeCells - 1) * kGaugeGap;   // 58
constexpr int kContentWidth = 84;
constexpr int kContentHeight = 28;

const QColor kPanel(12, 16, 24, 205);
const QColor kPanelBorder(122, 162, 247, 70);
const QColor kDigit(134, 239, 172);
const QColor kDigitGlow(134, 239, 172, 40);
const QColor kGoalText(113, 128, 154);
const QColor kPercent(156, 193, 255);
const QColor kGaugeEmpty(255, 255, 255, 26);
const QColor kGaugeAmber(251, 191, 36);
const QColor kGaugeFull(134, 239, 172);

QFont monoFont(int pixelSize)
{
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::DemiBold);
    return font;
}

} // namespace

PetStats::PetStats(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFocusPolicy(Qt::NoFocus);
    setToolTip(QStringLiteral("茄子的电子宠物面板"));
}

QSize PetStats::sizeHint() const
{
    return QSize(kContentWidth + 2 * kPad, kContentHeight + 2 * kPad);
}

void PetStats::setStats(int eaten, int familiar, int seen, int goal)
{
    const int newEaten = qMax(0, eaten);
    const int newFamiliar = qMax(0, familiar);
    const int newSeen = qMax(0, seen);
    const int newGoal = qMax(1, goal);
    if (m_eaten == newEaten && m_familiar == newFamiliar && m_seen == newSeen
        && m_goal == newGoal)
        return;
    m_eaten = newEaten;
    m_familiar = newFamiliar;
    m_seen = newSeen;
    m_goal = newGoal;
    update();
}

void PetStats::paintDigits(QPainter& painter, const QPoint& origin, int value) const
{
    const int clamped = qBound(0, value, 99);
    const int digits[2] = {clamped / 10, clamped % 10};
    const int cell = kDigitScale;
    const int glyphWidth = 3 * cell;
    const int glyphHeight = 5 * cell;

    // Faint halo first: the digits read as a glowing LED rather than flat paint.
    painter.setPen(Qt::NoPen);
    painter.setBrush(kDigitGlow);
    painter.drawRoundedRect(QRect(origin.x() - 2, origin.y() - 2,
                                  glyphWidth * 2 + kDigitGap + 4, glyphHeight + 4),
                            3, 3);

    painter.setBrush(kDigit);
    for (int d = 0; d < 2; ++d) {
        const int x = origin.x() + d * (glyphWidth + kDigitGap);
        for (int row = 0; row < 5; ++row) {
            const quint8 bits = kGlyphs[digits[d]][row];
            for (int col = 0; col < 3; ++col) {
                if (bits & (1 << (2 - col)))
                    painter.drawRect(QRect(x + col * cell, origin.y() + row * cell,
                                           cell, cell));
            }
        }
    }
}

void PetStats::paintGauge(QPainter& painter, const QRect& rect, qreal fullness) const
{
    const qreal clamped = qBound<qreal>(0.0, fullness, 1.0);
    int filled = int(clamped * kGaugeCells);
    if (filled <= 0 && m_eaten > 0)
        filled = 1;   // one meal always shows: the pet is never "empty" after eating
    const QColor fill = clamped >= 1.0 ? kGaugeFull : kGaugeAmber;

    painter.setPen(Qt::NoPen);
    for (int i = 0; i < kGaugeCells; ++i) {
        const QRect cell(rect.x() + i * (kGaugeCellW + kGaugeGap), rect.y(),
                         kGaugeCellW, kGaugeCellH);
        painter.setBrush(i < filled ? fill : kGaugeEmpty);
        painter.drawRoundedRect(cell, 1.5, 1.5);
    }
}

void PetStats::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(kPanelBorder, 1));
    p.setBrush(kPanel);
    p.drawRoundedRect(panel, 9, 9);

    const QPoint digitsOrigin(kPad, kPad + kDigitsTop);
    paintDigits(p, digitsOrigin, m_eaten);

    // "12/20" — how many of today's meals the pet already had.
    const QFont goalFont = monoFont(9);
    p.setFont(goalFont);
    p.setPen(kGoalText);
    const int digitsWidth = 2 * (3 * kDigitScale) + kDigitGap;
    const QRect goalRect(kPad + digitsWidth + 3, kPad + kDigitsTop,
                         kContentWidth - digitsWidth - 3, 10);
    p.drawText(goalRect, Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("/%1").arg(m_goal));

    // Fullness gauge under the digits.
    const QRect gauge(kPad, kPad + kGaugeTop, kGaugeWidth, kGaugeCellH);
    paintGauge(p, gauge, qreal(m_eaten) / qreal(qMax(1, m_goal)));

    // Familiar share, right aligned on the gauge row.
    const int percent = m_seen > 0
                            ? qRound(100.0 * double(m_familiar) / double(m_seen))
                            : 0;
    p.setFont(monoFont(9));
    p.setPen(kPercent);
    const QRect percentRect(kPad + kGaugeWidth + 4, kPad + kGaugeTop - 2,
                            kContentWidth - kGaugeWidth - 4, kGaugeCellH + 4);
    p.drawText(percentRect, Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1%").arg(percent));
}
