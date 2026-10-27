#pragma once

#include "WordSession.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class QTableWidget;

/// Window that lists every word collected in the current session.
class DetailsPanel : public QDialog
{
    Q_OBJECT
public:
    explicit DetailsPanel(QWidget* parent = nullptr);

    void setEntries(const QVector<WordEntry>& entries);

signals:
    void clearRequested();
    void markKnownRequested(const QStringList& words);
    void blockRequested(const QStringList& words);
    void restoreRequested(const QStringList& words);

private:
    void rebuild();
    QStringList selectedWords() const;
    void copyWords(const QStringList& words);

    QVector<WordEntry> m_entries;
    QTableWidget* m_table = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_feedback = nullptr;
};
