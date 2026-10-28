#pragma once

#include "OcrTypes.h"
#include "Settings.h"

#include <QSet>
#include <QString>
#include <QStringList>

/// Decides whether an OCR token is an interesting English word.
///
/// Two dictionaries are used:
///  - words_alpha.txt  : ~370k English words, used to reject OCR garbage
///  - common-10000.txt : frequency ordered list, used to drop words the
///                       learner almost certainly already knows
class WordFilter
{
public:
    /// Loads both word lists from \a dataDir. Returns false when the
    /// dictionaries are missing (the app still runs, just less precisely).
    bool load(const QString& dataDir, QString* error = nullptr);

    void setConfig(const AppConfig& cfg);
    const AppConfig& config() const { return m_config; }

    int dictionarySize() const { return m_dictionary.size(); }
    int commonSize() const { return m_common.size(); }

    /// Normalises an OCR token; returns an empty string when it is not a
    /// plausible English word.
    QString normalize(const QString& raw) const;

    /// Applies "only new words" filtering (frequent list / known / blocked).
    bool isInteresting(const QString& word) const;

    bool isInDictionary(const QString& word) const { return inDictionary(word); }
    bool isCommon(const QString& word) const { return inCommon(word); }

    /// Difficulty rank for @p word: 0 = most common (easiest), larger = rarer
    /// (harder). Words absent from the frequency list rank highest of all.
    int difficultyRank(const QString& word) const;

    QStringList knownWords() const { return m_config.knownWords; }
    QStringList blockedWords() const { return m_config.blockedWords; }

private:
    bool inWordSet(const QSet<QString>& set, const QString& word) const;
    bool inDictionary(const QString& word) const { return inWordSet(m_dictionary, word); }
    bool inCommon(const QString& word) const { return inWordSet(m_common, word); }

    AppConfig m_config;
    QSet<QString> m_dictionary;
    QSet<QString> m_common;
    QStringList m_commonOrdered;  ///< frequency ordered source list
    int m_commonCountApplied = -1;
    QSet<QString> m_blocked;
    QSet<QString> m_known;
    bool m_loaded = false;
};
