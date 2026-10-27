#pragma once

#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <QMetaType>

/// One word (with its pixel box) reported by the OCR engine.
struct WordHit
{
    QString text;
    QRect box;
    double confidence = 0.0;
};

/// Result of a single OCR pass.
struct OcrResult
{
    QVector<WordHit> words;
    QSize imageSize;
    int elapsedMs = 0;
    bool ok = false;
    QString error;
};

Q_DECLARE_METATYPE(OcrResult)
