#pragma once

#include "Settings.h"
#include "WordSession.h"
#include "Dictionary.h"

#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

class QLabel;
class QHBoxLayout;
class QVBoxLayout;
class QToolButton;
class QTimer;
class QVariantAnimation;
class QGraphicsOpacityEffect;
class WordChip;
class Dictionary;
class DefinitionCard;
class PetWidget;

/// The always-on-top status bar that shows the words found on screen.
///
/// The bar has a fixed width derived from the screen, so words appearing or
/// disappearing never resize it. Words that do not fit on one row are split
/// into pages; the "1/3" pill flips pages. When nobody is using the bar, the
/// eggplant nibbles the words one by one, flips to the next page when a page is
/// empty and wraps back to the first page after the last one.
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
    /// Re-evaluates the blue "new word" tint without touching the layout.
    void refreshFreshness();
    /// Re-lays the current page of chips (the bar width itself never changes).
    void updateWidth();
    /// Picks a stable width for the bar from the screen it currently sits on.
    void applyBarWidth();
    /// Measures the room left for word pills after every fixed widget.
    void measureChipBudget();
    /// Greedily splits the chip strip into pages that fit the budget.
    QVector<QVector<WordChip*>> paginateChips() const;
    /// Width reserved for the "1/3" pill.
    int pagePillWidth() const;
    /// Shows the current page, minus the words the pet already nibbled away.
    void layoutChips();
    /// Advances one page, wrapping back to the first page after the last.
    void nextPage();
    /// Idle animation: the pet eats words when nobody is using the bar.
    void scheduleSnack(int delayMs = -1);
    void startSnack();
    void finishSnack();
    void cancelSnack();
    void updateSnackVisual(qreal progress);
    bool canSnack() const;
    /// Centre of the pet's mouth, in bar coordinates.
    QPointF petMouth() const;
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
    QToolButton* m_pageBtn = nullptr;    // "2/4" — flips to the next page
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

    // Idle "pet eats a word" animation.
    QLabel* m_flyWord = nullptr;
    QGraphicsOpacityEffect* m_flyOpacity = nullptr;
    QVariantAnimation* m_flyAnim = nullptr;
    QTimer* m_snackTimer = nullptr;
    WordChip* m_snackChip = nullptr;
    QString m_snackWord;
    QPointF m_flyFrom;
    QSet<QString> m_eaten;   ///< words of the current page already eaten by the pet
    int m_page = 0;

    int m_barWidth = 0;      ///< width fixed from the screen size
    int m_chipBudget = 0;    ///< room for pills on one page

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
