#pragma once

#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>

struct AppConfig;

/// Persistence for the user vocabulary and the memory-curve state, backed by a
/// local SQLite database instead of the flat ini used for scalar config.
///
/// Rationale: the vocabulary (known / blocked words) and the auto-filter curve
/// table can grow to tens of thousands of rows. SQLite gives us indexed lookups,
/// incremental WAL writes, and transactional consistency — properties a single
/// rewritten ini line cannot provide.
class WordStore
{
public:
    /// (word → (filter round, hidden-until timestamp)) — mirrors
    /// WordSession::PersistFilter so the two layers can exchange it directly.
    using FilterState = QHash<QString, QPair<int, qint64>>;

    /// Opens (and creates if needed) the database at \a dbPath. Enables WAL +
    /// NORMAL synchronous for fast, crash-safe writes.
    bool open(const QString& dbPath, QString* error = nullptr);
    void close();
    bool isOpen() const;

    bool isEmpty() const;

    QStringList knownWords() const;
    QStringList blockedWords() const;
    void addKnown(const QStringList& words);
    void addBlocked(const QStringList& words);

    FilterState filterState() const;
    void saveFilterState(const FilterState& state);

    /// Today's pet stats: how many distinct words were seen, eaten by the pet,
    /// and are considered familiar (eaten / marked known / retired by the
    /// memory curve). All three are per-calendar-day counters.
    struct DailyStats
    {
        int seen = 0;
        int eaten = 0;
        int familiar = 0;
    };

    void recordSeen(const QStringList& words, const QString& day);
    void recordEaten(const QStringList& words, const QString& day);
    void recordKnown(const QStringList& words, const QString& day);
    void recordRetired(const QStringList& words, const QString& day);
    DailyStats dailyStats(const QString& day) const;

    /// One-time import of legacy ini-backed vocabulary into the database. Only
    /// transfers fields that are non-empty in \a cfg (already-lowercased lists).
    void migrateFromIni(const AppConfig& cfg);

private:
    void ensureSchema();
    bool exec(const QString& sql, QString* error = nullptr) const;

    QString m_connection;
    QString m_dbPath;
};