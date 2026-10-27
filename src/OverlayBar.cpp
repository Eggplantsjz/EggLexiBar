#include "OverlayBar.h"

#include "DefinitionCard.h"
#include "Dictionary.h"
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
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

class StatusDot : public QWidget
{
public:
    explicit StatusDot(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(12, 12);
        m_timer.setInterval(420);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] {
            m_phase = !m_phase;
            update();
        });
    }

    void setColor(const QColor& color)
    {
        if (m_color == color)
            return;
        m_color = color;
        update();
    }

    void setAnimated(bool animated)
    {
        if (animated == m_timer.isActive())
            return;
        if (animated)
            m_timer.start();
        else
            m_timer.stop();
        m_phase = true;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);

        QColor glow = m_color;
        glow.setAlpha(m_timer.isActive() && !m_phase ? 30 : 70);
        p.setBrush(glow);
        p.drawEllipse(QRectF(0, 0, width(), height()));

        QColor core = m_color;
        if (m_timer.isActive() && !m_phase)
            core.setAlpha(140);
        p.setBrush(core);
        p.drawEllipse(QPointF(width() / 2.0, height() / 2.0), 3.4, 3.4);
    }

private:
    QTimer m_timer;
    QColor m_color{0x4A, 0xDE, 0x80};
    bool m_phase = true;
};

QColor colorForKind(const QString& kind)
{
    if (kind == QLatin1String("ok"))
        return QColor(74, 222, 128);
    if (kind == QLatin1String("warn"))
        return QColor(251, 191, 36);
    if (kind == QLatin1String("error"))
        return QColor(248, 113, 113);
    if (kind == QLatin1String("scan"))
        return QColor(122, 162, 247);
    return QColor(138, 147, 166);
}

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

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 6, 10, 6);
    root->setSpacing(8);

    m_dot = new StatusDot(this);
    root->addWidget(m_dot, 0, Qt::AlignVCenter);

    m_status = new QLabel(QStringLiteral("准备中…"), this);
    m_status->setObjectName(QStringLiteral("barStatus"));
    m_status->setMinimumWidth(72);
    root->addWidget(m_status, 0, Qt::AlignVCenter);

    m_hint = new QLabel(QStringLiteral("把英文显示在屏幕上，生词会自动出现在这里"), this);
    m_hint->setObjectName(QStringLiteral("barHint"));
    root->addWidget(m_hint, 0, Qt::AlignVCenter);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("barScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea{background:transparent;border:none;}"
        "QScrollArea > QWidget > QWidget{background:transparent;}"));
    m_scroll->hide();

    m_chipHost = new QWidget;
    m_chipHost->setAutoFillBackground(false);
    m_chipLayout = new QHBoxLayout(m_chipHost);
    m_chipLayout->setContentsMargins(0, 0, 0, 0);
    m_chipLayout->setSpacing(6);
    m_chipLayout->addStretch(1);
    m_scroll->setWidget(m_chipHost);
    root->addWidget(m_scroll, 1, Qt::AlignVCenter);

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
    root->addWidget(m_moreBtn, 0, Qt::AlignVCenter);

    m_count = new QLabel(QStringLiteral("0 生词"), this);
    m_count->setObjectName(QStringLiteral("barCount"));
    m_count->setToolTip(QStringLiteral("本次识别到的生词数量"));
    root->addWidget(m_count, 0, Qt::AlignVCenter);

    m_pauseBtn = makeToolButton(this, QStringLiteral("❚❚"), QStringLiteral("暂停 / 继续识别"));
    m_listBtn = makeToolButton(this, QStringLiteral("☰"), QStringLiteral("生词列表"));
    m_settingsBtn = makeToolButton(this, QStringLiteral("⚙"), QStringLiteral("设置"));
    m_hideBtn = makeToolButton(this, QStringLiteral("✕"), QStringLiteral("隐藏状态栏（Ctrl+Alt+W 唤回）"));
    root->addWidget(m_pauseBtn);
    root->addWidget(m_listBtn);
    root->addWidget(m_settingsBtn);
    root->addWidget(m_hideBtn);

    setStyleSheet(QStringLiteral(
        "QLabel#barStatus{color:#B9C0D0;}"
        "QLabel#barCount{color:#98A1B4;}"
        "QLabel#barHint{color:#7F889C;}"
        "QToolButton{border:none;border-radius:6px;color:#C7CEDC;}"
        "QToolButton:hover{background:rgba(255,255,255,28);}"
        "QToolButton:pressed{background:rgba(255,255,255,52);}"
        "QToolButton#barMore{color:#9CC1FF;border:1px solid rgba(122,162,247,90);"
        "border-radius:11px;padding:0 8px;}"
        "QToolButton#barMore:hover{background:rgba(122,162,247,50);color:#EAF1FF;}"));

    connect(m_pauseBtn, &QToolButton::clicked, this, &OverlayBar::pauseToggled);
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

    m_scroll->viewport()->installEventFilter(this);
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

    // Fixed labels: the bar must not twitch (and drag its buttons sideways)
    // every time the status text or the counter changes.
    const QFontMetrics smallMetrics(smallFont);
    m_statusWidth = qMax(170, smallMetrics.horizontalAdvance(
                                  QStringLiteral("识别中… 99.9 s")) + 20);
    m_status->setFixedWidth(m_statusWidth);
    m_count->setFixedWidth(smallMetrics.horizontalAdvance(
                               QStringLiteral("888 生词")) + 6);

    for (WordChip* chip : m_chips)
        chip->setChipFont(wordFont);

    const int chipHeight = WordChip::chipHeightFor(wordFont);
    setFixedHeight(qMax(40, chipHeight + 14));
    m_scroll->setFixedHeight(chipHeight + 2);

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

    const QString shown = m_flashText.isEmpty() ? text : m_flashText;
    const QFontMetrics fm(m_status->font());
    const int width = m_statusWidth > 0 ? m_statusWidth : 262;
    m_status->setText(fm.elidedText(shown, Qt::ElideRight, width));
    m_status->setToolTip(shown);
    m_status->setStyleSheet(m_flashText.isEmpty()
                                ? QString()
                                : QStringLiteral("color:#9CC1FF;"));
    updateDot();
}

void OverlayBar::updateDot()
{
    if (!m_engineReady) {
        m_dot->setColor(colorForKind(QStringLiteral("error")));
        m_dot->setAnimated(false);
        return;
    }
    if (m_paused) {
        m_dot->setColor(colorForKind(QStringLiteral("warn")));
        m_dot->setAnimated(false);
        return;
    }
    m_dot->setColor(colorForKind(m_scanning ? QStringLiteral("scan") : m_statusKind));
    m_dot->setAnimated(m_scanning);
}

void OverlayBar::setPaused(bool paused)
{
    m_paused = paused;
    m_pauseBtn->setText(paused ? QStringLiteral("▶") : QStringLiteral("❚❚"));
    updateDot();
}

void OverlayBar::setScanning(bool scanning)
{
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    updateDot();
}

void OverlayBar::setEngineReady(bool ready)
{
    m_engineReady = ready;
    updateDot();
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
    // An empty bar is the exception: with no chips there is nothing to keep
    // steady, and freezing here would leave the bar blank ("N 生词", no words)
    // until the pointer happens to move away.
    if (!m_chips.isEmpty()
        && (pointerIsNearBar() || (m_card && m_card->isVisible())))
        return;

    const int limit = qBound(1, m_cfg.maxChips, 40);
    const int overflow = qMax(0, words.size() - limit);
    m_moreBtn->setText(QStringLiteral("+%1").arg(overflow));
    m_moreBtn->setVisible(overflow > 0);
    if (overflow > 0) {
        m_moreBtn->setToolTip(QStringLiteral("还有 %1 个待处理生词，点击打开列表").arg(overflow));
    }

    syncChips(words);
    updateWidth();
}

void OverlayBar::refreshLayout()
{
    QTimer::singleShot(0, this, [this] {
        if (!m_lastWords.isEmpty())
            setWords(m_lastWords);
    });
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

    const int limit = qBound(1, m_cfg.maxChips, 40);
    QVector<WordEntry> wanted = words.mid(0, limit);

    // Drop chips that are no longer shown.
    for (int i = m_chips.size() - 1; i >= 0; --i) {
        WordChip* chip = m_chips.at(i);
        bool keep = false;
        for (const WordEntry& entry : wanted) {
            if (entry.word == chip->word()) {
                keep = true;
                break;
            }
        }
        if (!keep) {
            m_chips.removeAt(i);
            m_chipLayout->removeWidget(chip);
            if (m_card && m_card->isVisible() && m_cardWord == chip->word())
                hideWordCardNow();
            chip->hide();
            chip->deleteLater();
        }
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int index = 0; index < wanted.size(); ++index) {
        const WordEntry& entry = wanted.at(index);
        WordChip* chip = findChip(entry.word);
        if (!chip) {
            chip = new WordChip(entry.word, m_chipHost);
            QFont wordFont(QStringLiteral("Segoe UI"));
            wordFont.setPixelSize(qBound(10, m_cfg.fontSize, 26));
            chip->setChipFont(wordFont);
            connect(chip, &WordChip::activated, this, [this](const QString& word) {
                emit copyRequested(word);
            });
            connect(chip, &WordChip::knownBySwipe, this, [this, chip](const QString& word) {
                // Hide immediately, before the session refresh catches up. This also
                // prevents the definition card from lingering after the gesture.
                hideWordCardNow();
                chip->hide();
                emit markKnownRequested(word);
            });
            connect(chip, &WordChip::menuRequested, this, &OverlayBar::showWordMenu);
            connect(chip, &WordChip::hoverStarted, this, &OverlayBar::showWordCard);
            connect(chip, &WordChip::hoverEnded, this, &OverlayBar::hideWordCard);
            m_chips.append(chip);
            m_chipLayout->insertWidget(index, chip);
        }
        chip->setCount(entry.count);
        chip->setFresh(now - entry.firstSeen < 4000);

        const int current = m_chips.indexOf(chip);
        if (current != index) {
            m_chips.removeAt(current);
            m_chips.insert(index, chip);
            m_chipLayout->insertWidget(index, chip);
        }
        chip->show();
    }
}

void OverlayBar::updateWidth()
{
    // Never resize the bar while the pointer is on or near it (or while a card
    // is open): the right-hand buttons would slide out from under the cursor
    // and the user would click the wrong one. The pending layout is applied by
    // the first update after the pointer moves away.
    if (m_scroll->isVisible()
        && (pointerIsNearBar() || (m_card && m_card->isVisible()))) {
        clampToScreen();
        return;
    }

    int chipsWidth = 0;
    for (WordChip* chip : m_chips)
        chipsWidth += chip->sizeHint().width() + 6;
    if (chipsWidth > 0)
        chipsWidth -= 6;

    if (m_chips.isEmpty()) {
        hideWordCardNow();
        m_scroll->hide();
        m_hint->show();
    } else {
        m_hint->hide();
        m_scroll->show();
        const int screenWidth = screen() ? screen()->availableGeometry().width() : 1920;
        const int maxWidth = qBound(300, qMin(1180, screenWidth - 360), 1400);
        m_scroll->setFixedWidth(qBound(90, chipsWidth, maxWidth));
    }

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
    if (m_cardHideTimer)
        m_cardHideTimer->stop();

    if (!chip || !isVisible() || !m_dictionary || !m_dictionary->isLoaded())
        return;

    const WordDefinition definition = m_dictionary->lookup(chip->word());
    if (!definition.isValid()) {
        hideWordCardNow();
        return;
    }

    if (!m_card) {
        m_card = new DefinitionCard;
        connect(m_card, &DefinitionCard::markKnownRequested, this, [this](const QString& word) {
            hideWordCardNow();
            emit markKnownRequested(word);
        });
        connect(m_card, &DefinitionCard::blockRequested, this, [this](const QString& word) {
            hideWordCardNow();
            emit blockRequested(word);
        });
        connect(m_card, &DefinitionCard::copyRequested, this, [this](const QString& word) {
            hideWordCardNow();
            emit copyRequested(word);
        });
        connect(m_card, &DefinitionCard::hoverChanged, this, [this](bool inside) {
            if (inside) {
                if (m_cardHideTimer)
                    m_cardHideTimer->stop();
            } else {
                hideWordCard();
            }
        });
    }

    m_cardWord = chip->word();
    emit wordInspected(m_cardWord);

    const QRect anchor(chip->mapToGlobal(QPoint(0, 0)), chip->size());
    m_card->showFor(definition, anchor);
}

void OverlayBar::hideWordCard(WordChip* chip)
{
    Q_UNUSED(chip);
    if (!m_card || !m_card->isVisible())
        return;
    // A short grace period lets the pointer travel from the chip to the card
    // (and onto one of the buttons) without the card disappearing.
    if (m_cardHideTimer)
        m_cardHideTimer->start();
}

void OverlayBar::hideWordCardNow()
{
    if (m_cardHideTimer)
        m_cardHideTimer->stop();
    const bool wasOpen = m_card && m_card->isVisible();
    if (wasOpen)
        m_card->hide();
    m_cardWord.clear();
    if (wasOpen)
        refreshLayout();
}

void OverlayBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rectF = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QColor background(20, 22, 28);
    background.setAlpha(qBound(120, int(255 * m_cfg.opacityPercent / 100.0), 255));

    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.setBrush(background);
    p.drawRoundedRect(rectF, 14, 14);
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

bool OverlayBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_scroll->viewport() && event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        QScrollBar* bar = m_scroll->horizontalScrollBar();
        const int delta = wheel->angleDelta().y() != 0 ? wheel->angleDelta().y()
                                                       : wheel->angleDelta().x();
        bar->setValue(bar->value() - delta / 2);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
