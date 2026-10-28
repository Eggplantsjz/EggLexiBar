#pragma once

#include <QString>
#include <QWidget>

class QTimer;

/// A tiny pixel-art eggplant ("digital pet") that sits on the left of the bar,
/// between the recognition status and the word chips. It blinks while idle and
/// plays a short "eating" animation whenever the user swipes a word up/down to
/// mark it known — the eaten word pops up in a little speech bubble.
class PetWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PetWidget(QWidget* parent = nullptr);
    ~PetWidget() override;

    QSize sizeHint() const override;

    /// Plays the "eat the word" animation and shows @p word in a speech bubble.
    void eat(const QString& word);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    enum class State { Idle, Blink, Eating };

    void startBlink();
    void endBlink();
    void endEating();

    State m_state = State::Idle;
    int m_tick = 0;      ///< animation frame counter

    QTimer* m_blinkTrigger = nullptr;
    QTimer* m_blinkTimer = nullptr;
    QTimer* m_eatTimer = nullptr;
    QTimer* m_animateTimer = nullptr;
};