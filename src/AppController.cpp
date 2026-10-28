#include "AppController.h"

#include "DetailsPanel.h"
#include "HotkeyFilter.h"
#include "OverlayBar.h"
#include "RegionSelector.h"
#include "SettingsDialog.h"
#include "WindowUtils.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QIcon>
#include <QLinearGradient>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QtMath>

namespace {

QIcon makeAppIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient gradient(0, 0, 64, 64);
    gradient.setColorAt(0.0, QColor(74, 110, 245));
    gradient.setColorAt(1.0, QColor(122, 162, 247));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(2, 2, 60, 60), 16, 16);

    painter.setPen(QColor(255, 255, 255));
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(36);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRectF(0, -2, 64, 64), Qt::AlignCenter, QStringLiteral("W"));
    painter.end();
    return QIcon(pixmap);
}

bool isExcluded(const QRect& box, const QVector<QRect>& excludes)
{
    const QPoint center = box.center();
    for (const QRect& rect : excludes) {
        if (rect.contains(center))
            return true;
    }
    return false;
}

// Persisted filter state uses "word|filterCount|hiddenUntilMs" entries so it
// survives a restart and the memory curve keeps advancing across sessions.
WordSession::PersistFilter parseFilterState(const QStringList& entries)
{
    WordSession::PersistFilter map;
    for (const QString& entry : entries) {
        const QStringList parts = entry.split(QLatin1Char('|'));
        if (parts.size() != 3)
            continue;
        const QString word = parts.at(0);
        if (word.isEmpty())
            continue;
        bool okCount = false;
        bool okUntil = false;
        const int count = parts.at(1).toInt(&okCount);
        const qint64 until = parts.at(2).toLongLong(&okUntil);
        if (!okCount || !okUntil)
            continue;
        map.insert(word, qMakePair(count, until));
    }
    return map;
}

QStringList serializeFilterState(const WordSession::PersistFilter& map)
{
    QStringList entries;
    entries.reserve(map.size());
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        entries.append(QStringLiteral("%1|%2|%3")
                           .arg(it.key())
                           .arg(it.value().first)
                           .arg(it.value().second));
    }
    return entries;
}

} // namespace

// ---------------------------------------------------------------------------
// OcrWorker
// ---------------------------------------------------------------------------

OcrWorker::OcrWorker(QObject* parent)
    : QObject(parent)
    , m_ocr(QString())
{
}

void OcrWorker::configure(const AppConfig& cfg)
{
    m_cfg = cfg;
    m_ocr.setConfiguredPath(cfg.tesseractPath);
}

void OcrWorker::process(const QImage& image, qint64 stamp)
{
    if (!m_ocr.available())
        m_ocr.setConfiguredPath(m_cfg.tesseractPath);
    const OcrResult result = m_ocr.recognize(image, m_cfg);
    emit finished(result, stamp);
}

// ---------------------------------------------------------------------------
// AppController
// ---------------------------------------------------------------------------

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    m_cfg = loadConfig();
    m_filter.setConfig(m_cfg);
    m_session.setPersistFilter(parseFilterState(m_cfg.autoFilterState));
}

AppController::~AppController()
{
    if (m_hotkey) {
        m_hotkey->unregister();
        qApp->removeNativeEventFilter(m_hotkey);
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
    }
    delete m_hotkey;
    m_hotkey = nullptr;
}

void AppController::start()
{
    createUi();
    createTray();
    createWorker();

    m_hotkey = new HotkeyFilter;
    m_hotkey->onToggle = [this] { toggleBar(); };
    qApp->installNativeEventFilter(m_hotkey);
    if (!m_hotkey->registerToggleHotkey()) {
        m_hotkeyOk = false;
        if (m_tray) {
            m_tray->setToolTip(QStringLiteral("WordLens · 屏幕生词栏\n"
                                              "Ctrl+Alt+W 已被其他程序占用，请用托盘菜单显示/隐藏"));
        }
    }

    loadDictionaries();
    refreshEngineState();

    m_bar->applyConfig(m_cfg);
    m_bar->show();

    if (!m_hotkeyOk) {
        m_bar->flashMessage(QStringLiteral("快捷键 Ctrl+Alt+W 被占用"));
    }

    m_timer = new QTimer(this);
    m_timer->setInterval(qMax(300, m_cfg.intervalMs));
    connect(m_timer, &QTimer::timeout, this, &AppController::tick);
    m_timer->start();

    QTimer::singleShot(600, this, &AppController::tick);
}

void AppController::createUi()
{
    m_bar = new OverlayBar;

    connect(m_bar, &OverlayBar::pauseToggled, this, [this] { setPaused(!m_paused); });
    connect(m_bar, &OverlayBar::captureModeToggled, this, [this] {
        m_cfg.captureMode = (m_cfg.captureMode + 1) % 4;
        // If we cycle TO custom region without one yet, auto-open the selector.
        if (m_cfg.captureMode == 3 && m_cfg.customRegion.isEmpty()) {
            saveConfig(m_cfg);
            m_bar->applyConfig(m_cfg);
            m_bar->flashMessage(QStringLiteral("请拖动鼠标框选识别区域"));
            // Defer the overlay so the hide animation doesn't get covered.
            QTimer::singleShot(180, this, &AppController::openRegionSelector);
            return;
        }
        saveConfig(m_cfg);
        m_bar->flashMessage(QStringLiteral("识别范围已切换"));
        m_timer->start(qMax(300, m_cfg.intervalMs));
        QTimer::singleShot(200, this, &AppController::tick);
    });
    connect(m_bar, &OverlayBar::regionSelectRequested, this, &AppController::openRegionSelector);
    connect(m_bar, &OverlayBar::detailsRequested, this, &AppController::openDetails);
    connect(m_bar, &OverlayBar::settingsRequested, this, &AppController::openSettings);
    connect(m_bar, &OverlayBar::copyRequested, this, &AppController::copyWord);
    connect(m_bar, &OverlayBar::markKnownRequested, this,
            [this](const QString& word) { markKnown({word}); });
    connect(m_bar, &OverlayBar::blockRequested, this,
            [this](const QString& word) { blockWords({word}); });
    connect(m_bar, &OverlayBar::wordInspected, this,
            [this](const QString& word) { m_session.touchInteraction(word); });
    connect(m_bar, &OverlayBar::barMoved, this, [this](const QPoint& pos) {
        m_cfg.barPos = pos;
        m_cfg.barPosSet = true;
        saveConfig(m_cfg);
    });
    connect(m_bar, &OverlayBar::hideRequested, this, [this] {
        m_bar->hide();
        if (m_tray && !m_trayHintShown) {
            m_tray->showMessage(QStringLiteral("WordLens"),
                                QStringLiteral("状态栏已隐藏，Ctrl+Alt+W 或托盘图标可重新显示"),
                                QSystemTrayIcon::Information, 3000);
            m_trayHintShown = true;
        }
    });

    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(95000);
    connect(m_watchdog, &QTimer::timeout, this, [this] {
        if (!m_busy)
            return;
        m_busy = false;
        m_bar->setScanning(false);
        m_bar->setStatus(QStringLiteral("识别超时"), QStringLiteral("error"));
    });

    m_scanTicker = new QTimer(this);
    m_scanTicker->setInterval(500);
    connect(m_scanTicker, &QTimer::timeout, this, [this] {
        if (!m_busy)
            return;
        m_bar->setStatus(QStringLiteral("识别中… %1s")
                             .arg(m_scanClock.elapsed() / 1000.0, 0, 'f', 1),
                         QStringLiteral("scan"));
    });

    m_detailsRefresh = new QTimer(this);
    m_detailsRefresh->setSingleShot(true);
    m_detailsRefresh->setInterval(600);
    connect(m_detailsRefresh, &QTimer::timeout, this, [this] {
        if (m_details && m_details->isVisible())
            m_details->setEntries(m_session.all());
    });
}

void AppController::createTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_tray = new QSystemTrayIcon(makeAppIcon(), this);
    m_tray->setToolTip(QStringLiteral("WordLens · 屏幕生词栏"));

    m_trayMenu = new QMenu(m_bar);
    m_toggleAction = m_trayMenu->addAction(QStringLiteral("隐藏状态栏"));
    connect(m_toggleAction, &QAction::triggered, this, &AppController::toggleBar);

    m_pauseAction = m_trayMenu->addAction(QStringLiteral("暂停识别"));
    m_pauseAction->setCheckable(true);
    connect(m_pauseAction, &QAction::triggered, this,
            [this](bool checked) { setPaused(checked); });

    m_trayMenu->addSeparator();
    m_trayMenu->addAction(QStringLiteral("生词列表…"), this, &AppController::openDetails);
    m_trayMenu->addAction(QStringLiteral("设置…"), this, &AppController::openSettings);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(QStringLiteral("退出"), qApp, &QApplication::quit);

    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    toggleBar();
            });
    m_tray->show();
}

void AppController::createWorker()
{
    qRegisterMetaType<OcrResult>("OcrResult");

    m_thread = new QThread(this);
    m_worker = new OcrWorker;
    m_worker->moveToThread(m_thread);

    connect(this, &AppController::configureRequested, m_worker, &OcrWorker::configure);
    connect(this, &AppController::ocrRequested, m_worker, &OcrWorker::process);
    connect(m_worker, &OcrWorker::finished, this, &AppController::handleOcr);

    m_thread->start();
    emit configureRequested(m_cfg);
}

void AppController::loadDictionaries()
{
    m_bar->setStatus(QStringLiteral("加载词典…"), QStringLiteral("warn"));

    QString error;
    QElapsedTimer timer;
    timer.start();
    const bool ok = m_filter.load(appDataDir(), &error);
    m_filter.setConfig(m_cfg);

    const QString dictPath = QDir(appDataDir()).filePath(QStringLiteral("dict.bin"));
    QString dictError;
    const bool dictOk = m_dictionary.load(dictPath, &dictError);
    m_bar->setDictionary(dictOk ? &m_dictionary : nullptr);

    if (ok && dictOk) {
        m_bar->setStatus(QStringLiteral("就绪 · 释义 %1 条")
                             .arg(QLocale().toString(m_dictionary.size())),
                         QStringLiteral("ok"));
    } else if (ok) {
        m_bar->setStatus(QStringLiteral("就绪 · 无释义数据（悬停不显示中文）"),
                         QStringLiteral("warn"));
    } else {
        m_bar->setStatus(error.isEmpty() ? QStringLiteral("词典加载失败") : error,
                         QStringLiteral("error"));
    }
    Q_UNUSED(timer);
}

void AppController::refreshEngineState()
{
    TesseractOcr probe(m_cfg.tesseractPath);
    m_engineReady = probe.available();
    m_bar->setEngineReady(m_engineReady);
    if (m_settings)
        m_settings->setEngineInfo(probe.executablePath(), probe.tessdataPath());

    if (!m_engineReady) {
        m_bar->setStatus(QStringLiteral("未找到 Tesseract，请在设置中指定"), QStringLiteral("error"));
    } else if (m_lastElapsedMs == 0 && !m_paused) {
        m_bar->setStatus(QStringLiteral("就绪"), QStringLiteral("ok"));
    }
}

void AppController::tick()
{
    if (m_paused || m_busy)
        return;

    if (!m_engineReady) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastEngineCheck > 8000) {
            m_lastEngineCheck = now;
            refreshEngineState();
        }
        if (!m_engineReady)
            return;
    }

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    const QRect screenGeometry = screen->geometry();
    const QPixmap shot = screen->grabWindow(0);
    if (shot.isNull()) {
        m_bar->setStatus(QStringLiteral("屏幕捕获失败"), QStringLiteral("error"));
        return;
    }

    const QImage fullImage = shot.toImage();
    if (fullImage.isNull())
        return;

    // Crop to the configured region of interest.
    const double scale = double(fullImage.width()) / qMax(1, screenGeometry.width());
    const QRect region = interestRect(screenGeometry);
    const QRect fullRect(QPoint(0, 0), fullImage.size());
    QRect crop(qRound((region.x() - screenGeometry.x()) * scale),
               qRound((region.y() - screenGeometry.y()) * scale),
               qRound(region.width() * scale),
               qRound(region.height() * scale));
    crop = crop.intersected(fullRect);
    if (crop.width() < 16 || crop.height() < 16)
        crop = fullRect;

    const QImage frame = (crop == fullRect) ? fullImage : fullImage.copy(crop);
    m_lastCropOffset = crop.topLeft();

    // Skip the expensive OCR pass when the picture did not change. Long scans
    // on a 4K screen would otherwise burn the CPU non stop while reading.
    const QImage thumb = frame
                             .scaled(80, 50, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                             .convertToFormat(QImage::Format_Grayscale8);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_lastThumb.isNull() && m_lastThumb.size() == thumb.size()) {
        qint64 sum = 0;
        int count = 0;
        for (int y = 0; y < thumb.height(); ++y) {
            const uchar* a = thumb.constScanLine(y);
            const uchar* b = m_lastThumb.constScanLine(y);
            for (int x = 0; x < thumb.width(); ++x) {
                sum += qAbs(int(a[x]) - int(b[x]));
                ++count;
            }
        }
        const double difference = count > 0 ? double(sum) / count : 0.0;
        if (difference < 1.0 && now - m_lastOcrAt < 120000) {
            m_session.touchRecent(now);
            const QVector<WordEntry> active = m_session.active(now, activeTtlMs());
            m_bar->setWords(active);
            m_lastActiveWords = active.size();
            m_bar->setStatus(QStringLiteral("无变化"), QStringLiteral("idle"));
            return;
        }
    }
    m_lastThumb = thumb;

    m_busy = true;
    m_scanClock.start();
    m_scanTicker->start();
    m_bar->setStatus(QStringLiteral("识别中…"), QStringLiteral("scan"));
    m_bar->setScanning(true);
    m_watchdog->start();
    emit ocrRequested(frame, now);
}

QRect AppController::interestRect(const QRect& screenGeometry) const
{
    if (m_cfg.captureMode == 1) {
        QRect rect = activeWindowRect();
        if (rect.isValid()) {
            rect = rect.intersected(screenGeometry);
            if (rect.width() > 160 && rect.height() > 100)
                return rect;
        }
    } else if (m_cfg.captureMode == 2) {
        QRect rect = cursorRegionRect(QSize(900, 560)).intersected(screenGeometry);
        if (rect.isValid())
            return rect;
    } else if (m_cfg.captureMode == 3 && m_cfg.customRegion.isValid()
               && !m_cfg.customRegion.isEmpty()) {
        QRect rect = m_cfg.customRegion.intersected(screenGeometry);
        if (rect.width() > 60 && rect.height() > 40)
            return rect;
    }
    return screenGeometry;
}

void AppController::handleOcr(const OcrResult& result, qint64 stamp)
{
    Q_UNUSED(stamp);
    m_busy = false;
    m_watchdog->stop();
    m_scanTicker->stop();
    m_bar->setScanning(false);
    m_lastOcrAt = QDateTime::currentMSecsSinceEpoch();

    if (!result.ok) {
        m_bar->setStatus(result.error.isEmpty() ? QStringLiteral("识别失败") : result.error,
                         QStringLiteral("error"));
        return;
    }

    const QVector<QRect> excludes = exclusionRects(result.imageSize, m_lastCropOffset);
    int englishCount = 0;
    QVector<WordHit> kept;
    kept.reserve(result.words.size() / 4);

    for (const WordHit& hit : result.words) {
        if (hit.confidence < m_cfg.minConfidence)
            continue;
        if (isExcluded(hit.box, excludes))
            continue;

        // OCR is noisy: run through a repair pass that tries common
        // character confusables (0↔o, 1↔l, ...) before we decide whether
        // this word is worth showing. The repaired spelling is what gets
        // stored in the session — so "analys1" becomes "analysl" which
        // normalizes to the real dictionary entry.
        QString repaired;
        const WordDefinition def = m_dictionary.lookupRepaired(hit.text, &repaired);
        if (!def.isValid())
            continue;   // no dictionary match even after repair → pure OCR noise

        const QString word = m_filter.normalize(repaired.isEmpty() ? hit.text : repaired);
        if (word.isEmpty())
            continue;
        ++englishCount;
        if (!m_filter.isInteresting(word))
            continue;

        WordHit filtered;
        filtered.text = word;
        filtered.box = hit.box;
        filtered.confidence = hit.confidence;
        kept.append(filtered);
    }

    // --- Proximity boost: words closer to the cursor get merged first so they
    //     stay at the front of the session (top of the bar) instead of being
    //     pushed down by every newer scan.
    if (!kept.isEmpty()) {
        const QPoint cursor = QCursor::pos();
        // WordHit.box is in image-local coords; m_lastCropOffset translates back
        // to absolute screen coords so distance comparisons are consistent.
        const QPoint cursorInImage = cursor - m_lastCropOffset;
        std::stable_sort(kept.begin(), kept.end(),
                         [&](const WordHit& a, const WordHit& b) {
                             const QPoint ca = a.box.center();
                             const QPoint cb = b.box.center();
                             const int da = (ca - cursorInImage).manhattanLength();
                             const int db = (cb - cursorInImage).manhattanLength();
                             return da < db;
                         });
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_session.ingest(kept, now);

    // Reactivate words whose memory-curve interval has elapsed, then hide words
    // that keep appearing without any user interaction. Auto-filtering never
    // touches knownWords — only an explicit swipe/menu/list action marks known.
    m_session.resurface(now);
    int retired = 0;
    if (m_cfg.autoFilter) {
        retired = m_session.autoFilter(now, m_cfg.autoFilterScans,
                                       qMax(0, m_cfg.autoFilterSeconds) * 1000);
    }

    // Persist filter state so the curve keeps advancing across restarts.
    const QStringList serialized = serializeFilterState(m_session.persistFilter());
    if (serialized != m_cfg.autoFilterState) {
        m_cfg.autoFilterState = serialized;
        saveConfig(m_cfg);
    }

    const QVector<WordEntry> active = sortedByDifficulty(m_session.active(now, activeTtlMs()));
    m_bar->setWords(active);
    m_lastElapsedMs = result.elapsedMs;
    m_lastActiveWords = active.size();
    updateBarStatus();
    if (retired > 0) {
        m_bar->flashMessage(QStringLiteral("已自动过滤 %1 个词 · 稍后会重现").arg(retired));
    }

    if (m_details && m_details->isVisible())
        m_detailsRefresh->start();
}

void AppController::updateBarStatus()
{
    if (!m_engineReady) {
        m_bar->setStatus(QStringLiteral("未找到 Tesseract，请在设置中指定"),
                         QStringLiteral("error"));
        return;
    }
    if (m_paused) {
        m_bar->setStatus(QStringLiteral("已暂停"), QStringLiteral("warn"));
        return;
    }
    if (m_lastElapsedMs <= 0) {
        m_bar->setStatus(QStringLiteral("就绪"), QStringLiteral("ok"));
        return;
    }
    m_bar->setStatus(QStringLiteral("识别 %1s").arg(m_lastElapsedMs / 1000.0, 0, 'f', 1),
                     QStringLiteral("ok"));
}

int AppController::activeTtlMs() const
{
    // A ttl of 0 makes WordSession keep pending words until they are handled.
    return m_cfg.keepUntilHandled ? 0 : qMax(1, m_cfg.activeTtlSeconds) * 1000;
}

void AppController::refreshWords()
{
    m_bar->setWords(sortedByDifficulty(
        m_session.active(QDateTime::currentMSecsSinceEpoch(), activeTtlMs())));
}

QVector<WordEntry> AppController::sortedByDifficulty(QVector<WordEntry> words) const
{
    // Rarer words first (larger difficultyRank = harder), so the user sees the
    // words they're most likely to need before the ones they already pass over.
    std::stable_sort(words.begin(), words.end(),
                     [this](const WordEntry& a, const WordEntry& b) {
                         const int ra = m_filter.difficultyRank(a.word);
                         const int rb = m_filter.difficultyRank(b.word);
                         if (ra != rb)
                             return ra > rb;
                         if (a.count != b.count)
                             return a.count > b.count;
                         return a.firstSeen > b.firstSeen;
                     });
    return words;
}

void AppController::applyConfig(bool persist)
{
    m_filter.setConfig(m_cfg);
    m_bar->applyConfig(m_cfg);
    if (m_timer)
        m_timer->setInterval(qMax(300, m_cfg.intervalMs));
    emit configureRequested(m_cfg);
    refreshEngineState();
    refreshWords();
    updateBarStatus();
    if (persist)
        saveConfig(m_cfg);
}

void AppController::setPaused(bool paused)
{
    m_paused = paused;
    m_bar->setPaused(paused);
    if (m_pauseAction)
        m_pauseAction->setChecked(paused);
    updateBarStatus();
    if (!paused) {
        m_busy = false;
        QTimer::singleShot(80, this, &AppController::tick);
    }
}

void AppController::toggleBar()
{
    if (m_bar->isVisible()) {
        m_bar->hide();
    } else {
        m_bar->show();
        m_bar->raise();
    }
    if (m_toggleAction)
        m_toggleAction->setText(m_bar->isVisible() ? QStringLiteral("隐藏状态栏")
                                                   : QStringLiteral("显示状态栏"));
}

void AppController::copyWord(const QString& word)
{
    QGuiApplication::clipboard()->setText(word);
    m_session.touchInteraction(word);
    m_bar->flashMessage(QStringLiteral("已复制：%1").arg(word));
}

void AppController::markKnown(const QStringList& words)
{
    if (words.isEmpty())
        return;
    for (const QString& word : words) {
        const QString normalized = word.trimmed().toLower();
        if (!normalized.isEmpty() && !m_cfg.knownWords.contains(normalized))
            m_cfg.knownWords.append(normalized);
        m_session.removeWord(word);
    }
    saveConfig(m_cfg);
    m_filter.setConfig(m_cfg);
    refreshWords();
    m_bar->flashMessage(words.size() == 1
                            ? QStringLiteral("已掌握：%1").arg(words.first())
                            : QStringLiteral("已标记 %1 个词为已掌握").arg(words.size()));
    if (m_details)
        m_details->setEntries(m_session.all());
}

void AppController::blockWords(const QStringList& words)
{
    if (words.isEmpty())
        return;
    for (const QString& word : words) {
        const QString normalized = word.trimmed().toLower();
        if (!normalized.isEmpty() && !m_cfg.blockedWords.contains(normalized))
            m_cfg.blockedWords.append(normalized);
        m_session.removeWord(word);
    }
    saveConfig(m_cfg);
    m_filter.setConfig(m_cfg);
    refreshWords();
    m_bar->flashMessage(words.size() == 1
                            ? QStringLiteral("已隐藏：%1").arg(words.first())
                            : QStringLiteral("已隐藏 %1 个词").arg(words.size()));
    if (m_details)
        m_details->setEntries(m_session.all());
}

void AppController::openDetails()
{
    if (!m_details) {
        m_details = new DetailsPanel;
        m_details->setDictionary(&m_dictionary);
        connect(m_details, &DetailsPanel::clearRequested, this, [this] {
            m_session.clear();
            m_bar->setWords({});
            m_bar->flashMessage(QStringLiteral("已清空本次生词"));
            if (m_details)
                m_details->setEntries({});
        });
        connect(m_details, &DetailsPanel::markKnownRequested, this,
                [this](const QStringList& words) {
                    markKnown(words);
                    m_details->setEntries(m_session.all());
                });
        connect(m_details, &DetailsPanel::blockRequested, this,
                [this](const QStringList& words) {
                    blockWords(words);
                    m_details->setEntries(m_session.all());
                });
        connect(m_details, &DetailsPanel::restoreRequested, this,
                [this](const QStringList& words) {
                    int restored = 0;
                    for (const QString& word : words) {
                        if (m_session.restoreWord(word))
                            ++restored;
                    }
                    if (restored > 0) {
                        refreshWords();
                        m_bar->flashMessage(QStringLiteral("已恢复 %1 个词").arg(restored));
                    }
                    m_details->setEntries(m_session.all());
                });
    }
    m_details->setEntries(m_session.all());
    m_details->show();
    m_details->raise();
    m_details->activateWindow();
}

void AppController::openRegionSelector()
{
    // Stop scanning while the overlay is shown, otherwise we'd keep taking
    // captures of the dimmed fullscreen selector.
    const bool wasPaused = m_paused;
    if (!wasPaused) setPaused(true);

    auto* sel = new RegionSelector;
    sel->showFullScreen();
    sel->raise();
    sel->activateWindow();
    connect(sel, &RegionSelector::regionSelected, this,
            [this, wasPaused](const QRect& rect) {
                m_cfg.captureMode = 3;
                m_cfg.customRegion = rect.normalized();
                saveConfig(m_cfg);
                m_bar->applyConfig(m_cfg);
                m_bar->flashMessage(QStringLiteral("识别区域已保存 · %1×%2")
                                        .arg(rect.width()).arg(rect.height()));
                if (!wasPaused) {
                    setPaused(false);
                    m_timer->start(qMax(300, m_cfg.intervalMs));
                    QTimer::singleShot(200, this, &AppController::tick);
                }
            });
    connect(sel, &RegionSelector::cancelled, this, [this, wasPaused] {
        if (!wasPaused) setPaused(false);
    });
    // Delete on close so we don't accumulate overlays if the user triggers
    // selection while one is still open.
    connect(sel, &RegionSelector::destroyed, this, [this] {
        // Nothing — just letting Qt clean up.
    });
}

void AppController::openSettings()
{
    if (!m_settings) {
        m_settings = new SettingsDialog;
        // Live preview: visual-setting changes repaint the overlay bar immediately,
        // but we must save/restore its position because applyConfig re-centers
        // the bar every time.
        connect(m_settings, &SettingsDialog::previewRequested, this,
                [this](const AppConfig& cfg) {
                    const QPoint savedPos = m_bar->pos();
                    m_bar->applyConfig(cfg);
                    // applyConfig may re-center or jump the bar — restore to where the user had it
                    m_bar->move(savedPos);
                    // force clamp within screen bounds
                    if (const QScreen* s = m_bar->screen()) {
                        const QRect avail = s->availableGeometry();
                        int nx = qBound(avail.left(), m_bar->x(), avail.right() - m_bar->width());
                        int ny = qBound(avail.top(), m_bar->y(), avail.bottom() - m_bar->height());
                        if (nx != m_bar->x() || ny != m_bar->y())
                            m_bar->move(nx, ny);
                    }
                });
    }

    const AppConfig before = m_cfg;

    m_settings->setConfig(m_cfg);
    TesseractOcr probe(m_cfg.tesseractPath);
    m_settings->setEngineInfo(probe.executablePath(), probe.tessdataPath());
    m_settings->setHotkeyInfo(m_hotkeyOk);

    if (m_settings->exec() == QDialog::Accepted) {
        m_cfg = m_settings->config();
        applyConfig(true);
    } else {
        // Revert any live preview changes back to the config before the dialog.
        m_bar->applyConfig(before);
    }
}

QVector<QRect> AppController::exclusionRects(const QSize& imageSize, const QPoint& imageOffset) const
{
    QVector<QRect> rects;
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen || imageSize.width() <= 0)
        return rects;

    const QRect geometry = screen->geometry();
    if (geometry.width() <= 0)
        return rects;

    const double scale = double(imageSize.width()) / geometry.width();
    const auto add = [&](const QRect& logical) {
        if (!logical.isValid())
            return;
        const QRect scaled(qRound((logical.x() - geometry.x()) * scale),
                           qRound((logical.y() - geometry.y()) * scale),
                           qRound(logical.width() * scale),
                           qRound(logical.height() * scale));
        rects.append(scaled.translated(-imageOffset).adjusted(-2, -2, 2, 2));
    };

    if (m_bar && m_bar->isVisible()) {
        add(m_bar->frameGeometry());
        add(m_bar->popupGeometry());
    }
    if (m_details && m_details->isVisible())
        add(m_details->frameGeometry());
    if (m_settings && m_settings->isVisible())
        add(m_settings->frameGeometry());
    return rects;
}
