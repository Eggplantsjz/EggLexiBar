#pragma once

#include "Settings.h"
#include "WordSession.h"

#include <QPoint>
#include <QVector>
#include <QWidget>

class QLabel;
class QScrollArea;
class QHBoxLayout;
class QToolButton;
class QTimer;
class StatusDot;
class WordChip;
class Dictionary;
class DefinitionCard;

/// The always-on-top status bar that shows the words found on screen.
class OverlayBar : public QWidget
{
    Q_OBJECT
public:
    explicit OverlayBar(QWidget* parent = nullptr);

    void applyConfig(const AppConfig& cfg);
    void setDictionary(const Dictionary* dictionary);
    void setWords(const QVector<WordEntry>& words);
    /// Geometry of the hover card while it is visible (used to exclude it from OCR).
    QRect popupGeometry() const;
    void setStatus(const QString& text, const QString& kind = QStringLiteral("idle"));
    void setPaused(bool paused);
    void setScanning(bool scanning);
    void setEngineReady(bool ready);
    void flashMessage(const QString& text);

signals:
    void pauseToggled();
    void detailsRequested();
    void settingsRequested();
    void hideRequested();
    void copyRequested(const QString& word);
    void markKnownRequested(const QString& word);
    void blockRequested(const QString& word);
    /// The user actually looked at this word (used to protect it from auto-snooze).
    void wordInspected(const QString& word);
    void barMoved(const QPoint& pos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    WordChip* findChip(const QString& word) const;
    void syncChips(const QVector<WordEntry>& words);
    void updateWidth();
    /// True while the pointer is inside the bar (used to freeze layout changes).
    bool pointerIsOnBar() const;
    /// True while the pointer is close enough to be aiming at the bar.
    bool pointerIsNearBar() const;
    void centerOnScreen();
    void clampToScreen();
    void placeAtSavedPosition();
    void updateDot();
    void showWordMenu(const QString& word, const QPoint& globalPos);
    void showWordCard(WordChip* chip);
    void hideWordCard(WordChip* chip = nullptr);
    void hideWordCardNow();
    /// Re-applies the newest word list after a frozen (pointer-over-bar) layout.
    void refreshLayout();
    void startAutoHideTimer();

    AppConfig m_cfg;
    StatusDot* m_dot = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_count = nullptr;
    QLabel* m_hint = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_chipHost = nullptr;
    QHBoxLayout* m_chipLayout = nullptr;
    QToolButton* m_moreBtn = nullptr;
    QToolButton* m_pauseBtn = nullptr;
    QToolButton* m_listBtn = nullptr;
    QToolButton* m_settingsBtn = nullptr;
    QToolButton* m_hideBtn = nullptr;
    QTimer* m_autoHideTimer = nullptr;
    QTimer* m_flashTimer = nullptr;
    QTimer* m_cardHideTimer = nullptr;
    QVector<WordChip*> m_chips;
    const Dictionary* m_dictionary = nullptr;
    DefinitionCard* m_card = nullptr;
    QString m_cardWord;

    QString m_statusText;
    QString m_statusKind = QStringLiteral("idle");
    QString m_flashText;
    int m_statusWidth = 0;
    QVector<WordEntry> m_lastWords;
    bool m_paused = false;
    bool m_scanning = false;
    bool m_engineReady = true;
    bool m_userMoved = false;
    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_pressGlobal;
    QPoint m_dragOffset;
};
