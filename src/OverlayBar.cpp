#include "OverlayBar.h"

#include "DefinitionCard.h"
#include "Dictionary.h"
#include "PetWidget.h"
#include "SnackBubble.h"
#include "WordChip.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QLinearGradient>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace {

const QColor kIdleColor(74, 222, 128);    // green
const QColor kScanColor(122, 162, 247);   // blue
const QColor kWarnColor(251, 191, 36);    // amber
const QColor kErrorColor(248, 113, 113);  // red

/// True when both lists contain the same words, regardless of order or hit
/// counts: an OCR refresh that only bumps a counter must not disturb the bar.
bool sameWordSet(const QVector<WordEntry>& a, const QVector<WordEntry>& b)
{
    if (a.size() != b.size())
        return false;
    QStringList wordsA;
    QStringList wordsB;
    wordsA.reserve(a.size());
    wordsB.reserve(b.size());
    for (const WordEntry& entry : a)
        wordsA.append(entry.word);
    for (const WordEntry& entry : b)
        wordsB.append(entry.word);
    wordsA.sort();
    wordsB.sort();
    return wordsA == wordsB;
}

int randomBetween(int lowMs, int highMs)
{
    return lowMs + int(QRandomGenerator::global()->bounded(quint32(highMs - lowMs + 1)));
}

} // namespace

QToolButton* makeToolButton(QWidget* parent, const QString& glyph, const QString& tooltip)
{
    auto* button = new QToolButton(parent);
    button->setText(glyph);
    button->setToolTip(tooltip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(24, 24);
    button->setAutoRaise(true);
    QFont font(QStringLiteral("Segoe UI Symbol"));
    font.setPixelSize(12);
    button->setFont(font);
    return button;
}

OverlayBar::OverlayBar(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool
                   | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setWindowTitle(QStringLiteral("WordLens"));
    setMouseTracking(true);

    // Single row: pet (status + mascot in one), optional message, word pills,
    // then the counter and the button cluster on the right.
    m_outer = new QVBoxLayout(this);
    m_outer->setContentsMargins(12, 4, 10, 4);
    m_outer->setSpacing(0);

    m_row = new QHBoxLayout;
    m_row->setSpacing(10);

    // The pixel-art eggplant doubles as the recognition indicator: it bobs while
    // scanning and carries a small status LED, so there is no separate dot or
    // permanent status label eating space on the left.
    m_pet = new PetWidget(this);
    m_row->addWidget(m_pet, 0, Qt::AlignVCenter);

    // Only errors, the paused state and transient messages need words. The label
    // is hidden otherwise, letting the words start right next to the pet.
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("barStatus"));
    m_status->hide();
    m_row->addWidget(m_status, 0, Qt::AlignVCenter);

    m_hint = new QLabel(QStringLiteral("等屏幕上的英文出现生词…"), this);
    m_hint->setObjectName(QStringLiteral("barHint"));
    m_row->addWidget(m_hint, 0, Qt::AlignVCenter);

    m_chipHost = new QWidget(this);
    m_chipHost->setAutoFillBackground(false);
    m_chipLayout = new QHBoxLayout(m_chipHost);
    m_chipLayout->setContentsMargins(0, 0, 0, 0);
    m_chipLayout->setSpacing(10);
    // The strip keeps a fixed width (so the page pill never moves); the trailing
    // stretch keeps the pills packed to the left instead of spreading them out.
    m_chipLayout->addStretch(1);
    m_chipHost->hide();
    m_row->addWidget(m_chipHost, 0, Qt::AlignVCenter);

    m_pageBtn = new QToolButton(this);
    m_pageBtn->setObjectName(QStringLiteral("barPage"));
    m_pageBtn->setCursor(Qt::PointingHandCursor);
    m_pageBtn->setAutoRaise(true);
    m_pageBtn->setFixedHeight(24);
    QFont moreFont(QStringLiteral("Microsoft YaHei UI"));
    moreFont.setPixelSize(11);
    m_pageBtn->setFont(moreFont);
    m_pageBtn->setToolTip(QStringLiteral("翻到下一页（最后一页之后回到第一页）"));
    m_pageBtn->hide();
    m_row->addWidget(m_pageBtn, 0, Qt::AlignVCenter);

    m_row->addStretch(1);

    m_count = new QLabel(QStringLiteral("0 生词"), this);
    m_count->setObjectName(QStringLiteral("barCount"));
    m_count->setToolTip(QStringLiteral("本次识别到的生词数量"));
    m_row->addWidget(m_count, 0, Qt::AlignVCenter);

    m_pauseBtn = makeToolButton(this, QStringLiteral("❚❚"), QStringLiteral("暂停 / 继续识别"));
    m_scopeBtn = makeToolButton(this, QStringLiteral("▢"), QStringLiteral("识别范围：全屏（点击切换）"));
    m_adjustBtn = makeToolButton(this, QStringLiteral("⊞"), QStringLiteral("拖动调整识别区域"));
    m_adjustBtn->hide();
    m_listBtn = makeToolButton(this, QStringLiteral("☰"), QStringLiteral("生词列表"));
    m_settingsBtn = makeToolButton(this, QStringLiteral("⚙"), QStringLiteral("设置"));
    m_hideBtn = makeToolButton(this, QStringLiteral("✕"), QStringLiteral("隐藏状态栏（Ctrl+Alt+W 唤回）"));
    m_row->addWidget(m_pauseBtn);
    m_row->addWidget(m_scopeBtn);
    m_row->addWidget(m_adjustBtn);
    m_row->addWidget(m_listBtn);
    m_row->addWidget(m_settingsBtn);
    m_row->addWidget(m_hideBtn);

    m_outer->addLayout(m_row, 0);

    setStyleSheet(QStringLiteral(
        "QLabel#barStatus{color:#C6CDDB;}"
        "QLabel#barCount{color:#98A1B4;}"
        "QLabel#barHint{color:#7F889C;}"
        "QToolButton{border:none;border-radius:8px;color:#CBD3E1;padding:2px;}"
        "QToolButton:hover{background:rgba(255,255,255,38);color:#F2F5FA;}"
        "QToolButton:pressed{background:rgba(255,255,255,64);}"
        "QToolButton#barPage{color:#9CC1FF;border:1px solid rgba(122,162,247,90);"
        "border-radius:11px;padding:0 10px;}"
        "QToolButton#barPage:hover{background:rgba(122,162,247,50);color:#EAF1FF;}"
        "QLabel#flyWord{background:rgba(58,64,84,240);border-radius:12px;"
        "color:#E9EDF7;padding:4px 12px;}"));

    connect(m_pauseBtn, &QToolButton::clicked, this, &OverlayBar::pauseToggled);
    connect(m_scopeBtn, &QToolButton::clicked, this, [this] {
        // Cycle: 0 (fullscreen) → 1 (active window) → 2 (cursor region) → 3 (custom) → 0
        const int nextMode = (m_cfg.captureMode + 1) % 4;
        emit captureModeToggled();
        setCaptureMode(nextMode);
    });
    connect(m_adjustBtn, &QToolButton::clicked, this, &OverlayBar::regionSelectRequested);
    connect(m_listBtn, &QToolButton::clicked, this, &OverlayBar::detailsRequested);
    connect(m_settingsBtn, &QToolButton::clicked, this, &OverlayBar::settingsRequested);
    connect(m_hideBtn, &QToolButton::clicked, this, &OverlayBar::hideRequested);
    connect(m_pageBtn, &QToolButton::clicked, this, [this] { nextPage(); });

    m_autoHideTimer = new QTimer(this);
    m_autoHideTimer->setSingleShot(true);
    connect(m_autoHideTimer, &QTimer::timeout, this, [this] {
        if (m_cfg.autoHide && !m_dragging && !underMouse())
            hide();
    });

    m_cardHideTimer = new QTimer(this);
    m_cardHideTimer->setSingleShot(true);
    m_cardHideTimer->setInterval(240);
    connect(m_cardHideTimer, &QTimer::timeout, this, [this] {
        if (!m_card || !m_card->isVisible())
            return;
        if (m_card->underMouse())
            m_cardHideTimer->start();
        else
            hideWordCardNow();
    });

    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    m_flashTimer->setInterval(1400);
    connect(m_flashTimer, &QTimer::timeout, this, [this] {
        m_flashText.clear();
        setStatus(m_statusText, m_statusKind);
    });

    // --- idle snack: the pet eats a word when the bar is left alone ---------
    m_snackTimer = new QTimer(this);
    m_snackTimer->setSingleShot(true);
    connect(m_snackTimer, &QTimer::timeout, this, &OverlayBar::startSnack);

    // The "word" that flies from the strip into the pet's mouth. It is a child
    // of the bar (not of the chip) so it can leave the chip rect and stay above
    // the other children while it travels.
    m_flyWord = new QLabel(this);
    m_flyWord->setObjectName(QStringLiteral("flyWord"));
    m_flyWord->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_flyWord->hide();

    m_flyOpacity = new QGraphicsOpacityEffect(m_flyWord);
    m_flyOpacity->setOpacity(1.0);
    m_flyWord->setGraphicsEffect(m_flyOpacity);

    m_flyAnim = new QVariantAnimation(this);
    m_flyAnim->setStartValue(0.0);
    m_flyAnim->setEndValue(1.0);
    m_flyAnim->setDuration(460);
    m_flyAnim->setEasingCurve(QEasingCurve::InCubic);
    connect(m_flyAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { updateSnackVisual(value.toReal()); });
    connect(m_flyAnim, &QVariantAnimation::finished, this, &OverlayBar::finishSnack);

    // The bar is too short to print a definition, so the pet's meal shows up in
    // a small bubble under its mouth for a couple of seconds.
    m_snackBubble = new SnackBubble(this);
}

void OverlayBar::applyConfig(const AppConfig& cfg)
{
    m_cfg = cfg;
    m_userMoved = cfg.barPosSet;
    setWindowOpacity(1.0);

    const int fontPx = qBound(10, cfg.fontSize, 26);

    QFont wordFont(QStringLiteral("Segoe UI"));
    wordFont.setPixelSize(fontPx);

    QFont smallFont(QStringLiteral("Microsoft YaHei UI"));
    smallFont.setPixelSize(qMax(11, fontPx - 1));

    m_status->setFont(smallFont);
    m_count->setFont(smallFont);
    m_hint->setFont(smallFont);

    // The counter keeps a fixed width so scans do not jiggle the buttons.
    const QFontMetrics smallMetrics(smallFont);
    m_count->setFixedWidth(smallMetrics.horizontalAdvance(
                               QStringLiteral("888 生词")) + 6);

    for (WordChip* chip : m_chips)
        chip->setChipFont(wordFont);

    const int chipHeight = WordChip::chipHeightFor(wordFont);
    // Single row of chips; the pixel-art pet is the tallest element, so it
    // governs the bar height when the font is small.
    const int petHeight = m_pet ? m_pet->sizeHint().height() : 0;
    setFixedHeight(qMax(chipHeight + 18, petHeight + 12));

    // Update the capture-mode button visibility before measuring the row, so
    // the custom-region "adjust" button is part of the budget.
    setCaptureMode(m_cfg.captureMode);
    applyBarWidth();
    updateWidth();
    update();

    if (!m_cfg.barPosSet)
        centerOnScreen();
    else
        placeAtSavedPosition();

    cancelSnack();
    scheduleSnack();
}

void OverlayBar::setDictionary(const Dictionary* dictionary)
{
    m_dictionary = dictionary;
    if (!m_dictionary || !m_dictionary->isLoaded())
        hideWordCardNow();
}

void OverlayBar::setStatus(const QString& text, const QString& kind)
{
    m_statusText = text;
    m_statusKind = kind;
    updateStatusLabel();
    updatePet();
}

void OverlayBar::updateStatusLabel()
{
    const bool flash = !m_flashText.isEmpty();
    const QString shown = flash ? m_flashText : m_statusText;
    const bool important = flash
                           || m_statusKind == QLatin1String("error")
                           || m_statusKind == QLatin1String("warn")
                           || m_statusKind == QLatin1String("paused");
    const bool wantVisible = important && !shown.isEmpty();
    const bool visible = m_status->isVisible();

    // Do not grow/shrink the bar while the pointer is on it: that would slide the
    // buttons out from under a click in progress. The pending message is applied
    // by refreshLayout() once the pointer leaves.
    if (visible != wantVisible && pointerIsNearBar())
        return;

    if (!wantVisible) {
        m_status->hide();
        if (visible)
            updateWidth();
        return;
    }

    const QFontMetrics fm(m_status->font());
    m_status->setText(fm.elidedText(shown, Qt::ElideRight, 300));
    m_status->setToolTip(shown);
    if (flash)
        m_status->setStyleSheet(QStringLiteral("color:#9CC1FF;"));
    else if (m_statusKind == QLatin1String("error"))
        m_status->setStyleSheet(QStringLiteral("color:#FCA5A5;"));
    else
        m_status->setStyleSheet(QStringLiteral("color:#FCD34D;"));
    m_status->show();
    updateWidth();
}

void OverlayBar::updatePet()
{
    if (!m_pet)
        return;

    const bool scanning = m_scanning && !m_paused && m_engineReady;
    m_pet->setPaused(m_paused);
    m_pet->setScanning(scanning);

    QColor color = kIdleColor;
    QString tip = QStringLiteral("WordLens · 空闲");
    if (!m_engineReady) {
        color = kErrorColor;
        tip = QStringLiteral("未找到 Tesseract，请在设置中指定");
    } else if (m_paused) {
        color = kWarnColor;
        tip = QStringLiteral("已暂停识别");
    } else if (scanning) {
        color = kScanColor;
        tip = QStringLiteral("正在识别屏幕…");
    }
    m_pet->setStatusColor(color);
    m_pet->setToolTip(tip);
}

void OverlayBar::setPaused(bool paused)
{
    m_paused = paused;
    m_pauseBtn->setText(paused ? QStringLiteral("▶") : QStringLiteral("❚❚"));
    // While paused the user wants to look at the words, not lose them to a
    // hungry eggplant.
    if (paused)
        cancelSnack();
    else
        scheduleSnack();
    updatePet();
}

void OverlayBar::setCaptureMode(int mode)
{
    // 0 = whole screen, 1 = active window, 2 = cursor region, 3 = custom region
    static const QString glyphs[] = {
        QStringLiteral("▢"),   // fullscreen
        QStringLiteral("⊟"),   // active window
        QStringLiteral("◎"),   // cursor region
        QStringLiteral("◇"),   // custom region
    };
    static const QString tips[] = {
        QStringLiteral("识别范围：整个屏幕（点击切换）"),
        QStringLiteral("识别范围：当前窗口（点击切换）"),
        QStringLiteral("识别范围：鼠标附近 900×560（点击切换）"),
        QStringLiteral("识别范围：自定义区域（点击切换 · ⊞ 调整框选）"),
    };
    const int idx = qBound(0, mode, 3);
    m_scopeBtn->setText(glyphs[idx]);
    m_scopeBtn->setToolTip(tips[idx]);
    m_adjustBtn->setVisible(idx == 3);
}

void OverlayBar::setScanning(bool scanning)
{
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    updatePet();
}

void OverlayBar::setEngineReady(bool ready)
{
    m_engineReady = ready;
    if (!ready)
        cancelSnack();
    updatePet();
}

void OverlayBar::flashMessage(const QString& text)
{
    m_flashText = text;
    m_flashTimer->start();
    setStatus(m_statusText, m_statusKind);
}

QRect OverlayBar::popupGeometry() const
{
    if (m_card && m_card->isVisible())
        return m_card->frameGeometry();
    return {};
}

QRect OverlayBar::snackGeometry() const
{
    if (m_snackBubble && m_snackBubble->isVisible())
        return m_snackBubble->frameGeometry();
    return {};
}

void OverlayBar::setWords(const QVector<WordEntry>& words)
{
    const bool changed = !sameWordSet(m_lastWords, words);
    m_lastWords = words;
    m_count->setText(QStringLiteral("%1 生词").arg(words.size()));
    // Let the blue "new word" tint expire even while the layout is frozen:
    // otherwise a pointer resting on the bar keeps the chips blue forever.
    refreshFreshness();

    // A new word list invalidates the idle animation: whatever the pet already
    // nibbled away comes back so the strip matches the session again.
    if (changed)
        cancelSnack();

    // While the pointer is on (or near) the bar, or a card is open, freeze the
    // whole layout: re-ordering would slide the chips out from under the cursor.
    // The pending state is applied as soon as the pointer leaves (see
    // refreshLayout()).
    //
    // An empty word list is the exception: there is no chip to click any more,
    // and freezing would leave stale words on screen indefinitely.
    if (!m_chips.isEmpty() && !words.isEmpty()
        && (pointerIsNearBar() || (m_card && m_card->isVisible())))
        return;

    syncChips(words);
    updateWidth();

    if (changed && !m_chips.isEmpty())
        scheduleSnack();
}

void OverlayBar::refreshFreshness()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (WordChip* chip : m_chips) {
        qint64 firstSeen = -1;
        for (const WordEntry& entry : m_lastWords) {
            if (entry.word == chip->word()) {
                firstSeen = entry.firstSeen;
                break;
            }
        }
        // Chips that are only still on screen because the layout is frozen keep
        // their current look; there is no fresh entry to compare against.
        if (firstSeen < 0)
            continue;
        chip->setFresh(now - firstSeen < 4000);
    }
}

void OverlayBar::refreshLayout()
{
    if (m_refreshScheduled)
        return;
    m_refreshScheduled = true;
    applyPendingLayout(0);
}

void OverlayBar::applyPendingLayout(int attempt)
{
    // The layout is only frozen while the pointer is close, and that zone is
    // larger than the bar itself: a pointer parked just below it would never
    // fire another leaveEvent, so retry a few times instead of waiting for
    // one. After the last attempt we apply anyway to avoid stale chips.
    if (pointerIsNearBar() && attempt < 20) {
        QTimer::singleShot(350, this, [this, attempt] {
            applyPendingLayout(attempt + 1);
        });
        return;
    }

    m_refreshScheduled = false;
    updateStatusLabel();
    setWords(m_lastWords);
}

WordChip* OverlayBar::findChip(const QString& word) const
{
    for (WordChip* chip : m_chips) {
        if (chip->word() == word)
            return chip;
    }
    return nullptr;
}

void OverlayBar::syncChips(const QVector<WordEntry>& words)
{
    // Do not reshuffle the bar while the pointer is on the bar or on the
    // definition card: moving the chip under the cursor would close the card.
    const bool cardOpen = m_card && m_card->isVisible();
    const bool onBar = pointerIsOnBar();
    if (!m_chips.isEmpty() && (cardOpen || onBar)) {
        if (cardOpen) {
            bool cardWordStillListed = false;
            for (const WordEntry& entry : words) {
                if (entry.word == m_cardWord) {
                    cardWordStillListed = true;
                    break;
                }
            }
            if (!cardWordStillListed)
                hideWordCardNow();
        }
        for (WordChip* chip : m_chips) {
            for (const WordEntry& entry : words) {
                if (entry.word == chip->word()) {
                    chip->setCount(entry.count);
                    break;
                }
            }
        }
        return;
    }

    // A hard cap keeps widget churn bounded; updateWidth() decides how many of
    // these actually fit and collapses the rest into the "+N" pill.
    const int limit = qBound(1, words.size(), 60);
    QVector<WordEntry> wanted = words.mid(0, limit);

    // Collect current chips by word so we can reuse.
    QHash<QString, WordChip*> byWord;
    for (WordChip* c : m_chips)
        byWord.insert(c->word(), c);

    // Remove anything no longer wanted.
    for (auto it = m_chips.begin(); it != m_chips.end();) {
        WordChip* chip = *it;
        bool keep = false;
        for (const WordEntry& entry : wanted)
            if (entry.word == chip->word()) { keep = true; break; }
        if (!keep) {
            m_chipLayout->removeWidget(chip);
            if (m_card && m_card->isVisible() && m_cardWord == chip->word())
                hideWordCardNow();
            chip->hide();
            chip->deleteLater();
            it = m_chips.erase(it);
        } else {
            ++it;
        }
    }

    m_chips.clear();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int index = 0; index < wanted.size(); ++index) {
        const WordEntry& entry = wanted.at(index);
        WordChip* chip = byWord.value(entry.word);
        if (!chip) {
            // New word → a single chip, directly packed into the strip. Words
            // are shown with no definition underneath (the meaning appears in
            // the hover card only).
            chip = new WordChip(entry.word, m_chipHost);
            QFont wordFont(QStringLiteral("Segoe UI"));
            wordFont.setPixelSize(qBound(10, m_cfg.fontSize, 26));
            chip->setChipFont(wordFont);
            connect(chip, &WordChip::activated, this, [this](const QString& word) {
                emit copyRequested(word);
            });
            connect(chip, &WordChip::knownBySwipe, this, [this, chip](const QString& word) {
                hideWordCardNow();
                if (m_pet)
                    m_pet->eat(word);
                chip->hide();
                emit markKnownRequested(word);
            });
            connect(chip, &WordChip::menuRequested, this, &OverlayBar::showWordMenu);
            connect(chip, &WordChip::hoverStarted, this, &OverlayBar::showWordCard);
            connect(chip, &WordChip::hoverEnded, this, &OverlayBar::hideWordCard);

            m_chipLayout->insertWidget(qMax(0, m_chipLayout->count() - 1), chip, 0,
                                       Qt::AlignVCenter);
        }

        chip->setCount(entry.count);
        chip->setFresh(now - entry.firstSeen < 4000);

        chip->show();
        m_chips.append(chip);
    }
}

void OverlayBar::updateWidth()
{
    layoutChips();
    clampToScreen();
}

void OverlayBar::applyBarWidth()
{
    QScreen* target = screen() ? screen() : QGuiApplication::primaryScreen();
    const int screenWidth = target ? target->availableGeometry().width() : 1920;
    // Fixed by design: the bar never grows or shrinks when words appear, so the
    // buttons stay where the user expects them. Only the screen size decides.
    m_barWidth = qBound(560, qMin(1320, screenWidth - 200), 1320);
    setFixedWidth(m_barWidth);
}

void OverlayBar::measureChipBudget()
{
    // Widths are summed widget by widget instead of asking the row layout:
    // its cached sizeHint can still contain the chip strip and would collapse
    // the page to a single pill.
    int fixedWidth = 0;
    int fixedWidgets = 0;
    const auto addFixed = [&](QWidget* widget) {
        if (!widget || widget->isHidden())
            return;
        fixedWidth += widget->sizeHint().width();
        ++fixedWidgets;
    };
    addFixed(m_pet);
    addFixed(m_status);
    addFixed(m_hint);
    addFixed(m_count);
    addFixed(m_pauseBtn);
    addFixed(m_scopeBtn);
    addFixed(m_adjustBtn);
    addFixed(m_listBtn);
    addFixed(m_settingsBtn);
    addFixed(m_hideBtn);
    const QMargins margins = m_outer->contentsMargins();
    fixedWidth += margins.left() + margins.right();
    // Gaps: between every fixed widget, plus the slot for the strip, the page
    // pill and the right-side stretch.
    fixedWidth += (fixedWidgets + 3) * m_row->spacing();
    m_chipBudget = qMax(120, (m_barWidth > 0 ? m_barWidth : width()) - fixedWidth);
}

namespace {

/// Packs @p chips into pages no wider than @p budget, keeping list order.
QVector<QVector<WordChip*>> packChips(const QVector<WordChip*>& chips, int budget,
                                      int cap, int spacing)
{
    QVector<QVector<WordChip*>> pages;
    QVector<WordChip*> page;
    int used = 0;
    for (WordChip* chip : chips) {
        const int width = chip->sizeHint().width();
        const int extra = width + (page.isEmpty() ? 0 : spacing);
        const bool full = !page.isEmpty() && used + extra > budget;
        if (full || page.size() >= cap) {
            pages.append(page);
            page.clear();
            used = 0;
        }
        used += page.isEmpty() ? width : width + spacing;
        page.append(chip);
    }
    if (!page.isEmpty())
        pages.append(page);
    return pages;
}

} // namespace

int OverlayBar::pagePillWidth() const
{
    const QFontMetrics fm(m_pageBtn->font());
    return fm.horizontalAdvance(QStringLiteral("99/99")) + 26;
}

QVector<QVector<WordChip*>> OverlayBar::paginateChips() const
{
    const int cap = qBound(1, m_cfg.maxChips, 40);
    const int spacing = m_chipLayout->spacing();
    QVector<QVector<WordChip*>> pages = packChips(m_chips, m_chipBudget, cap, spacing);
    if (pages.size() > 1) {
        // The page pill only exists when there are several pages; make room for
        // it by repacking, so the last pill of a page can never overflow.
        pages = packChips(m_chips, qMax(120, m_chipBudget - pagePillWidth()), cap,
                          spacing);
    }
    return pages;
}

void OverlayBar::layoutChips()
{
    const bool hasChips = !m_chips.isEmpty();
    if (!hasChips)
        hideWordCardNow();

    m_hint->setVisible(!hasChips);
    // With no words at all the hint already says everything; a lone "0 生词"
    // next to it is just noise.
    m_count->setVisible(hasChips);
    m_chipHost->setVisible(hasChips);
    if (!hasChips) {
        m_pageBtn->hide();
        return;
    }

    measureChipBudget();
    const QVector<QVector<WordChip*>> pages = paginateChips();
    const int pageCount = qMax(1, pages.size());
    if (m_page >= pageCount)
        m_page = 0;

    // Keep the strip at a constant width so the page pill (and everything to its
    // right) stays put while the pet nibbles words away.
    m_chipHost->setFixedWidth(qMax(60, m_chipBudget - (pageCount > 1 ? pagePillWidth() : 0) - 2));

    // Words the pet has eaten stay hidden until the page is turned; everything
    // else on the current page is shown at its full width.
    for (WordChip* chip : m_chips)
        chip->hide();
    for (WordChip* chip : pages.value(m_page))
        chip->setVisible(!m_eaten.contains(chip->word()));

    if (pageCount > 1) {
        m_pageBtn->setText(QStringLiteral("%1/%2").arg(m_page + 1).arg(pageCount));
        m_pageBtn->setToolTip(
            QStringLiteral("第 %1 页 / 共 %2 页 · 点击翻页（最后一页之后回到第一页）")
                .arg(m_page + 1)
                .arg(pageCount));
        m_pageBtn->show();
    } else {
        m_pageBtn->hide();
    }
}

void OverlayBar::nextPage()
{
    const QVector<QVector<WordChip*>> pages = paginateChips();
    if (pages.size() <= 1)
        return;
    m_page = (m_page + 1) % pages.size();
    m_eaten.clear();
    if (m_flyAnim && m_flyAnim->state() == QAbstractAnimation::Running)
        m_flyAnim->stop();
    if (m_flyWord)
        m_flyWord->hide();
    if (m_snackBubble)
        m_snackBubble->hideNow();
    m_snackWord.clear();
    m_snackChip = nullptr;
    layoutChips();
    scheduleSnack();
}

QPointF OverlayBar::petMouth() const
{
    if (!m_pet)
        return QPointF(40, height() / 2.0);
    const QRect g = m_pet->geometry();
    return QPointF(g.center().x(), g.top() + g.height() * 0.72);
}

bool OverlayBar::canSnack() const
{
    return isVisible() && m_engineReady && !m_paused && !m_chips.isEmpty()
           && !pointerIsNearBar() && !(m_card && m_card->isVisible())
           && m_chipHost && !m_chipHost->isHidden()
           && (!m_flyAnim || m_flyAnim->state() != QAbstractAnimation::Running);
}

void OverlayBar::scheduleSnack(int delayMs)
{
    if (!m_snackTimer)
        return;
    m_snackTimer->start(delayMs > 0 ? delayMs : randomBetween(5000, 11000));
}

void OverlayBar::startSnack()
{
    if (!canSnack()) {
        scheduleSnack(6000);
        return;
    }

    const QVector<QVector<WordChip*>> pages = paginateChips();
    const QVector<WordChip*> page = pages.value(m_page);
    WordChip* victim = nullptr;
    for (WordChip* chip : page) {
        if (chip->isVisible()) {
            victim = chip;
            break;
        }
    }

    if (!victim) {
        // The page is already empty: turn to the next one and let the pet rest
        // for a moment before the next snack.
        if (pages.size() > 1) {
            nextPage();
            scheduleSnack(2600);
        } else {
            m_eaten.clear();
            layoutChips();
            scheduleSnack(5000);
        }
        return;
    }

    m_snackChip = victim;
    m_snackWord = victim->word();
    m_flyFrom = QPointF(victim->mapTo(this, QPoint(0, 0)).x() + victim->width() / 2.0,
                        victim->mapTo(this, QPoint(0, 0)).y() + victim->height() / 2.0);

    QFont flyFont(QStringLiteral("Segoe UI"));
    flyFont.setPixelSize(qBound(10, m_cfg.fontSize, 26));
    m_flyWord->setFont(flyFont);
    m_flyWord->setText(m_snackWord);
    m_flyWord->adjustSize();
    m_flyWord->raise();
    m_flyOpacity->setOpacity(1.0);
    m_flyWord->show();
    updateSnackVisual(0.0);

    victim->hide();
    m_flyAnim->start();
}

void OverlayBar::updateSnackVisual(qreal progress)
{
    if (!m_flyWord || m_flyWord->isHidden())
        return;

    const qreal t = qBound<qreal>(0.0, progress, 1.0);
    const QPointF from = m_flyFrom;
    const QPointF to = petMouth();
    // Slight hop: the word arcs upward before dropping into the mouth.
    const QPointF control((from.x() + to.x()) / 2.0,
                          qMin(from.y(), to.y()) - 12.0);
    const qreal u = 1.0 - t;
    const QPointF p = u * u * from + 2.0 * u * t * control + t * t * to;

    m_flyOpacity->setOpacity(qBound(0.0, 1.0 - t * t, 1.0));
    // Keep the sprite inside the bar: the arc can otherwise poke past the top
    // edge and get clipped mid-flight.
    const int x = qBound(0, qRound(p.x() - m_flyWord->width() / 2.0),
                         qMax(0, width() - m_flyWord->width()));
    const int y = qBound(0, qRound(p.y() - m_flyWord->height() / 2.0),
                         qMax(0, height() - m_flyWord->height()));
    m_flyWord->move(x, y);
    m_flyWord->raise();
}

void OverlayBar::finishSnack()
{
    if (m_flyWord)
        m_flyWord->hide();

    const QString word = m_snackWord;
    m_snackWord.clear();
    m_snackChip = nullptr;
    if (word.isEmpty())
        return;

    if (m_pet)
        m_pet->eat(word);
    m_eaten.insert(word);
    layoutChips();

    // Page finished → turn to the next one; after the last page, wrap to the
    // first page with all its words restored. The flip is done inline (instead
    // of nextPage()) so the meaning bubble for the last word of the page stays
    // on screen: the user just watched that word disappear.
    const QVector<QVector<WordChip*>> pages = paginateChips();
    bool anyLeft = false;
    for (WordChip* chip : pages.value(m_page)) {
        if (!m_eaten.contains(chip->word())) {
            anyLeft = true;
            break;
        }
    }

    if (!anyLeft && !pages.isEmpty()) {
        if (pages.size() > 1) {
            m_page = (m_page + 1) % pages.size();
            m_eaten.clear();
            layoutChips();
        } else {
            m_eaten.clear();
            layoutChips();
        }
    }

    // Show the word with its meaning, like the pet showing what it is chewing.
    if (m_dictionary && m_dictionary->isLoaded() && !pointerIsNearBar()) {
        const WordDefinition definition = m_dictionary->lookup(word);
        if (definition.isValid()) {
            const QRect anchor(m_pet->mapToGlobal(QPoint(0, 0)), m_pet->size());
            m_snackBubble->showFor(definition, anchor);
        }
    }

    // Slower than the old purely decorative pace: the bubble needs a moment to
    // be read before the next word flies in.
    scheduleSnack(anyLeft ? randomBetween(2800, 4200) : randomBetween(3000, 4400));
}

void OverlayBar::cancelSnack()
{
    if (m_snackTimer)
        m_snackTimer->stop();
    if (m_flyAnim && m_flyAnim->state() == QAbstractAnimation::Running)
        m_flyAnim->stop();
    if (m_flyWord)
        m_flyWord->hide();
    if (m_snackBubble)
        m_snackBubble->hideNow();
    m_snackWord.clear();
    m_snackChip = nullptr;

    if (m_eaten.isEmpty())
        return;
    m_eaten.clear();
    // The words come back immediately: the pointer is heading for the bar and
    // the user expects to see the whole list.
    layoutChips();
}

bool OverlayBar::pointerIsOnBar() const
{
    return isVisible() && rect().contains(mapFromGlobal(QCursor::pos()));
}

bool OverlayBar::pointerIsNearBar() const
{
    if (!isVisible())
        return false;
    // Generous margin: people approach the bar from below, so the frozen zone
    // extends further down than up.
    const QRect reach = rect().adjusted(-80, -40, 80, 160);
    return reach.contains(mapFromGlobal(QCursor::pos()));
}

void OverlayBar::centerOnScreen()
{
    QScreen* target = screen();
    if (!target)
        target = QGuiApplication::primaryScreen();
    if (!target)
        return;
    const QRect available = target->availableGeometry();
    const int x = available.x() + (available.width() - width()) / 2;
    const int y = available.y() + 8;
    move(x, y);
}

void OverlayBar::clampToScreen()
{
    // Prefer the screen that owns the bar's top-left corner. With a fixed width
    // the bar can reach past a narrow secondary monitor, and using the centre
    // would fall outside every screen and yank the bar to the primary.
    QScreen* target = QGuiApplication::screenAt(pos());
    if (!target)
        target = QGuiApplication::screenAt(frameGeometry().center());
    if (!target)
        target = screen();
    if (!target)
        target = QGuiApplication::primaryScreen();
    if (!target)
        return;
    const QRect available = target->availableGeometry();
    const int nx = qBound(available.left(), x(), available.right() - width() + 1);
    const int ny = qBound(available.top(), y(), available.bottom() - height() + 1);
    if (nx != x() || ny != y())
        move(nx, ny);
}

void OverlayBar::placeAtSavedPosition()
{
    if (m_cfg.barPosSet) {
        move(m_cfg.barPos);
        clampToScreen();
    } else {
        centerOnScreen();
    }
}

void OverlayBar::showWordMenu(const QString& word, const QPoint& globalPos)
{
    QMenu menu;
    QAction* copyAction = menu.addAction(QStringLiteral("复制“%1”").arg(word));
    menu.addSeparator();
    QAction* knownAction = menu.addAction(QStringLiteral("标记为已掌握"));
    QAction* blockAction = menu.addAction(QStringLiteral("不再显示此词"));

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    if (chosen == copyAction)
        emit copyRequested(word);
    else if (chosen == knownAction)
        emit markKnownRequested(word);
    else if (chosen == blockAction)
        emit blockRequested(word);
}

void OverlayBar::showWordCard(WordChip* chip)
{
    // Hover chip → show the floating DefinitionCard (full senses, phonetic,
    // origin tags, action buttons). The bar itself only shows the bare words.
    if (m_cardHideTimer) m_cardHideTimer->stop();
    if (!chip || !isVisible() || !m_dictionary || !m_dictionary->isLoaded())
        return;

    cancelSnack();

    const WordDefinition definition = m_dictionary->lookup(chip->word());
    if (!definition.isValid()) return;

    emit wordInspected(chip->word());
    m_cardWord = chip->word();

    if (!m_card) {
        m_card = new DefinitionCard;
        connect(m_card, &DefinitionCard::markKnownRequested, this, [this](const QString& w) {
            hideWordCardNow(); emit markKnownRequested(w);
        });
        connect(m_card, &DefinitionCard::blockRequested, this, [this](const QString& w) {
            hideWordCardNow(); emit blockRequested(w);
        });
        connect(m_card, &DefinitionCard::copyRequested, this, [this](const QString& w) {
            hideWordCardNow(); emit copyRequested(w);
        });
        connect(m_card, &DefinitionCard::hoverChanged, this, [this](bool inside) {
            if (inside) {
                if (m_cardHideTimer) m_cardHideTimer->stop();
            } else {
                hideWordCard();
            }
        });
    }

    const QRect anchor(chip->mapToGlobal(QPoint(0, 0)), chip->size());
    m_card->showFor(definition, anchor);
    m_card->raise();
}

void OverlayBar::hideWordCard(WordChip* chip)
{
    Q_UNUSED(chip);
    if (!m_card || !m_card->isVisible()) return;
    if (!m_cardHideTimer) {
        m_cardHideTimer = new QTimer(this);
        m_cardHideTimer->setSingleShot(true);
    }
    m_cardHideTimer->start(200);  // grace period to reach card buttons
}

void OverlayBar::hideWordCardNow()
{
    if (m_cardHideTimer) m_cardHideTimer->stop();
    if (m_card && m_card->isVisible()) {
        m_card->hide();
        m_cardWord.clear();
    }
}

void OverlayBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rectF = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const int alpha = qBound(60, int(255 * m_cfg.opacityPercent / 100.0), 255);

    // Soft vertical gradient (slightly lighter at the top) instead of a flat fill.
    QLinearGradient gradient(rectF.topLeft(), rectF.bottomLeft());
    QColor top(30, 33, 42, alpha);
    QColor bottom(16, 18, 24, alpha);
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);

    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.setBrush(gradient);
    p.drawRoundedRect(rectF, 14, 14);

    // Subtle top highlight line for a slightly "lifted" edge.
    p.setPen(QPen(QColor(255, 255, 255, 18), 1));
    p.drawLine(QPointF(rectF.left() + 14, rectF.top() + 1),
               QPointF(rectF.right() - 14, rectF.top() + 1));
}

void OverlayBar::mousePressEvent(QMouseEvent* event)
{
    hideWordCardNow();
    cancelSnack();
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragMoved = false;
        m_pressGlobal = event->globalPosition().toPoint();
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_autoHideTimer->stop();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void OverlayBar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint global = event->globalPosition().toPoint();
        // A plain click must not count as "the user placed the bar here",
        // otherwise the bar would stop centring itself for the whole session.
        if ((global - m_pressGlobal).manhattanLength() > 2)
            m_dragMoved = true;
        move(global - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void OverlayBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        if (m_dragMoved) {
            m_userMoved = true;
            // A drag can move the bar to another monitor: re-derive its fixed
            // width from that screen.
            applyBarWidth();
            updateWidth();
            clampToScreen();
            emit barMoved(pos());
        }
        m_dragMoved = false;
        startAutoHideTimer();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void OverlayBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    placeAtSavedPosition();
    scheduleSnack();
}

void OverlayBar::hideEvent(QHideEvent* event)
{
    hideWordCardNow();
    cancelSnack();
    QWidget::hideEvent(event);
}

void OverlayBar::enterEvent(QEnterEvent* event)
{
    m_autoHideTimer->stop();
    // The user is here: give the words back and stop nibbling.
    cancelSnack();
    QWidget::enterEvent(event);
}

void OverlayBar::leaveEvent(QEvent* event)
{
    startAutoHideTimer();
    // The layout was frozen while the pointer was here; catch up now.
    refreshLayout();
    scheduleSnack();
    QWidget::leaveEvent(event);
}

void OverlayBar::startAutoHideTimer()
{
    if (m_card && m_card->isVisible())
        return;
    if (m_cfg.autoHide && isVisible())
        m_autoHideTimer->start(qMax(3, m_cfg.autoHideSeconds) * 1000);
}
