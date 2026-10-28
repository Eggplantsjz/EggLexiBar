#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>

/// Everything the application needs to remember between runs.
struct AppConfig
{
    // --- OCR ---
    int intervalMs = 1500;      ///< delay between two screen scans
    int minWordLength = 3;
    int maxWordLength = 24;
    int ocrScalePercent = 150;  ///< 100 / 150 / 200  (150% makes small UI text ~21px tall)
    int psm = 6;                ///< 6 = uniform block of text (best for UI screen grabs)
    int minConfidence = 45;     ///< 0..100  (lowered from 55 so we catch more words)
    int captureMode = 0;        ///< 0 = whole screen, 1 = active window, 2 = cursor region, 3 = custom region
    QRect customRegion;         ///< user-drawn region; empty = not set

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
    bool autoFilter = true;       ///< hide words the user keeps not interacting with
    int autoFilterScans = 3;      ///< ... after they were seen in this many scans
    int autoFilterSeconds = 20;   ///< ... and stayed around for this long
    QStringList autoFilterState;  ///< persisted "word|filterCount|hiddenUntilMs" entries
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
