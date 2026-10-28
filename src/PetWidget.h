#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class QTimer;

/// A tiny pixel-art eggplant that doubles as the bar's status indicator.
///
/// - Idle: blinks every few seconds, green status LED.
/// - Scanning: bobs up and down with a blue LED, so "the pet moves" means the
///   recogniser is working.
/// - Paused: closes its eyes and shows an amber LED.
/// - Error: red LED (the detailed message appears next to the pet).
///
/// It also plays a short "eating" animation whenever the user swipes a word
/// up/down to mark it known.
class PetWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PetWidget(QWidget* parent = nullptr);
    ~PetWidget() override;

    QSize sizeHint() const override;

    /// Bob up and down while the screen is being recognised.
    void setScanning(bool scanning);
    /// Close the eyes while recognition is paused.
    void setPaused(bool paused);
    /// Colour of the small status LED drawn at the pet's feet.
    void setStatusColor(const QColor& color);

    /// Plays the "eat the word" animation.
    void eat(const QString& word);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    enum class State { Idle, Blink, Eating };

    void startBlink();
    void endBlink();
    void endEating();

    State m_state = State::Idle;
    int m_tick = 0;       ///< animation frame counter (blink / eat)
    int m_scanTick = 0;   ///< bounce phase counter
    bool m_scanning = false;
    bool m_paused = false;
    QColor m_statusColor{0x4A, 0xDE, 0x80};

    QTimer* m_blinkTrigger = nullptr;
    QTimer* m_blinkTimer = nullptr;
    QTimer* m_eatTimer = nullptr;
    QTimer* m_animateTimer = nullptr;
    QTimer* m_scanTimer = nullptr;
};
