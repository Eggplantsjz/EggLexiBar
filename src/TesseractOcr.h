#pragma once

#include "OcrTypes.h"
#include "Settings.h"

#include <QImage>
#include <QString>

/// Thin wrapper around the tesseract command line tool.
///
/// The engine is executed as a child process on purpose: it keeps the app
/// independent of the tesseract ABI / compiler that produced it.
class TesseractOcr
{
public:
    explicit TesseractOcr(const QString& configuredPath = QString());

    /// Re-resolve the executable (e.g. after the user changed the path).
    void setConfiguredPath(const QString& path);

    bool available() const { return !m_exe.isEmpty(); }
    QString executablePath() const { return m_exe; }
    QString tessdataPath() const { return m_tessdata; }
    QString lastError() const { return m_lastError; }

    /// Blocking call. Never throws.
    OcrResult recognize(const QImage& image, const AppConfig& cfg);

    static QString autoDetectExecutable();

private:
    void resolve();

    QString m_configured;
    QString m_exe;
    QString m_tessdata;
    QString m_lastError;
};
