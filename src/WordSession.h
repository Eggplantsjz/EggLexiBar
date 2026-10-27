#pragma once

#include "OcrTypes.h"

#include <QHash>
#include <QRect>
#include <QString>
#include <QVector>

/// A word that was found on screen during this session.
struct WordEntry
{
    QString word;
    int count = 0;   ///< total number of hits
    int scans = 0;   ///< number of OCR batches that contained the word
    qint64 firstSeen = 0;
    qint64 lastSeen = 0;
    bool snoozed = false;    ///< auto-retired because it kept showing up
    bool interacted = false; ///< the user hovered/clicked it at least once
    QRect lastBox;
};

/// Keeps track of every word seen since the app was started.
class WordSession
{
public:
    void ingest(const QVector<WordHit>& hits, qint64 nowMs);

    /// Words seen within the last \a ttlMs, newest first. A ttl of 0 (or less)
    /// keeps every pending word until it is handled.
    QVector<WordEntry> active(qint64 nowMs, int ttlMs) const;

    /// Every word of the session, newest first.
    QVector<WordEntry> all() const;

    int size() const { return m_words.size(); }
    void clear() { m_words.clear(); }
    bool removeWord(const QString& word) { return m_words.remove(word) > 0; }

    /// Keeps words that are still on screen from expiring (used when a scan is
    /// skipped because the picture did not change).
    void touchRecent(qint64 nowMs);

    /// Marks a word as inspected so auto-snooze leaves it alone.
    void touchInteraction(const QString& word);

    /// Retires words that were seen in at least \a minScans batches, have been
    /// around for \a minAgeMs and were never inspected. Returns how many.
    int autoSnooze(qint64 nowMs, int minScans, int minAgeMs);

    /// Shows or hides a snoozed word again. Returns true when the word exists.
    bool setSnoozed(const QString& word, bool snoozed);

    int snoozedCount() const;

private:
    QHash<QString, WordEntry> m_words;
};
