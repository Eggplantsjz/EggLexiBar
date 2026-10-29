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