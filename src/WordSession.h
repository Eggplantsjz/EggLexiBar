#pragma once

#include "OcrTypes.h"

#include <QHash>
#include <QPair>
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
    bool autoFiltered = false; ///< hidden by the memory-curve auto filter (NOT "known")
    int filterCount = 0;       ///< how many times it has been auto-filtered
    qint64 hiddenUntil = 0;    ///< earliest time it is allowed to reappear
    bool interacted = false;   ///< the user hovered/clicked it at least once
    QRect lastBox;
};

/// Keeps track of every word seen since the app was started.
///
/// A word can be in one of two "no longer on the bar" states:
///   - autoFiltered: the word kept appearing but the user never hovered/clicked
///     it, so it is temporarily hidden. It is NOT considered known — it will
///     resurface after a memory-curve interval (longer each time) so the user
///     gets another chance to swipe it when needed.
///   - known/blocked: handled by AppController (knownWords / blockedWords),
///     never fed back into this session. Only an explicit swipe/menu/list action
///     marks a word known — auto-filtering never does.
class WordSession
{
public:
    /// Persisted filter state: word -> (filterCount, hiddenUntil). `filterCount`
    /// drives the memory-curve interval; `hiddenUntil` is when it may resurface.
    using PersistFilter = QHash<QString, QPair<int, qint64>>;

    void ingest(const QVector<WordHit>& hits, qint64 nowMs);

    /// Words currently shown on the bar (not auto-filtered, not expired),
    /// newest first. A ttl of 0 (or less) keeps every pending word.
    QVector<WordEntry> active(qint64 nowMs, int ttlMs) const;

    /// Every word of the session (including auto-filtered), newest first.
    QVector<WordEntry> all() const;

    int size() const { return m_words.size(); }
    void clear();
    bool removeWord(const QString& word);

    /// Keeps words that are still on screen from expiring (used when a scan is
    /// skipped because the picture did not change).
    void touchRecent(qint64 nowMs);

    /// Records that the user hovered/clicked a word. This resets the auto-filter
    /// curve for that word (it is clearly being looked at) but does NOT mark it
    /// known — only an explicit swipe/menu/list action does that.
    void touchInteraction(const QString& word);

    /// Hides words that were seen in at least \a minScans batches, have been
    /// around for \a minAgeMs and were never interacted with. Each hide extends
    /// the memory-curve interval. Returns how many were hidden.
    int autoFilter(qint64 nowMs, int minScans, int minAgeMs);

    /// Re-activates auto-filtered words whose memory-curve interval has elapsed.
    void resurface(qint64 nowMs);

    /// Manually restores a word the user wants back (from the details list):
    /// clears filtering, resets the curve, and marks it interacted.
    bool restoreWord(const QString& word);

    int filteredCount() const;

    // --- persistence ---
    void setPersistFilter(const PersistFilter& state);
    PersistFilter persistFilter() const;

private:
    QHash<QString, WordEntry> m_words;
    PersistFilter m_persistFilter;
};