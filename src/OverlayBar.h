#pragma once

#include "Settings.h"
#include "WordSession.h"
#include "Dictionary.h"

#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPoint>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

class QLabel;
class QHBoxLayout;
class QVBoxLayout;
class QToolButton;
class QTimer;
class WordChip;
class Dictionary;
class DefinitionCard;
class PetWidget;

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
    /// Updates the capture-mode button glyph/tooltip after cfg changes.
    void setCaptureMode(int mode);

signals:
    void pauseToggled();
    void detailsRequested();
    void settingsRequested();
    void hideRequested();
    void captureModeToggled();
    void regionSelectRequested();
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

private:
    WordChip* findChip(const QString& word) const;
    void syncChips(const QVector<WordEntry>& words);
    /// Recomputes how many chips fit and resizes the bar to its content.
    void updateWidth();
    /// Shows/hides the textual status; the pet carries the state otherwise.
    void updateStatusLabel();
    /// Drives the pet's motion and status dot.
    void updatePet();
    /// True while the pointer is inside the bar (used to freeze layout changes).
    bool pointerIsOnBar() const;
    /// True while the pointer is close enough to be aiming at the bar.
    bool pointerIsNearBar() const;
    void centerOnScreen();
    void clampToScreen();
    void placeAtSavedPosition();
    void showWordMenu(const QString& word, const QPoint& globalPos);
    void showWordCard(WordChip* chip);
    void hideWordCard(WordChip* chip = nullptr);
    void hideWordCardNow();
    /// Re-applies the newest word list after a frozen (pointer-over-bar) layout.
    void refreshLayout();
    /// Retries refreshLayout() until the pointer is far enough away to resize.
    void applyPendingLayout(int attempt);
    void startAutoHideTimer();

    AppConfig m_cfg;
    QVBoxLayout* m_outer = nullptr;
    QHBoxLayout* m_row = nullptr;
    QLabel* m_status = nullptr;
    PetWidget* m_pet = nullptr;
    QLabel* m_count = nullptr;
    QLabel* m_hint = nullptr;
    QWidget* m_chipHost = nullptr;
    QHBoxLayout* m_chipLayout = nullptr;
    QToolButton* m_moreBtn = nullptr;
    QToolButton* m_pauseBtn = nullptr;
    QToolButton* m_scopeBtn = nullptr;   // capture-mode quick toggle
    QToolButton* m_adjustBtn = nullptr;  // draw custom region (visible only in mode==3)
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
    QVector<WordEntry> m_lastWords;
    bool m_paused = false;
    bool m_scanning = false;
    bool m_engineReady = true;
    bool m_userMoved = false;
    bool m_dragging = false;
    bool m_dragMoved = false;
    bool m_refreshScheduled = false;
    QPoint m_pressGlobal;
    QPoint m_dragOffset;
};
