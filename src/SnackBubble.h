#pragma once

#include "Dictionary.h"

#include <QRect>
#include <QWidget>

class QLabel;
class QTimer;
class QPropertyAnimation;

/// The little speech bubble the pet shows after swallowing a word: the word,
/// its phonetic and the first couple of Chinese senses.
///
/// The bar itself is only ~57 px tall, so the bubble is a separate top-level
/// window anchored under the pet (or above it when the bar sits at the bottom
/// of the screen). It never takes focus or mouse input: it is a passive label
/// that fades in, lingers long enough to read, then fades out.
class SnackBubble : public QWidget
{
    Q_OBJECT
public:
    explicit SnackBubble(QWidget* parent = nullptr);

    /// Shows the word the pet just ate. @p anchor is the pet's global
    /// geometry; the bubble points at it.
    void showFor(const WordDefinition& definition, const QRect& anchor);

    /// Hides immediately (the user touched the bar, paused, turned the page…).
    void hideNow();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void fadeTo(qreal target, int durationMs);

    QLabel* m_text = nullptr;
    QTimer* m_holdTimer = nullptr;
    QPropertyAnimation* m_fade = nullptr;
    /// True when the bubble sits below the pet, so the triangle is on top.
    bool m_pointerUp = true;
    int m_pointerX = 24;
};
