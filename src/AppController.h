#pragma once

#include "Dictionary.h"
#include "OcrTypes.h"
#include "Settings.h"
#include "TesseractOcr.h"
#include "WordFilter.h"
#include "WordSession.h"
#include "WordStore.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QStringList>
#include <QVector>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QTimer;
class QThread;
class OverlayBar;
class DetailsPanel;
class SettingsDialog;
class HotkeyFilter;

/// Runs tesseract on a dedicated thread so the bar never freezes.
class OcrWorker : public QObject
{
    Q_OBJECT
public:
    explicit OcrWorker(QObject* parent = nullptr);

public slots:
    void configure(const AppConfig& cfg);
    void process(const QImage& image, qint64 stamp);

signals:
    void finished(const OcrResult& result, qint64 stamp);

private:
    AppConfig m_cfg;
    TesseractOcr m_ocr;
};

/// Coordinates capture -> OCR -> filtering -> user interface.
class AppController : public QObject
{
    Q_OBJECT
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    void start();

signals:
    void ocrRequested(const QImage& image, qint64 stamp);
    void configureRequested(const AppConfig& cfg);

private:
    void createUi();
    void createTray();
    void createWorker();
    void loadDictionaries();
    void refreshEngineState();
    void tick();
    void handleOcr(const OcrResult& result, qint64 stamp);
    void applyConfig(bool persist);
    void setPaused(bool paused);
    void toggleBar();
    void copyWord(const QString& word);
    void markKnown(const QStringList& words);
    void blockWords(const QStringList& words);
    void openDetails();
    void openSettings();
    void openRegionSelector();
    void updateBarStatus();
    int activeTtlMs() const;
    void refreshWords();
    QVector<WordEntry> sortedByDifficulty(QVector<WordEntry> words) const;
    QRect interestRect(const QRect& screenGeometry) const;
    QVector<QRect> exclusionRects(const QSize& imageSize, const QPoint& imageOffset) const;

    AppConfig m_cfg;
    WordFilter m_filter;
    WordSession m_session;
    WordStore m_store;
    Dictionary m_dictionary;
    OverlayBar* m_bar = nullptr;
    DetailsPanel* m_details = nullptr;
    SettingsDialog* m_settings = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    QAction* m_toggleAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QTimer* m_timer = nullptr;
    QTimer* m_watchdog = nullptr;
    QTimer* m_detailsRefresh = nullptr;
    QTimer* m_scanTicker = nullptr;
    QThread* m_thread = nullptr;
    OcrWorker* m_worker = nullptr;
    HotkeyFilter* m_hotkey = nullptr;

    bool m_busy = false;
    bool m_paused = false;
    bool m_engineReady = false;
    bool m_hotkeyOk = true;
    bool m_trayHintShown = false;
    qint64 m_lastEngineCheck = 0;
    qint64 m_lastElapsedMs = 0;
    qint64 m_lastOcrAt = 0;
    int m_lastActiveWords = 0;
    QImage m_lastThumb;
    QPoint m_lastCropOffset;
    QElapsedTimer m_scanClock;
    QString m_lastNearestWord;   ///< for auto-card: track word under cursor
    QTimer* m_nearestCardTimer = nullptr;  ///< auto-hide the nearest-word card after 1s
};
