#pragma once

#include <QFont>
#include <QPoint>
#include <QString>
#include <QWidget>

class QTimer;

/// A single pill shaped word inside the overlay bar.
class WordChip : public QWidget
{
    Q_OBJECT
public:
    explicit WordChip(const QString& word, QWidget* parent = nullptr);

    QString word() const { return m_word; }

    void setCount(int count);
    void setFresh(bool fresh);
    void setChipFont(const QFont& font);
    /// Brief visual feedback that this chip's word was copied successfully.
    void markCopied();

    static int chipHeightFor(const QFont& font);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activated(const QString& word);
    void menuRequested(const QString& word, const QPoint& globalPos);
    /// Emitted when the user swipes the pill vertically to mark it as known.
    void knownBySwipe(const QString& word);
    /// Emitted after the pointer rests on the chip for a moment.
    void hoverStarted(WordChip* chip);
    void hoverEnded(WordChip* chip);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    QString m_word;
    int m_count = 1;
    bool m_hover = false;
    bool m_pressed = false;
    bool m_swiping = false;
    bool m_fresh = false;
    bool m_copied = false;
    QPoint m_pressPos;
    QFont m_font;
    int m_padX = 10;
    QTimer* m_hoverTimer = nullptr;
};
