#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>

/// Everything the application needs to remember between runs.
struct AppConfig
{
    // --- OCR ---
    int intervalMs = 1500;      ///< delay between two screen scans
    int minWordLength = 3;
    int maxWordLength = 24;
    int ocrScalePercent = 100;  ///< 100 / 150 / 200
    int psm = 11;               ///< tesseract page segmentation mode
    int minConfidence = 55;     ///< 0..100
    int captureMode = 0;        ///< 0 = whole screen, 1 = active window, 2 = around cursor

    // --- word filtering ---
    bool filterCommon = true;      ///< only show words outside the frequent list
    int commonWordCount = 3000;    ///< how many entries of the frequent list to use
    bool strictDictionary = true;  ///< drop words that are not in the dictionary

    // --- presentation ---
    int maxChips = 14;
    int fontSize = 13;
    int opacityPercent = 88;
    int activeTtlSeconds = 90;  ///< how long a word stays in the bar
    bool keepUntilHandled = true; ///< pending words never expire until handled
    bool autoSnooze = true;       ///< quietly retire words that keep appearing
    int autoSnoozeScans = 3;      ///< ... after they were seen in this many scans
    int autoSnoozeSeconds = 20;   ///< ... and stayed around for this long
    bool autoHide = false;
    int autoHideSeconds = 20;

    // --- window placement ---
    bool barPosSet = false;
    QPoint barPos;

    // --- OCR engine ---
    QString language = QStringLiteral("eng");
    QString tesseractPath;  ///< empty = auto detect

    // --- user vocabulary ---
    QStringList knownWords;    ///< marked as already known
    QStringList blockedWords;  ///< never show again
};

/// Directory that contains words_alpha.txt / common-10000.txt.
QString appDataDir();

/// Full path of the ini file used to persist AppConfig.
QString configFilePath();

AppConfig loadConfig();
void saveConfig(const AppConfig& cfg);
