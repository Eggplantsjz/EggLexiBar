#include "WordStore.h"

#include "Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
const char* kConnection = "wordlens_store";
}

WordStore::FilterState WordStore::filterState() const
{
    FilterState state;
    if (!isOpen())
        return state;

    QSqlDatabase db = QSqlDatabase::database(kConnection);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT word, round, hidden_until FROM filter_state")))
        return state;
    while (q.next()) {
        state.insert(q.value(0).toString(),
                     qMakePair(q.value(1).toInt(), q.value(2).toLongLong()));
    }
    return state;
}

bool WordStore::exec(const QString& sql, QString* error) const
{
    QSqlDatabase db = QSqlDatabase::database(kConnection);
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    return true;
}

void WordStore::ensureSchema()
{
    const QSqlDatabase db = QSqlDatabase::database(kConnection);
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS known ("
        " word TEXT PRIMARY KEY COLLATE NOCASE)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS blocked ("
        " word TEXT PRIMARY KEY COLLATE NOCASE)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS filter_state ("
        " word TEXT PRIMARY KEY COLLATE NOCASE,"
        " round INTEGER NOT NULL DEFAULT 0,"
        " hidden_until INTEGER NOT NULL DEFAULT 0)"));
    // Per-day pet counters. A row exists for every word that showed up on the
    // bar that day; the flags are turned on when the pet eats it, the user marks
    // it known or the memory curve retires it.
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS daily_word ("
        " day TEXT NOT NULL,"
        " word TEXT NOT NULL COLLATE NOCASE,"
        " eaten INTEGER NOT NULL DEFAULT 0,"
        " known INTEGER NOT NULL DEFAULT 0,"
        " retired INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY (day, word))"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS daily_word_day ON daily_word(day)"));
    // Keep roughly a quarter of history; the pet only reports on today.
    q.exec(QStringLiteral(
        "DELETE FROM daily_word WHERE day < date('now', '-120 days')"));
}

bool WordStore::open(const QString& dbPath, QString* error)
{
    if (QSqlDatabase::contains(kConnection))
        QSqlDatabase::removeDatabase(kConnection);

    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    m_dbPath = dbPath;

    QSqlDatabase db =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnection);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }
    m_connection = kConnection;
    ensureSchema();
    return true;
}

void WordStore::close()
{
    if (QSqlDatabase::contains(kConnection))
        QSqlDatabase::removeDatabase(kConnection);
    m_connection.clear();
}

bool WordStore::isOpen() const
{
    return !m_connection.isEmpty()
        && QSqlDatabase::contains(m_connection)
        && QSqlDatabase::database(m_connection).isOpen();
}

bool WordStore::isEmpty() const
{
    if (!isOpen())
        return true;
    const QSqlDatabase db = QSqlDatabase::database(m_connection);
    for (const char* table : {"known", "blocked", "filter_state"}) {
        QSqlQuery q(db);
        q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(
            QString::fromLatin1(table)));
        if (q.next() && q.value(0).toInt() > 0)
            return false;
    }
    return true;
}

QStringList WordStore::knownWords() const
{
    QStringList out;
    if (!isOpen())
        return out;
    const QSqlDatabase db = QSqlDatabase::database(m_connection);
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT word FROM known")))
        while (q.next())
            out.append(q.value(0).toString());
    return out;
}

QStringList WordStore::blockedWords() const
{
    QStringList out;
    if (!isOpen())
        return out;
    const QSqlDatabase db = QSqlDatabase::database(m_connection);
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT word FROM blocked")))
        while (q.next())
            out.append(q.value(0).toString());
    return out;
}

void WordStore::addKnown(const QStringList& words)
{
    if (!isOpen())
        return;
    const QSqlDatabase db = QSqlDatabase::database(m_connection);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO known (word) VALUES (?)"));
    for (const QString& w : words) {
        const QString word = w.trimmed().toLower();
        if (word.isEmpty())
            continue;
        q.addBindValue(word);
        q.exec();
    }
}

void WordStore::addBlocked(const QStringList& words)
{
    if (!isOpen())
        return;
    const QSqlDatabase db = QSqlDatabase::database(m_connection);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO blocked (word) VALUES (?)"));
    for (const QString& w : words) {
        const QString word = w.trimmed().toLower();
        if (word.isEmpty())
            continue;
        q.addBindValue(word);
        q.exec();
    }
}

void WordStore::saveFilterState(const FilterState& state)
{
    if (!isOpen())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    db.transaction();
    QSqlQuery del(db);
    del.exec(QStringLiteral("DELETE FROM filter_state"));
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral(
        "INSERT INTO filter_state (word, round, hidden_until) VALUES (?, ?, ?)"));
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        ins.addBindValue(it.key());
        ins.addBindValue(it.value().first);
        ins.addBindValue(it.value().second);
        ins.exec();
    }
    db.commit();
}

void WordStore::recordSeen(const QStringList& words, const QString& day)
{
    if (!isOpen() || words.isEmpty() || day.isEmpty())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO daily_word (day, word) VALUES (?, ?)"));
    for (const QString& word : words) {
        const QString w = word.trimmed().toLower();
        if (w.isEmpty())
            continue;
        q.addBindValue(day);
        q.addBindValue(w);
        q.exec();
    }
    db.commit();
}

namespace {

/// Turns one flag column on for every word, creating the day row if the word
/// was not recorded as seen yet (e.g. it was marked known from the list).
void markDailyFlag(QSqlDatabase& db, const QStringList& words,
                   const QString& day, const QString& column)
{
    if (!db.isOpen() || words.isEmpty() || day.isEmpty())
        return;

    db.transaction();
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO daily_word (day, word) VALUES (?, ?)"));
    QSqlQuery update(db);
    update.prepare(QStringLiteral(
                       "UPDATE daily_word SET %1 = 1 WHERE day = ? AND word = ?")
                       .arg(column));
    for (const QString& word : words) {
        const QString w = word.trimmed().toLower();
        if (w.isEmpty())
            continue;
        insert.addBindValue(day);
        insert.addBindValue(w);
        insert.exec();
        update.addBindValue(day);
        update.addBindValue(w);
        update.exec();
    }
    db.commit();
}

} // namespace

void WordStore::recordEaten(const QStringList& words, const QString& day)
{
    if (!isOpen())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    markDailyFlag(db, words, day, QStringLiteral("eaten"));
}

void WordStore::recordKnown(const QStringList& words, const QString& day)
{
    if (!isOpen())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    markDailyFlag(db, words, day, QStringLiteral("known"));
}

void WordStore::recordRetired(const QStringList& words, const QString& day)
{
    if (!isOpen())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    markDailyFlag(db, words, day, QStringLiteral("retired"));
}

WordStore::DailyStats WordStore::dailyStats(const QString& day) const
{
    DailyStats stats;
    if (!isOpen() || day.isEmpty())
        return stats;

    const QSqlDatabase db = QSqlDatabase::database(m_connection);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*), SUM(eaten),"
        " SUM(CASE WHEN eaten = 1 OR known = 1 OR retired = 1 THEN 1 ELSE 0 END)"
        " FROM daily_word WHERE day = ?"));
    q.addBindValue(day);
    if (q.exec() && q.next()) {
        stats.seen = q.value(0).toInt();
        stats.eaten = q.value(1).toInt();
        stats.familiar = q.value(2).toInt();
    }
    return stats;
}

void WordStore::migrateFromIni(const AppConfig& cfg)
{
    if (isEmpty() && isOpen()) {
        addKnown(cfg.knownWords);
        addBlocked(cfg.blockedWords);

        FilterState state;
        for (const QString& entry : cfg.autoFilterState) {
            const QStringList parts = entry.split(QLatin1Char('|'));
            if (parts.size() != 3)
                continue;
            bool okCount = false;
            bool okUntil = false;
            const int count = parts.at(1).toInt(&okCount);
            const qint64 until = parts.at(2).toLongLong(&okUntil);
            if (!okCount || !okUntil || parts.at(0).isEmpty())
                continue;
            state.insert(parts.at(0).toLower(), qMakePair(count, until));
        }
        saveFilterState(state);
    }
}