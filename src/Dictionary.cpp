#include "Dictionary.h"

#include <QHash>
#include <QSet>
#include <QtEndian>

#include <cstring>

namespace {

constexpr char kMagic[8] = {'W', 'L', 'D', 'I', 'C', 'T', '1', '\0'};
constexpr int kHeaderSize = 16;
constexpr int kRecordSize = 12;

/// Part-of-speech markers used by ECDICT ("n.", "vt.", "interj." ...).
const QStringList& posPrefixes()
{
    static const QStringList prefixes = {
        QStringLiteral("n."),      QStringLiteral("v."),     QStringLiteral("vt."),
        QStringLiteral("vi."),     QStringLiteral("a."),     QStringLiteral("adj."),
        QStringLiteral("ad."),     QStringLiteral("adv."),   QStringLiteral("prep."),
        QStringLiteral("conj."),   QStringLiteral("pron."),  QStringLiteral("num."),
        QStringLiteral("art."),    QStringLiteral("interj."), QStringLiteral("int."),
        QStringLiteral("aux."),    QStringLiteral("pref."),  QStringLiteral("suf."),
        QStringLiteral("pl."),     QStringLiteral("ph."),
    };
    return prefixes;
}

/// Domain markers that say nothing about the word's meaning for a learner:
/// web slang, personal names and place names (usually transliterations).
bool isUsefulTag(const QString& tag)
{
    static const QSet<QString> ignored = {
        QStringLiteral("网络"), QStringLiteral("人名"), QStringLiteral("地名"),
        QStringLiteral("姓氏"), QStringLiteral("网络俚语"),
    };
    return !tag.isEmpty() && !ignored.contains(tag);
}

/// Candidates are returned in the most likely order.
QStringList baseForms(const QString& word)
{
    QStringList forms;
    const auto add = [&forms](const QString& candidate) {
        if (candidate.size() >= 2 && !forms.contains(candidate))
            forms.append(candidate);
    };

    const int n = word.size();

    if (word.endsWith(QLatin1String("ies")) && n > 4)
        add(word.left(n - 3) + QLatin1Char('y'));
    if (word.endsWith(QLatin1String("ves")) && n > 4) {
        add(word.left(n - 3) + QLatin1Char('f'));
        add(word.left(n - 3) + QLatin1String("fe"));
    }
    if (word.endsWith(QLatin1String("ches")) || word.endsWith(QLatin1String("shes"))
        || word.endsWith(QLatin1String("sses")) || word.endsWith(QLatin1String("xes"))
        || word.endsWith(QLatin1String("zes"))) {
        add(word.left(n - 2));
    }
    if (word.endsWith(QLatin1String("ies")) && n > 4)
        add(word.left(n - 1));
    if (word.endsWith(QLatin1Char('s')) && !word.endsWith(QLatin1String("ss")) && n > 3)
        add(word.left(n - 1));

    if (word.endsWith(QLatin1String("ied")) && n > 4)
        add(word.left(n - 3) + QLatin1Char('y'));
    if (word.endsWith(QLatin1String("ed")) && n > 3) {
        add(word.left(n - 2));
        add(word.left(n - 1));
        if (n > 4 && word.at(n - 3) == word.at(n - 4))
            add(word.left(n - 3));
    }

    if (word.endsWith(QLatin1String("ing")) && n > 4) {
        add(word.left(n - 3));
        add(word.left(n - 3) + QLatin1Char('e'));
        if (n > 5 && word.at(n - 4) == word.at(n - 5))
            add(word.left(n - 4));
    }

    if (word.endsWith(QLatin1String("iest")) && n > 5)
        add(word.left(n - 4) + QLatin1Char('y'));
    if (word.endsWith(QLatin1String("ier")) && n > 4)
        add(word.left(n - 3) + QLatin1Char('y'));

    if (word.endsWith(QLatin1String("est")) && n > 4) {
        add(word.left(n - 3));
        add(word.left(n - 2));
    }
    if (word.endsWith(QLatin1String("er")) && n > 3) {
        add(word.left(n - 1));
        add(word.left(n - 2));
    }
    if (word.endsWith(QLatin1String("ly")) && n > 4) {
        add(word.left(n - 2));
    }

    return forms;
}

} // namespace

Dictionary::~Dictionary()
{
    unload();
}

QString Dictionary::normalize(const QString& word)
{
    QString out;
    out.reserve(word.size());
    for (const QChar c : word) {
        if (c.unicode() < 128 && c.isLetter())
            out.append(c.toLower());
        else if (c == QLatin1Char('\''))
            out.append(c);
    }
    return out;
}

bool Dictionary::load(const QString& path, QString* error)
{
    unload();

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开词典文件：%1").arg(path);
        return false;
    }

    m_size = m_file.size();
    if (m_size < kHeaderSize) {
        if (error)
            *error = QStringLiteral("词典文件不完整");
        unload();
        return false;
    }

    m_base = m_file.map(0, m_size);
    if (!m_base) {
        if (error)
            *error = QStringLiteral("词典文件映射失败");
        unload();
        return false;
    }

    if (std::memcmp(m_base, kMagic, sizeof(kMagic)) != 0) {
        if (error)
            *error = QStringLiteral("词典文件格式不匹配");
        unload();
        return false;
    }

    const quint32 count = qFromLittleEndian<quint32>(m_base + 8);
    const quint32 blobOffset = qFromLittleEndian<quint32>(m_base + 12);
    if (blobOffset != quint32(kHeaderSize + qint64(count) * kRecordSize)
        || qint64(blobOffset) > m_size) {
        if (error)
            *error = QStringLiteral("词典索引损坏");
        unload();
        return false;
    }

    m_count = int(count);
    m_blob = m_base + blobOffset;
    m_blobSize = m_size - blobOffset;

    for (int i = 0; i < m_count; ++i) {
        const Record r = recordAt(i);
        const qint64 keyEnd = qint64(r.keyOffset) + r.keyLength;
        const qint64 defEnd = qint64(r.defOffset) + r.defLength;
        if (keyEnd > m_blobSize || defEnd > m_blobSize || r.keyLength == 0
            || r.defLength == 0) {
            if (error)
                *error = QStringLiteral("词典索引损坏");
            unload();
            return false;
        }
    }

    return true;
}

void Dictionary::unload()
{
    if (m_base) {
        m_file.unmap(const_cast<uchar*>(m_base));
        m_base = nullptr;
    }
    m_blob = nullptr;
    m_blobSize = 0;
    m_count = 0;
    m_size = 0;
    if (m_file.isOpen())
        m_file.close();
}

Dictionary::Record Dictionary::recordAt(int index) const
{
    const uchar* p = m_base + kHeaderSize + qint64(index) * kRecordSize;
    Record r;
    r.keyOffset = qFromLittleEndian<quint32>(p);
    r.defOffset = qFromLittleEndian<quint32>(p + 4);
    r.keyLength = qFromLittleEndian<quint16>(p + 8);
    r.defLength = qFromLittleEndian<quint16>(p + 10);
    return r;
}

QByteArrayView Dictionary::keyAt(const Record& record) const
{
    return QByteArrayView(reinterpret_cast<const char*>(m_blob + record.keyOffset),
                          record.keyLength);
}

QByteArrayView Dictionary::defAt(const Record& record) const
{
    return QByteArrayView(reinterpret_cast<const char*>(m_blob + record.defOffset),
                          record.defLength);
}

WordDefinition Dictionary::decode(const QString& key, const Record& record) const
{
    WordDefinition def;
    def.word = key;

    const QString text = QString::fromUtf8(defAt(record));
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty())
        def.phonetic = lines.takeFirst().trimmed();

    for (const QString& line : lines) {
        const QString sense = line.trimmed();
        if (!sense.isEmpty())
            def.senses.append(sense);
    }
    return def;
}

WordDefinition Dictionary::lookupExact(const QString& word) const
{
    if (!isLoaded())
        return {};

    const QString key = normalize(word);
    if (key.isEmpty())
        return {};

    const QByteArray needle = key.toUtf8();
    const QByteArrayView needleView(needle);

    int lo = 0;
    int hi = m_count - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const Record record = recordAt(mid);
        const int cmp = keyAt(record).compare(needleView);
        if (cmp == 0)
            return decode(key, record);
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return {};
}

WordDefinition Dictionary::lookup(const QString& word) const
{
    WordDefinition direct = lookupExact(word);
    if (direct.isValid())
        return direct;

    const QString key = normalize(word);
    if (key.isEmpty())
        return {};

    for (const QString& form : baseForms(key)) {
        WordDefinition def = lookupExact(form);
        if (def.isValid()) {
            def.word = word.trimmed();
            def.senses.prepend(QStringLiteral("原形：%1").arg(form));
            return def;
        }
    }
    return {};
}

WordDefinition Dictionary::lookupRepaired(const QString& word, QString* outWord) const
{
    // 1) Try raw lookup first — common case, zero extra cost.
    WordDefinition def = lookup(word);
    if (def.isValid()) {
        if (outWord) *outWord = word;
        return def;
    }

    const QString key = normalize(word);
    if (key.isEmpty())
        return {};

    // 2) Single-character OCR-confusable repair. Tesseract often confuses:
    //    0↔o, 1↔l/I, 2↔z, 5↔s, 6↔g, 8↔b.
    //    We ONLY replace DIGIT→LETTER because the reverse never produces valid
    //    English words; trying each single position keeps this OCR-repair pass
    //    to at most (6-digit-variants × len) dictionary lookups — still microseconds.
    static const QHash<QChar, QString> repairs = {
        {QLatin1Char('0'), QStringLiteral("oO")},
        {QLatin1Char('1'), QStringLiteral("lI")},
        {QLatin1Char('2'), QStringLiteral("zZ")},
        {QLatin1Char('5'), QStringLiteral("sS")},
        {QLatin1Char('6'), QStringLiteral("gG")},
        {QLatin1Char('8'), QStringLiteral("bB")},
    };

    for (int i = 0; i < key.size(); ++i) {
        const QChar c = key.at(i);
        auto it = repairs.find(c);
        if (it == repairs.end()) continue;

        QString candidate = key;
        for (const QChar& replacement : it.value()) {
            candidate[i] = replacement;
            // lookup() already tries exact + base forms.
            def = lookup(candidate);
            if (def.isValid()) {
                // Report the dictionary spelling back so the filter/session
                // store the repaired word instead of the garbled OCR output.
                def.word = def.word.toLower();  // normalize
                if (outWord) *outWord = candidate;
                return def;
            }
        }
    }

    // 3) Two-character merges that OCR sometimes produces: 'rn' read as 'm',
    //    'vv' read as 'w'. Try collapsing each pair.
    static const QPair<QString, QString> merges[] = {
        {QStringLiteral("rn"), QStringLiteral("m")},
        {QStringLiteral("vv"), QStringLiteral("w")},
        {QStringLiteral("ii"), QStringLiteral("u")},
        {QStringLiteral("cl"), QStringLiteral("d")},
    };
    for (const auto& pair : merges) {
        const QString& from = pair.first;
        const QString& to = pair.second;
        int idx = 0;
        while ((idx = key.indexOf(from, idx)) >= 0) {
            QString candidate = key;
            candidate.replace(idx, from.size(), to);
            def = lookup(candidate);
            if (def.isValid()) {
                def.word = def.word.toLower();
                if (outWord) *outWord = candidate;
                return def;
            }
            ++idx;
        }
    }

    return {};
}

bool hasUsableSense(const WordDefinition& definition)
{
    for (const QString& rawSense : definition.senses) {
        const QString sense = rawSense.trimmed();
        if (sense.isEmpty() || sense.startsWith(QStringLiteral("原形：")))
            continue;
        if (sense.startsWith(QStringLiteral("abbr"), Qt::CaseInsensitive))
            continue;

        for (const QString& prefix : posPrefixes()) {
            if (sense.startsWith(prefix, Qt::CaseInsensitive))
                return true;
        }

        // "[计] 属性", "[医] 水" ... a subject-tagged gloss is specific enough
        // to be worth showing.
        if (sense.startsWith(QLatin1Char('[')) && sense.contains(QLatin1Char(']'))) {
            const QString tag = sense.mid(1, sense.indexOf(QLatin1Char(']')) - 1).trimmed();
            if (isUsefulTag(tag))
                return true;
        }
    }
    return false;
}

bool isListableWord(const Dictionary& dictionary, const QString& word)
{
    if (!dictionary.isLoaded())
        return true;
    return hasUsableSense(dictionary.lookup(word));
}
