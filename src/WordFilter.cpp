#include "WordFilter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {

bool isAsciiLetter(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z');
}

bool isWordChar(QChar c)
{
    return isAsciiLetter(c) || c == QLatin1Char('\'') || c == QLatin1Char('-');
}

} // namespace

bool WordFilter::load(const QString& dataDir, QString* error)
{
    m_dictionary.clear();
    m_common.clear();
    m_commonOrdered.clear();
    m_loaded = false;

    QFile dictFile(QDir(dataDir).filePath(QStringLiteral("words_alpha.txt")));
    if (!dictFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("无法读取词典: %1").arg(dictFile.fileName());
        return false;
    }

    QTextStream dictStream(&dictFile);
    dictStream.setEncoding(QStringConverter::Utf8);
    m_dictionary.reserve(400000);
    while (!dictStream.atEnd()) {
        const QString word = dictStream.readLine().trimmed().toLower();
        if (word.size() >= 2)
            m_dictionary.insert(word);
    }
    dictFile.close();

    QFile commonFile(QDir(dataDir).filePath(QStringLiteral("common-10000.txt")));
    if (commonFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream commonStream(&commonFile);
        commonStream.setEncoding(QStringConverter::Utf8);
        while (!commonStream.atEnd()) {
            const QString word = commonStream.readLine().trimmed().toLower();
            if (!word.isEmpty())
                m_commonOrdered.append(word);
        }
        commonFile.close();
    }

    m_commonCountApplied = -1;
    m_loaded = true;
    setConfig(m_config); // rebuilds the frequent-word subset
    if (error)
        error->clear();
    return true;
}

void WordFilter::setConfig(const AppConfig& cfg)
{
    m_config = cfg;

    m_blocked.clear();
    for (const QString& word : cfg.blockedWords)
        m_blocked.insert(word.trimmed().toLower());

    m_known.clear();
    for (const QString& word : cfg.knownWords)
        m_known.insert(word.trimmed().toLower());

    const int wanted = qBound(0, cfg.commonWordCount, m_commonOrdered.size());
    if (m_commonCountApplied != wanted) {
        m_common.clear();
        m_common.reserve(wanted);
        for (int i = 0; i < wanted; ++i)
            m_common.insert(m_commonOrdered.at(i));
        m_commonCountApplied = wanted;
    }
}

bool WordFilter::inWordSet(const QSet<QString>& set, const QString& word) const
{
    if (set.isEmpty() || word.isEmpty())
        return false;
    if (set.contains(word))
        return true;
    if (word.size() < 4)
        return false;

    const auto hit = [&set](const QString& base) {
        return base.size() >= 2 && set.contains(base);
    };

    if (word.endsWith(QLatin1String("ies")) && hit(word.left(word.size() - 3) + QLatin1Char('y')))
        return true;
    if (word.endsWith(QLatin1String("es")) && hit(word.left(word.size() - 2)))
        return true;
    if (word.endsWith(QLatin1Char('s')) && hit(word.left(word.size() - 1)))
        return true;
    if (word.endsWith(QLatin1String("ed"))) {
        const QString base = word.left(word.size() - 2);
        if (hit(base))
            return true;
        if (base.size() > 1 && base.at(base.size() - 1) == base.at(base.size() - 2)
            && hit(base.left(base.size() - 1)))
            return true;
        if (hit(base + QLatin1Char('e')))
            return true;
    }
    if (word.endsWith(QLatin1String("ing"))) {
        const QString base = word.left(word.size() - 3);
        if (hit(base))
            return true;
        if (hit(base + QLatin1Char('e')))
            return true;
        if (base.size() > 1 && base.at(base.size() - 1) == base.at(base.size() - 2)
            && hit(base.left(base.size() - 1)))
            return true;
    }
    if (word.endsWith(QLatin1String("ly")) && hit(word.left(word.size() - 2)))
        return true;
    if (word.endsWith(QLatin1String("est")) && hit(word.left(word.size() - 3)))
        return true;
    if (word.endsWith(QLatin1String("er")) && hit(word.left(word.size() - 2)))
        return true;
    return false;
}

QString WordFilter::normalize(const QString& raw) const
{
    QString s = raw;
    s.replace(QChar(0x2019), QLatin1Char('\'')); // right single quote -> apostrophe
    s.replace(QChar(0x2018), QLatin1Char('\''));
    s = s.trimmed();
    if (s.isEmpty())
        return {};

    // Trim punctuation that the OCR engine attached to the word.
    int start = 0;
    int end = s.size() - 1;
    while (start <= end && !isWordChar(s.at(start)))
        ++start;
    while (end >= start && !isWordChar(s.at(end)))
        --end;
    if (end < start)
        return {};
    s = s.mid(start, end - start + 1);

    for (const QChar c : s) {
        if (!isWordChar(c))
            return {};
    }

    QString lower = s.toLower();
    if (lower.size() < m_config.minWordLength || lower.size() > m_config.maxWordLength)
        return {};

    // "USB", "CPU", "PDF" ... usually not what a learner wants, and they
    // generate a lot of OCR noise.
    const bool allUpper = (s == s.toUpper());
    if (allUpper && lower.size() <= 4 && !inDictionary(lower))
        return {};

    if (lower.contains(QLatin1Char('-'))) {
        const QStringList parts = lower.split(QLatin1Char('-'), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            return {};
        for (const QString& part : parts) {
            if (part.size() < 2 || !isAsciiLetter(part.at(0)))
                return {};
            if (m_config.strictDictionary && !inDictionary(part))
                return {};
        }
        return lower;
    }

    const int apostrophe = lower.indexOf(QLatin1Char('\''));
    if (apostrophe >= 0) {
        static const QSet<QString> suffixes = {
            QStringLiteral("s"),  QStringLiteral("t"), QStringLiteral("re"),
            QStringLiteral("ve"), QStringLiteral("ll"), QStringLiteral("d"),
            QStringLiteral("m"),
        };
        const QString base = lower.left(apostrophe);
        const QString suffix = lower.mid(apostrophe + 1);
        if (base.size() < 2 || !suffixes.contains(suffix))
            return {};
        if (m_config.strictDictionary && !inDictionary(base))
            return {};
        return lower;
    }

    if (m_config.strictDictionary && !inDictionary(lower))
        return {};

    return lower;
}

bool WordFilter::isInteresting(const QString& word) const
{
    if (word.isEmpty())
        return false;
    if (inWordSet(m_known, word))
        return false;
    if (inWordSet(m_blocked, word))
        return false;
    if (!m_config.filterCommon)
        return true;
    return !inCommon(word);
}

int WordFilter::difficultyRank(const QString& word) const
{
    const int idx = m_commonOrdered.indexOf(word);
    if (idx >= 0)
        return idx;
    // Not in the frequent list → rarer than everything in it.
    return m_commonOrdered.size();
}
