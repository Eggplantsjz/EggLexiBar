// Headless checks for the "inbox" rules of WordSession.
// Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-session.ps1
#include "WordSession.h"

#include "Dictionary.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStringList>

namespace {

int g_failed = 0;

void check(bool ok, const QString& what)
{
    if (ok) {
        qInfo().noquote() << "  ok   " << what;
    } else {
        qWarning().noquote() << "  FAIL " << what;
        ++g_failed;
    }
}

QVector<WordHit> hits(const QStringList& words, const QRect& box = QRect(10, 10, 40, 18))
{
    QVector<WordHit> result;
    result.reserve(words.size());
    for (const QString& word : words) {
        WordHit hit;
        hit.text = word;
        hit.box = box;
        hit.confidence = 90;
        result.append(hit);
    }
    return result;
}

QStringList wordsOf(const QVector<WordEntry>& entries)
{
    QStringList result;
    for (const WordEntry& entry : entries)
        result.append(entry.word);
    result.sort();
    return result;
}

/// A word that keeps showing up and is never inspected gets retired.
void testAutoSnoozeRetiresRepeatedWords()
{
    qInfo() << "auto-snooze retires repeated untouched words";
    WordSession session;
    const qint64 t0 = 1000000;

    session.ingest(hits({QStringLiteral("boundary")}), t0);
    session.ingest(hits({QStringLiteral("boundary")}), t0 + 5000);
    check(session.autoSnooze(t0 + 6000, 3, 20000) == 0,
          QStringLiteral("two scans are not enough"));
    check(!session.active(t0 + 6000, 0).isEmpty(),
          QStringLiteral("word still visible after two scans"));

    session.ingest(hits({QStringLiteral("boundary")}), t0 + 10000);
    check(session.autoSnooze(t0 + 15000, 3, 20000) == 0,
          QStringLiteral("age threshold is respected"));
    check(session.autoSnooze(t0 + 21000, 3, 20000) == 1,
          QStringLiteral("third scan + 20s retires the word"));
    check(session.active(t0 + 21000, 0).isEmpty(),
          QStringLiteral("retired word leaves the bar"));
    check(session.all().size() == 1, QStringLiteral("retired word stays in the list"));
    check(session.snoozedCount() == 1, QStringLiteral("snoozed count is reported"));
}

/// Hovering or clicking a word protects it from auto-snooze forever.
void testInteractionProtectsWord()
{
    qInfo() << "inspecting a word protects it";
    WordSession session;
    const qint64 t0 = 2000000;

    session.ingest(hits({QStringLiteral("conformal")}), t0);
    session.touchInteraction(QStringLiteral("conformal"));
    for (int i = 1; i <= 10; ++i)
        session.ingest(hits({QStringLiteral("conformal")}), t0 + i * 1000);

    check(session.autoSnooze(t0 + 60000, 3, 20000) == 0,
          QStringLiteral("inspected word is never retired"));
    check(wordsOf(session.active(t0 + 60000, 0)) == QStringList{QStringLiteral("conformal")},
          QStringLiteral("inspected word stays in the bar"));
}

/// "Keep until handled" must survive a long quiet period.
void testKeepUntilHandled()
{
    qInfo() << "keep-until-handled never expires";
    WordSession session;
    const qint64 t0 = 3000000;
    session.ingest(hits({QStringLiteral("decimator")}), t0);

    const qint64 muchLater = t0 + 6 * 60 * 60 * 1000;
    check(wordsOf(session.active(muchLater, 0)) == QStringList{QStringLiteral("decimator")},
          QStringLiteral("word survives six hours with ttl 0"));
    check(session.active(muchLater, 90000).isEmpty(),
          QStringLiteral("with a ttl the word expires normally"));
}

/// Restoring a snoozed word brings it back and keeps it there.
void testRestoreSnoozedWord()
{
    qInfo() << "restore brings a word back";
    WordSession session;
    const qint64 t0 = 4000000;

    session.ingest(hits({QStringLiteral("mapper")}), t0);
    session.ingest(hits({QStringLiteral("mapper")}), t0 + 1000);
    session.ingest(hits({QStringLiteral("mapper")}), t0 + 2000);
    check(session.autoSnooze(t0 + 30000, 3, 20000) == 1, QStringLiteral("word got snoozed"));

    check(session.setSnoozed(QStringLiteral("mapper"), false), QStringLiteral("restore succeeds"));
    check(session.snoozedCount() == 0, QStringLiteral("snoozed count drops to zero"));

    // The app marks restored words as inspected, so they must not bounce back.
    session.touchInteraction(QStringLiteral("mapper"));
    check(session.autoSnooze(t0 + 90000, 3, 20000) == 0,
          QStringLiteral("restored word is not snoozed again"));
    check(wordsOf(session.active(t0 + 90000, 0)) == QStringList{QStringLiteral("mapper")},
          QStringLiteral("restored word is back on the bar"));
}

/// A new batch must not drop words from the previous one.
void testNewBatchesDoNotEvictOldWords()
{
    qInfo() << "old words are not evicted by new scans";
    WordSession session;
    const qint64 t0 = 5000000;

    session.ingest(hits({QStringLiteral("alpha"), QStringLiteral("beta")}), t0);
    session.ingest(hits({QStringLiteral("gamma")}), t0 + 1000);

    const QStringList bar = wordsOf(session.active(t0 + 1000, 0));
    check(bar == QStringList({QStringLiteral("alpha"), QStringLiteral("beta"),
                              QStringLiteral("gamma")}),
          QStringLiteral("all three words stay in the inbox"));
}

/// Counts accumulate across scans for the same word.
void testCountsAccumulate()
{
    qInfo() << "counts accumulate, newest first";
    WordSession session;
    session.ingest(hits({QStringLiteral("cache"), QStringLiteral("cache")}), 1000);
    session.ingest(hits({QStringLiteral("cache")}), 2000);
    session.ingest(hits({QStringLiteral("bool")}), 3000);

    const QVector<WordEntry> all = session.all();
    check(all.size() == 2, QStringLiteral("two distinct words"));
    check(all.last().word == QStringLiteral("cache") && all.last().count == 3,
          QStringLiteral("cache counted three times"));
    check(all.first().word == QStringLiteral("bool"),
          QStringLiteral("newest word is listed first"));
}

/// Nothing may reach the bar unless the offline dictionary can explain it.
void testDictionaryGate()
{
    qInfo() << "only words with a dictionary definition are listed";

    Dictionary dictionary;
    const QString path = QDir(appDataDir()).filePath(QStringLiteral("dict.bin"));
    QString error;
    if (!dictionary.load(path, &error)) {
        qWarning().noquote() << "  SKIP  dictionary not available:" << path << error;
        return;
    }
    qInfo().noquote() << "  dictionary:" << path << dictionary.size() << "entries";

    const QStringList listable = {
        QStringLiteral("boundary"),   QStringLiteral("attribute"),
        QStringLiteral("cofactor"),   QStringLiteral("mapper"),
        QStringLiteral("increment"),  QStringLiteral("hierarchy"),
        QStringLiteral("bisect"),     QStringLiteral("console"),
        QStringLiteral("running"),    // via the base form "run"
        QStringLiteral("analog"),     // only a "[计]" gloss, still useful
        QStringLiteral("affordable"), // same
        QStringLiteral("ars"),        // "[计]" technical gloss: in the dictionary
        QStringLiteral("aff"),
    };
    for (const QString& word : listable) {
        check(isListableWord(dictionary, word), QStringLiteral("keeps %1").arg(word));
    }

    const QStringList dropped = {
        QStringLiteral("clang-format"), // compound, no dictionary entry
        QStringLiteral("clang-tidy"),
        QStringLiteral("aui"),          // not in the dictionary at all
        QStringLiteral("nese"),         // abbreviation only
        QStringLiteral("clk"),
        QStringLiteral("hes"),
        QStringLiteral("hile"),         // only a "[人名]" transliteration
        QStringLiteral("abercrombie"),  // only place/name entries
        QStringLiteral("it's"),
    };
    for (const QString& word : dropped) {
        check(!isListableWord(dictionary, word), QStringLiteral("drops %1").arg(word));
    }

    // Whatever we keep must be able to fill a hover card.
    int emptyCards = 0;
    for (const QString& word : listable) {
        if (!dictionary.lookup(word).isValid())
            ++emptyCards;
    }
    check(emptyCards == 0, QStringLiteral("every listed word has a card"));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testAutoSnoozeRetiresRepeatedWords();
    testInteractionProtectsWord();
    testKeepUntilHandled();
    testRestoreSnoozedWord();
    testNewBatchesDoNotEvictOldWords();
    testCountsAccumulate();
    testDictionaryGate();

    if (g_failed == 0) {
        qInfo() << "\nall session checks passed";
        return 0;
    }
    qWarning() << "\n" << g_failed << "check(s) failed";
    return 1;
}
