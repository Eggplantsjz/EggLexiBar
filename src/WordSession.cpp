#include "WordSession.h"

#include <QDateTime>
#include <QSet>
#include <algorithm>

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
    // ttlMs is already in milliseconds, exactly like nowMs/lastSeen.
    const qint64 threshold = nowMs - qint64(qMax(0, ttlMs));
    QVector<WordEntry> result;
    result.reserve(m_words.size());
    for (const WordEntry& entry : m_words) {
        if (entry.snoozed)
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
    if (it != m_words.end())
        it->interacted = true;
}

int WordSession::autoSnooze(qint64 nowMs, int minScans, int minAgeMs)
{
    const int scansNeeded = qMax(2, minScans);
    const qint64 ageNeeded = qMax(0, minAgeMs);
    int retired = 0;
    for (auto it = m_words.begin(); it != m_words.end(); ++it) {
        WordEntry& entry = it.value();
        if (entry.snoozed || entry.interacted)
            continue;
        if (entry.scans < scansNeeded)
            continue;
        if (nowMs - entry.firstSeen < ageNeeded)
            continue;
        entry.snoozed = true;
        ++retired;
    }
    return retired;
}

bool WordSession::setSnoozed(const QString& word, bool snoozed)
{
    auto it = m_words.find(word);
    if (it == m_words.end())
        return false;
    it->snoozed = snoozed;
    if (!snoozed)
        it->lastSeen = QDateTime::currentMSecsSinceEpoch();
    return true;
}

int WordSession::snoozedCount() const
{
    int count = 0;
    for (const WordEntry& entry : m_words) {
        if (entry.snoozed)
            ++count;
    }
    return count;
}
