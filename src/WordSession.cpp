#include "WordSession.h"

#include <QDateTime>
#include <QSet>
#include <algorithm>

namespace {

/// Memory-curve interval for the (1-based) filter round. Round 1 means the word
/// was hidden for the first time: it comes back quickly. Each subsequent round
/// (the word surfaced and was ignored again) waits longer, converging toward
/// "nearly never appears" — but it is still NOT marked known.
qint64 filterIntervalMs(int filterCount)
{
    static const qint64 intervals[] = {
        2 * 60 * 1000LL,        // 1st: 2 minutes
        10 * 60 * 1000LL,       // 2nd: 10 minutes
        60 * 60 * 1000LL,       // 3rd: 1 hour
        6 * 60 * 60 * 1000LL,   // 4th: 6 hours
        24 * 60 * 60 * 1000LL,  // 5th: 1 day
        3 * 24 * 60 * 60 * 1000LL,   // 6th: 3 days
        7 * 24 * 60 * 60 * 1000LL,   // 7th: 7 days
        14 * 24 * 60 * 60 * 1000LL,  // 8th: 14 days
        30 * 24 * 60 * 60 * 1000LL,  // 9th+: 30 days
    };
    const int last = int(sizeof(intervals) / sizeof(intervals[0])) - 1;
    const int idx = std::min(std::max(1, filterCount), last + 1) - 1;
    return intervals[idx];
}

} // namespace

void WordSession::ingest(const QVector<WordHit>& hits, qint64 nowMs)
{
    QHash<QString, int> perFrame;
    QHash<QString, QRect> boxes;
    for (const WordHit& hit : hits) {
        if (hit.text.isEmpty())
            continue;
        perFrame[hit.text] += 1;
        boxes[hit.text] = hit.box;
    }

    for (auto it = perFrame.constBegin(); it != perFrame.constEnd(); ++it) {
        auto found = m_words.find(it.key());
        if (found == m_words.end()) {
            WordEntry entry;
            entry.word = it.key();
            entry.count = it.value();
            entry.scans = 1;
            entry.firstSeen = nowMs;
            entry.lastSeen = nowMs;
            entry.lastBox = boxes.value(it.key());

            // Restore a persisted filter state, if any. A word that was hidden
            // before a restart stays hidden until its interval elapses.
            auto persisted = m_persistFilter.find(it.key());
            if (persisted != m_persistFilter.end()) {
                entry.filterCount = persisted.value().first;
                entry.hiddenUntil = persisted.value().second;
                entry.autoFiltered = entry.hiddenUntil > nowMs;
            }
            m_words.insert(it.key(), entry);
        } else {
            found->count += it.value();
            found->scans += 1;
            found->lastSeen = nowMs;
            found->lastBox = boxes.value(it.key());
        }
    }
}

QVector<WordEntry> WordSession::active(qint64 nowMs, int ttlMs) const
{
    const bool keepForever = ttlMs <= 0;
    const qint64 threshold = nowMs - qint64(std::max(0, ttlMs));
    QVector<WordEntry> result;
    result.reserve(m_words.size());
    for (const WordEntry& entry : m_words) {
        if (entry.autoFiltered)
            continue;
        if (keepForever || entry.lastSeen >= threshold)
            result.append(entry);
    }
    std::sort(result.begin(), result.end(), [](const WordEntry& a, const WordEntry& b) {
        if (a.firstSeen != b.firstSeen)
            return a.firstSeen > b.firstSeen;
        return a.word < b.word;
    });
    return result;
}

QVector<WordEntry> WordSession::all() const
{
    QVector<WordEntry> result;
    result.reserve(m_words.size());
    for (const WordEntry& entry : m_words)
        result.append(entry);
    std::sort(result.begin(), result.end(), [](const WordEntry& a, const WordEntry& b) {
        if (a.firstSeen != b.firstSeen)
            return a.firstSeen > b.firstSeen;
        return a.word < b.word;
    });
    return result;
}

void WordSession::clear()
{
    m_words.clear();
    m_persistFilter.clear();
}

bool WordSession::removeWord(const QString& word)
{
    m_persistFilter.remove(word);
    return m_words.remove(word) > 0;
}

void WordSession::touchRecent(qint64 nowMs)
{
    const qint64 cutoff = nowMs - 5 * 60 * 1000;
    for (auto it = m_words.begin(); it != m_words.end(); ++it) {
        if (it->lastSeen >= cutoff)
            it->lastSeen = nowMs;
    }
}

void WordSession::touchInteraction(const QString& word)
{
    auto it = m_words.find(word);
    if (it == m_words.end())
        return;
    it->interacted = true;
    // The user looked at it: cancel the auto-filter curve so it isn't hidden
    // for something the user is clearly paying attention to. This does NOT mark
    // it known.
    if (it->autoFiltered || it->filterCount > 0) {
        it->autoFiltered = false;
        it->filterCount = 0;
        it->hiddenUntil = 0;
        m_persistFilter.remove(word);
    }
}

int WordSession::autoFilter(qint64 nowMs, int minScans, int minAgeMs)
{
    const int scansNeeded = std::max(2, minScans);
    const qint64 ageNeeded = std::max(0LL, qint64(minAgeMs));
    int hidden = 0;
    for (auto it = m_words.begin(); it != m_words.end(); ++it) {
        WordEntry& entry = it.value();
        if (entry.autoFiltered || entry.interacted)
            continue;
        if (entry.scans < scansNeeded)
            continue;
        if (nowMs - entry.firstSeen < ageNeeded)
            continue;

        entry.filterCount += 1;
        entry.hiddenUntil = nowMs + filterIntervalMs(entry.filterCount);
        entry.autoFiltered = true;
        m_persistFilter.insert(it.key(), qMakePair(entry.filterCount, entry.hiddenUntil));
        ++hidden;
    }
    return hidden;
}

void WordSession::resurface(qint64 nowMs)
{
    for (auto it = m_words.begin(); it != m_words.end(); ++it) {
        WordEntry& entry = it.value();
        if (entry.autoFiltered && entry.hiddenUntil <= nowMs) {
            entry.autoFiltered = false;
        }
    }
}

bool WordSession::restoreWord(const QString& word)
{
    auto it = m_words.find(word);
    if (it == m_words.end())
        return false;
    it->autoFiltered = false;
    it->filterCount = 0;
    it->hiddenUntil = 0;
    it->interacted = true;
    it->lastSeen = QDateTime::currentMSecsSinceEpoch();
    m_persistFilter.remove(word);
    return true;
}

int WordSession::filteredCount() const
{
    int count = 0;
    for (const WordEntry& entry : m_words) {
        if (entry.autoFiltered)
            ++count;
    }
    return count;
}

void WordSession::setPersistFilter(const PersistFilter& state)
{
    m_persistFilter = state;
}

WordSession::PersistFilter WordSession::persistFilter() const
{
    return m_persistFilter;
}