#include "PetWidget.h"

#include <QPainter>
#include <QRandomGenerator>
#include <QTimer>

namespace {

constexpr int CELL = 3;               // size of one pixel-art cell
constexpr int COLS = 10;
constexpr int ROWS = 13;
constexpr int TOP_MARGIN = 6;         // headroom for the "scanning" bounce
constexpr int WIDGET_W = COLS * CELL;
constexpr int WIDGET_H = ROWS * CELL + TOP_MARGIN;

const QColor kStem(0x57, 0xB2, 0x5A);
const QColor kStemDark(0x3E, 0x8E, 0x43);
const QColor kBody(0x8A, 0x5C, 0xE0);       // eggplant purple
const QColor kBodyDark(0x6D, 0x3F, 0xC4);
const QColor kBodyLight(0xB4, 0x8C, 0xFF);  // highlight
const QColor kWhite(255, 255, 255);
const QColor kInk(0x2A, 0x24, 0x38);        // eye pupil / outline
const QColor kMouth(0x3A, 0x1D, 0x5E);      // inside of the mouth

/// One "hop" while the scanner is running. The last frames rest so the motion
/// reads as a bounce rather than a constant jitter.
constexpr int kScanBounce[8] = {0, -2, -4, -5, -4, -2, 0, 0};

} // namespace

PetWidget::PetWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(WIDGET_W, WIDGET_H);
    setAutoFillBackground(false);

    // Blinks on a 2.2–3.5 s jitter so the pet feels alive but subtle.
    m_blinkTrigger = new QTimer(this);
    m_blinkTrigger->setSingleShot(true);
    connect(m_blinkTrigger, &QTimer::timeout, this, &PetWidget::startBlink);

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setSingleShot(true);
    m_blinkTimer->setInterval(110);
    connect(m_blinkTimer, &QTimer::timeout, this, &PetWidget::endBlink);

    m_eatTimer = new QTimer(this);
    m_eatTimer->setSingleShot(true);
    connect(m_eatTimer, &QTimer::timeout, this, &PetWidget::endEating);

    m_animateTimer = new QTimer(this);
    m_animateTimer->setInterval(80);
    connect(m_animateTimer, &QTimer::timeout, this, [this] {
        ++m_tick;
        update();
    });

    // Runs only while scanning; drives the bob.
    m_scanTimer = new QTimer(this);
    m_scanTimer->setInterval(80);
    connect(m_scanTimer, &QTimer::timeout, this, [this] {
        ++m_scanTick;
        update();
    });

    // Kick off the first blink cycle, with a head start so it isn't jarring.
    m_blinkTrigger->start(3200);
}

PetWidget::~PetWidget() = default;

QSize PetWidget::sizeHint() const
{
    return QSize(WIDGET_W, WIDGET_H);
}

void PetWidget::setScanning(bool scanning)
{
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    if (scanning) {
        m_scanTick = 0;
        m_scanTimer->start();
        m_blinkTrigger->stop();
        if (m_state == State::Blink) {
            m_state = State::Idle;
            m_tick = 0;
        }
    } else {
        m_scanTimer->stop();
        if (!m_paused && m_state != State::Eating)
            m_blinkTrigger->start(2200 + int(QRandomGenerator::global()->bounded(1300)));
    }
    update();
}

void PetWidget::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    if (paused) {
        m_blinkTimer->stop();
        m_blinkTrigger->stop();
        if (m_state == State::Blink) {
            m_state = State::Idle;
            m_tick = 0;
        }
    } else if (!m_scanning && m_state != State::Eating) {
        m_blinkTrigger->start(1600 + int(QRandomGenerator::global()->bounded(900)));
    }
    update();
}

void PetWidget::setStatusColor(const QColor& color)
{
    if (m_statusColor == color)
        return;
    m_statusColor = color;
    update();
}

void PetWidget::startBlink()
{
    if (m_state == State::Eating || m_scanning || m_paused)
        return;
    m_state = State::Blink;
    m_tick = 0;
    m_blinkTimer->start();
    update();
}

void PetWidget::endBlink()
{
    m_state = State::Idle;
    m_tick = 0;
    m_blinkTrigger->start(2200 + int(QRandomGenerator::global()->bounded(1300)));
    update();
}

void PetWidget::eat(const QString& word)
{
    Q_UNUSED(word); // the feedback is the swallow animation itself; the bar's
                    // status line already reports the word was marked known.
    m_state = State::Eating;
    m_tick = 0;
    m_blinkTimer->stop();
    m_blinkTrigger->stop();
    m_animateTimer->start();
    m_eatTimer->start(9 * m_animateTimer->interval());
    update();
}

void PetWidget::endEating()
{
    m_animateTimer->stop();
    m_state = State::Idle;
    m_tick = 0;
    if (!m_scanning && !m_paused)
        m_blinkTrigger->start(2600 + int(QRandomGenerator::global()->bounded(1200)));
    update();
}

void PetWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false); // crisp pixel edges

    // Vertical offset: TOP_MARGIN leaves headroom for the scanning hop; eating
    // adds a little "gulp" dip on top of it.
    int dy = TOP_MARGIN;
    if (m_state == State::Eating) {
        if (m_tick >= 4 && m_tick <= 6)
            dy += 2;
        else if (m_tick == 7)
            dy -= 2;
    } else if (m_scanning && !m_paused) {
        dy += kScanBounce[m_scanTick % 8];
    }

    auto cell = [&](int x, int y, const QColor& c) {
        p.fillRect(x * CELL, y * CELL + dy, CELL, CELL, c);
    };

    // --- stem (green, rows 0..2) ---
    cell(3, 0, kStemDark); cell(4, 0, kStemDark); cell(5, 0, kStemDark); cell(6, 0, kStemDark);
    cell(2, 1, kStem); cell(3, 1, kStem); cell(4, 1, kStem); cell(5, 1, kStem);
    cell(6, 1, kStem); cell(7, 1, kStem);
    cell(3, 2, kStem); cell(4, 2, kStemDark); cell(5, 2, kStemDark); cell(6, 2, kStem);

    // --- body (purple, rounded eggplant silhouette) ---
    // row 3
    cell(3, 3, kBodyDark); cell(4, 3, kBody); cell(5, 3, kBody); cell(6, 3, kBodyDark);
    // row 4
    cell(2, 4, kBodyDark); cell(3, 4, kBody); cell(4, 4, kBody); cell(5, 4, kBody);
    cell(6, 4, kBody); cell(7, 4, kBodyDark);
    // row 5
    cell(2, 5, kBodyDark); cell(3, 5, kBody); cell(4, 5, kBody); cell(5, 5, kBody);
    cell(6, 5, kBody); cell(7, 5, kBodyDark);
    // row 6 (widest)
    cell(1, 6, kBodyDark); cell(2, 6, kBody); cell(3, 6, kBody); cell(4, 6, kBody);
    cell(5, 6, kBody); cell(6, 6, kBody); cell(7, 6, kBody); cell(8, 6, kBodyDark);
    // row 7 (widest)
    cell(1, 7, kBodyDark); cell(2, 7, kBody); cell(3, 7, kBody); cell(4, 7, kBody);
    cell(5, 7, kBody); cell(6, 7, kBody); cell(7, 7, kBody); cell(8, 7, kBodyDark);
    // row 8
    cell(2, 8, kBodyDark); cell(3, 8, kBody); cell(4, 8, kBody); cell(5, 8, kBody);
    cell(6, 8, kBody); cell(7, 8, kBodyDark);
    // row 9
    cell(3, 9, kBodyDark); cell(4, 9, kBody); cell(5, 9, kBody); cell(6, 9, kBodyDark);
    // row 10
    cell(4, 10, kBodyDark); cell(5, 10, kBodyDark);
    // row 11 (tip)
    cell(4, 11, kBodyDark); cell(5, 11, kBodyDark);

    // --- highlight ---
    cell(2, 4, kBodyLight);   // small glint on the upper-left

    // --- face ---
    const bool eyesClosed = (m_state == State::Blink) || (m_state == State::Eating)
                            || m_paused;
    if (eyesClosed) {
        // Closed eyes = short horizontal lines.
        cell(3, 5, kInk); cell(4, 5, kInk);
        cell(6, 5, kInk); cell(7, 5, kInk);
    } else {
        // Open eyes = white block with a dark pupil.
        cell(3, 5, kWhite); cell(4, 5, kWhite);
        cell(6, 5, kWhite); cell(7, 5, kWhite);
        cell(3, 5, kInk); cell(6, 5, kInk);
    }

    // --- mouth ---
    if (m_state == State::Eating) {
        // Mouth opens and closes across the swallow. A tiny white "food" dot
        // rides just above the lips for the first frames, then vanishes.
        const int open = (m_tick <= 1) ? 1 : (m_tick <= 3) ? 3 : (m_tick == 4) ? 2 : 1;
        for (int i = 0; i < open; ++i)
            cell(4, 8 + i, kMouth), cell(5, 8 + i, kMouth);
        // Lips outline for the open mouth.
        cell(3, 8, kInk); cell(6, 8, kInk);
        if (m_tick >= 2 && m_tick <= 3)
            cell(4, 7, kWhite), cell(5, 7, kWhite); // the word being swallowed
    } else {
        // Idle smile: a tiny dark curve.
        cell(4, 8, kInk); cell(5, 8, kInk);
        cell(4, 9, kBodyDark); cell(5, 9, kBodyDark);
    }

    // --- status LED at the feet ---
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool pulse = m_scanning && !m_paused && (m_scanTick % 8) < 4;
    const QPointF led(WIDGET_W - 4.0, WIDGET_H - 4.0);
    QColor glow = m_statusColor;
    glow.setAlpha(pulse ? 90 : 55);
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(led, pulse ? 5.6 : 5.0, pulse ? 5.6 : 5.0);
    p.setBrush(m_statusColor);
    p.drawEllipse(led, 2.6, 2.6);
    p.setPen(QPen(QColor(0, 0, 0, 110), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(led, 2.6, 2.6);
}
