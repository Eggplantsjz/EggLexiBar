#pragma once

#include <QByteArrayView>
#include <QFile>
#include <QString>
#include <QStringList>

/// One dictionary entry as shown to the user.
struct WordDefinition
{
    QString word;       ///< the word that was looked up (original spelling)
    QString phonetic;   ///< IPA without surrounding slashes, may be empty
    QStringList senses; ///< Chinese (or English fallback) senses, one per line

    bool isValid() const { return !senses.isEmpty(); }
};

class Dictionary;

/// True when at least one sense is a gloss a learner can actually use.
///
/// The raw dictionary contains plenty of OCR-noise entries whose "meaning" is
/// a bare Chinese word. Real entries start with a part of speech ("n.", "vt.",
/// "interj.", ...) or a subject tag ("[计]", "[医]", ...); abbreviation-only
/// entries are not useful either.
bool hasUsableSense(const WordDefinition& definition);

/// True when the dictionary can show a definition card for \a word, so that a
/// chip never appears without a meaning behind it. Without a loaded dictionary
/// there is nothing to check and every word passes.
bool isListableWord(const Dictionary& dictionary, const QString& word);

/// Read-only lookup table for the compact offline dictionary (data/dict.bin).
/// The file is memory mapped; lookups are a binary search over the key table,
/// so opening the dictionary costs almost nothing and no heap copy is made.
class Dictionary
{
public:
    Dictionary() = default;
    ~Dictionary();

    Dictionary(const Dictionary&) = delete;
    Dictionary& operator=(const Dictionary&) = delete;

    bool load(const QString& path, QString* error = nullptr);
    void unload();

    bool isLoaded() const { return m_count > 0; }
    int size() const { return m_count; }

    /// Looks up \a word, falling back to simple base forms (plural, -ing, -ed, ...).
    WordDefinition lookup(const QString& word) const;

    /// Like lookup() but also tries common OCR-character confusables
    /// (e.g. '1'→'l', '0'→'o', '5'→'s') when the raw word has no match.
    /// Returns the first valid definition found, and writes the repaired
    /// spelling back to @p outWord if it differs from @p word.
    WordDefinition lookupRepaired(const QString& word, QString* outWord = nullptr) const;

    /// Looks up the exact spelling only.
    WordDefinition lookupExact(const QString& word) const;

private:
    struct Record
    {
        quint32 keyOffset = 0;
        quint32 defOffset = 0;
        quint16 keyLength = 0;
        quint16 defLength = 0;
    };

    Record recordAt(int index) const;
    QByteArrayView keyAt(const Record& record) const;
    QByteArrayView defAt(const Record& record) const;
    WordDefinition decode(const QString& key, const Record& record) const;
    static QString normalize(const QString& word);

    QFile m_file;
    const uchar* m_base = nullptr;
    qint64 m_size = 0;
    const uchar* m_blob = nullptr;
    qint64 m_blobSize = 0;
    int m_count = 0;
};
