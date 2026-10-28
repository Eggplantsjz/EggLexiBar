#pragma once

#include "Dictionary.h"

#include <QRect>
#include <QWidget>

class QLabel;
class QPushButton;

/// A small dark popup that shows the meaning of a word above the overlay bar.
class DefinitionCard : public QWidget
{
    Q_OBJECT
public:
    explicit DefinitionCard(QWidget* parent = nullptr);

    /// Places the card above \a anchor (below it when there is no room).
    void showFor(const WordDefinition& definition, const QRect& anchor);

signals:
    void markKnownRequested(const QString& word);
    void blockRequested(const QString& word);
    void copyRequested(const QString& word);
    /// True while the pointer is inside the card, so the bar can keep it open.
    void hoverChanged(bool inside);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QLabel* m_text = nullptr;
    QWidget* m_actions = nullptr;
    QString m_word;
    /// True when the card sits above the anchor → the triangle points down.
    bool m_anchorBelow = true;
    /// X-position of the triangle pointer relative to the card's left edge.
    int m_pointerX = 0;
};
