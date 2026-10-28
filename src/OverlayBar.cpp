#include "OverlayBar.h"

#include "DefinitionCard.h"
#include "Dictionary.h"
#include "PetWidget.h"
#include "WordChip.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QLinearGradient>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

const QColor kIdleColor(74, 222, 128);    // green
const QColor kScanColor(122, 162, 247);   // blue
const QColor kWarnColor(251, 191, 36);    // amber
const QColor kErrorColor(248, 113, 113);  // red

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
    m_chipHost->hide();
    m_row->addWidget(m_chipHost, 0, Qt::AlignVCenter);

    m_moreBtn = new QToolButton(this);
    m_moreBtn->setObjectName(QStringLiteral("barMore"));
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setAutoRaise(true);
    m_moreBtn->setFixedHeight(24);
    QFont moreFont(QStringLiteral("Microsoft YaHei UI"));
    moreFont.setPixelSize(11);
    m_moreBtn->setFont(moreFont);
    m_moreBtn->setToolTip(QStringLiteral("还有更多待处理生词，点击打开列表"));
    m_moreBtn->hide();
    m_row->addWidget(m_moreBtn, 0, Qt::AlignVCenter);

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
        "QToolButton#barMore{color:#9CC1FF;border:1px solid rgba(122,162,247,90);"
        "border-radius:11px;padding:0 10px;}"
        "QToolButton#barMore:hover{background:rgba(122,162,247,50);color:#EAF1FF;}"));

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
    connect(m_moreBtn, &QToolButton::clicked, this, &OverlayBar::detailsRequested);

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
    // the custom-region "adjust" button is included in the width.
    setCaptureMode(m_cfg.captureMode);
    updateWidth();
    update();

    if (!m_cfg.barPosSet)
        centerOnScreen();
    else
        placeAtSavedPosition();
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

void OverlayBar::setWords(const QVector<WordEntry>& words)
{
    m_lastWords = words;
    m_count->setText(QStringLiteral("%1 生词").arg(words.size()));

    // While the pointer is on (or near) the bar, or a card is open, freeze the
    // whole layout: resizing or re-ordering would slide the buttons and chips
    // out from under the cursor. The pending state is applied as soon as the
    // pointer leaves (see refreshLayout()).
    //
    // An empty word list is the exception: there is no chip to click any more,
    // and freezing would leave stale words on screen indefinitely.
    if (!m_chips.isEmpty() && !words.isEmpty()
        && (pointerIsNearBar() || (m_card && m_card->isVisible())))
        return;

    syncChips(words);
    updateWidth();
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

            m_chipLayout->addWidget(chip, 0, Qt::AlignVCenter);
        }

        chip->setCount(entry.count);
        chip->setFresh(now - entry.firstSeen < 4000);

        chip->show();
        m_chips.append(chip);
    }
}

void OverlayBar::updateWidth()
{
    const bool hasChips = !m_chips.isEmpty();
    const bool cardOpen = m_card && m_card->isVisible();

    // Never resize the bar while the pointer is on or near it (or while a card
    // is open): the right-hand buttons would slide out from under the cursor
    // and the user would click the wrong one. The pending layout is applied by
    // the first update after the pointer moves away.
    if (hasChips && (pointerIsNearBar() || cardOpen)) {
        clampToScreen();
        return;
    }

    if (!hasChips)
        hideWordCardNow();
    m_hint->setVisible(!hasChips);
    // With no words at all the hint already says everything; a lone "0 生词"
    // next to it is just noise.
    m_count->setVisible(hasChips);

    QScreen* target = screen() ? screen() : QGuiApplication::primaryScreen();
    const int screenWidth = target ? target->availableGeometry().width() : 1920;
    const int maxBarWidth = qBound(420, qMin(1240, screenWidth - 200), 1400);

    // Measure everything that is not a word pill. Hidden widgets are ignored by
    // the layout, so take the strip and the "+N" pill out of the equation first;
    // their space is budgeted separately. The widths are summed by hand instead
    // of asking the row layout, whose cached sizeHint can still contain the
    // hidden chip strip and would collapse the strip to a single pill.
    m_chipHost->hide();
    m_moreBtn->hide();
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
    // Gaps: between every fixed widget, plus one slot for the strip and one for
    // the "+N" pill (the right-side stretch adds one more).
    fixedWidth += (fixedWidgets + 3) * m_row->spacing();

    const QFontMetrics moreMetrics(m_moreBtn->font());
    const int moreWidth = moreMetrics.horizontalAdvance(QStringLiteral("+888")) + 26;
    const int baseBudget = qMax(120, maxBarWidth - fixedWidth);

    // Show as many pills as fit, whole. Never compress a pill: its text would
    // be clipped (the old scroll-area layout did exactly that). Only reserve
    // room for the "+N" pill when something actually has to be hidden, so a bar
    // that fits everything does not waste a slot.
    const int cap = qBound(1, m_cfg.maxChips, 40);
    const int spacing = m_chipLayout->spacing();
    const auto countFitting = [&](int budget) {
        int used = 0;
        int count = 0;
        for (WordChip* chip : m_chips) {
            if (count >= cap)
                break;
            const int extra = chip->sizeHint().width() + (count > 0 ? spacing : 0);
            if (count > 0 && used + extra > budget)
                break;
            used += extra;
            ++count;
        }
        return count;
    };

    const int total = qMax(m_lastWords.size(), m_chips.size());
    int visible = countFitting(baseBudget - moreWidth);
    if (visible >= total)
        visible = countFitting(baseBudget);

    for (int i = 0; i < m_chips.size(); ++i)
        m_chips.at(i)->setVisible(i < visible);

    if (hasChips && visible < total) {
        const int hidden = total - visible;
        m_moreBtn->setText(QStringLiteral("+%1").arg(hidden));
        m_moreBtn->setToolTip(
            QStringLiteral("还有 %1 个生词未显示，点击打开列表").arg(hidden));
        m_moreBtn->show();
    }

    m_chipHost->setVisible(hasChips);
    adjustSize();

    // Keep the bar centred while its width changes; it is only resized when the
    // pointer is away, so this cannot yank the bar from under the cursor.
    if (!m_userMoved)
        centerOnScreen();
    else
        clampToScreen();
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
    QScreen* target = QGuiApplication::screenAt(frameGeometry().center());
    if (!target)
        target = QGuiApplication::primaryScreen();
    if (!target)
        return;
    const QRect available = target->availableGeometry();
    const int nx = qBound(available.left(), x(), available.right() - width());
    const int ny = qBound(available.top(), y(), available.bottom() - height());
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
}

void OverlayBar::hideEvent(QHideEvent* event)
{
    hideWordCardNow();
    QWidget::hideEvent(event);
}

void OverlayBar::enterEvent(QEnterEvent* event)
{
    m_autoHideTimer->stop();
    QWidget::enterEvent(event);
}

void OverlayBar::leaveEvent(QEvent* event)
{
    startAutoHideTimer();
    // The layout was frozen while the pointer was here; catch up now.
    refreshLayout();
    QWidget::leaveEvent(event);
}

void OverlayBar::startAutoHideTimer()
{
    if (m_card && m_card->isVisible())
        return;
    if (m_cfg.autoHide && isVisible())
        m_autoHideTimer->start(qMax(3, m_cfg.autoHideSeconds) * 1000);
}
