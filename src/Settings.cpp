#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

QString appDataDir()
{
    const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    if (QDir(local).exists())
        return local;
#ifdef WORLDLENS_SOURCE_DIR
    const QString source = QStringLiteral(WORLDLENS_SOURCE_DIR) + QStringLiteral("/data");
    if (QDir(source).exists())
        return source;
#endif
    return local;
}

QString configFilePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("wordlens.ini"));
}

AppConfig loadConfig()
{
    AppConfig cfg;
    QSettings s(configFilePath(), QSettings::IniFormat);

    cfg.intervalMs = s.value(QStringLiteral("ocr/intervalMs"), cfg.intervalMs).toInt();
    cfg.minWordLength = s.value(QStringLiteral("ocr/minWordLength"), cfg.minWordLength).toInt();
    cfg.maxWordLength = s.value(QStringLiteral("ocr/maxWordLength"), cfg.maxWordLength).toInt();
    cfg.ocrScalePercent = s.value(QStringLiteral("ocr/scalePercent"), cfg.ocrScalePercent).toInt();
    cfg.psm = s.value(QStringLiteral("ocr/psm"), cfg.psm).toInt();
    cfg.minConfidence = s.value(QStringLiteral("ocr/minConfidence"), cfg.minConfidence).toInt();
    cfg.captureMode = s.value(QStringLiteral("ocr/captureMode"), cfg.captureMode).toInt();
    cfg.language = s.value(QStringLiteral("ocr/language"), cfg.language).toString();
    cfg.tesseractPath = s.value(QStringLiteral("ocr/tesseractPath"), cfg.tesseractPath).toString();

    cfg.filterCommon = s.value(QStringLiteral("words/filterCommon"), cfg.filterCommon).toBool();
    cfg.commonWordCount = s.value(QStringLiteral("words/commonWordCount"), cfg.commonWordCount).toInt();
    cfg.strictDictionary = s.value(QStringLiteral("words/strictDictionary"), cfg.strictDictionary).toBool();
    cfg.knownWords = s.value(QStringLiteral("words/known"), cfg.knownWords).toStringList();
    cfg.blockedWords = s.value(QStringLiteral("words/blocked"), cfg.blockedWords).toStringList();

    cfg.maxChips = s.value(QStringLiteral("ui/maxChips"), cfg.maxChips).toInt();
    cfg.fontSize = s.value(QStringLiteral("ui/fontSize"), cfg.fontSize).toInt();
    cfg.opacityPercent = s.value(QStringLiteral("ui/opacity"), cfg.opacityPercent).toInt();
    cfg.activeTtlSeconds = s.value(QStringLiteral("ui/ttlSeconds"), cfg.activeTtlSeconds).toInt();
    cfg.keepUntilHandled =
        s.value(QStringLiteral("ui/keepUntilHandled"), cfg.keepUntilHandled).toBool();
    cfg.autoSnooze = s.value(QStringLiteral("ui/autoSnooze"), cfg.autoSnooze).toBool();
    cfg.autoSnoozeScans =
        s.value(QStringLiteral("ui/autoSnoozeScans"), cfg.autoSnoozeScans).toInt();
    cfg.autoSnoozeSeconds =
        s.value(QStringLiteral("ui/autoSnoozeSeconds"), cfg.autoSnoozeSeconds).toInt();
    cfg.autoHide = s.value(QStringLiteral("ui/autoHide"), cfg.autoHide).toBool();
    cfg.autoHideSeconds = s.value(QStringLiteral("ui/autoHideSeconds"), cfg.autoHideSeconds).toInt();

    cfg.barPosSet = s.value(QStringLiteral("ui/barPosSet"), false).toBool();
    cfg.barPos = s.value(QStringLiteral("ui/barPos"), QPoint()).toPoint();

    return cfg;
}

void saveConfig(const AppConfig& cfg)
{
    const QString path = configFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSettings s(path, QSettings::IniFormat);
    s.setValue(QStringLiteral("ocr/intervalMs"), cfg.intervalMs);
    s.setValue(QStringLiteral("ocr/minWordLength"), cfg.minWordLength);
    s.setValue(QStringLiteral("ocr/maxWordLength"), cfg.maxWordLength);
    s.setValue(QStringLiteral("ocr/scalePercent"), cfg.ocrScalePercent);
    s.setValue(QStringLiteral("ocr/psm"), cfg.psm);
    s.setValue(QStringLiteral("ocr/minConfidence"), cfg.minConfidence);
    s.setValue(QStringLiteral("ocr/captureMode"), cfg.captureMode);
    s.setValue(QStringLiteral("ocr/language"), cfg.language);
    s.setValue(QStringLiteral("ocr/tesseractPath"), cfg.tesseractPath);

    s.setValue(QStringLiteral("words/filterCommon"), cfg.filterCommon);
    s.setValue(QStringLiteral("words/commonWordCount"), cfg.commonWordCount);
    s.setValue(QStringLiteral("words/strictDictionary"), cfg.strictDictionary);
    s.setValue(QStringLiteral("words/known"), cfg.knownWords);
    s.setValue(QStringLiteral("words/blocked"), cfg.blockedWords);

    s.setValue(QStringLiteral("ui/maxChips"), cfg.maxChips);
    s.setValue(QStringLiteral("ui/fontSize"), cfg.fontSize);
    s.setValue(QStringLiteral("ui/opacity"), cfg.opacityPercent);
    s.setValue(QStringLiteral("ui/ttlSeconds"), cfg.activeTtlSeconds);
    s.setValue(QStringLiteral("ui/keepUntilHandled"), cfg.keepUntilHandled);
    s.setValue(QStringLiteral("ui/autoSnooze"), cfg.autoSnooze);
    s.setValue(QStringLiteral("ui/autoSnoozeScans"), cfg.autoSnoozeScans);
    s.setValue(QStringLiteral("ui/autoSnoozeSeconds"), cfg.autoSnoozeSeconds);
    s.setValue(QStringLiteral("ui/autoHide"), cfg.autoHide);
    s.setValue(QStringLiteral("ui/autoHideSeconds"), cfg.autoHideSeconds);
    s.setValue(QStringLiteral("ui/barPosSet"), cfg.barPosSet);
    s.setValue(QStringLiteral("ui/barPos"), cfg.barPos);

    s.sync();
}
