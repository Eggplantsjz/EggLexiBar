#include "DetailsPanel.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTime>
#include <QVBoxLayout>

DetailsPanel::DetailsPanel(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("WordLens · 生词列表"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowFlag(Qt::Tool, true);
    resize(500, 460);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(10);

    auto* topRow = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("搜索单词…"));
    m_search->setClearButtonEnabled(true);
    topRow->addWidget(m_search, 1);

    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QStringLiteral("color:#98A1B4;"));
    topRow->addWidget(m_summary, 0);
    root->addLayout(topRow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({QStringLiteral("单词"), QStringLiteral("次数"),
                                        QStringLiteral("状态"), QStringLiteral("首次出现")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    root->addWidget(m_table, 1);

    auto* buttonRow = new QHBoxLayout;
    auto* copyButton = new QPushButton(QStringLiteral("复制选中"), this);
    auto* copyAllButton = new QPushButton(QStringLiteral("复制全部"), this);
    auto* knownButton = new QPushButton(QStringLiteral("标记已掌握"), this);
    auto* blockButton = new QPushButton(QStringLiteral("不再显示"), this);
    auto* restoreButton = new QPushButton(QStringLiteral("恢复显示"), this);
    restoreButton->setToolTip(QStringLiteral("把自动收起的词放回悬浮栏"));
    auto* clearButton = new QPushButton(QStringLiteral("清空"), this);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), this);
    buttonRow->addWidget(copyButton);
    buttonRow->addWidget(copyAllButton);
    buttonRow->addWidget(knownButton);
    buttonRow->addWidget(blockButton);
    buttonRow->addWidget(restoreButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(clearButton);
    buttonRow->addWidget(closeButton);
    root->addLayout(buttonRow);

    m_feedback = new QLabel(this);
    m_feedback->setStyleSheet(QStringLiteral("color:#9CC1FF;"));
    m_feedback->setFixedHeight(16);
    root->addWidget(m_feedback);

    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);
    connect(copyButton, &QPushButton::clicked, this, [this] {
        copyWords(selectedWords());
    });
    connect(copyAllButton, &QPushButton::clicked, this, [this] {
        QStringList words;
        for (const WordEntry& entry : m_entries)
            words << entry.word;
        copyWords(words);
    });
    connect(knownButton, &QPushButton::clicked, this, [this] {
        const QStringList words = selectedWords();
        if (words.isEmpty())
            return;
        emit markKnownRequested(words);
        m_feedback->setText(QStringLiteral("已标记 %1 个词为已掌握").arg(words.size()));
    });
    connect(blockButton, &QPushButton::clicked, this, [this] {
        const QStringList words = selectedWords();
        if (words.isEmpty())
            return;
        emit blockRequested(words);
        m_feedback->setText(QStringLiteral("已隐藏 %1 个词").arg(words.size()));
    });
    connect(restoreButton, &QPushButton::clicked, this, [this] {
        const QStringList words = selectedWords();
        if (words.isEmpty()) {
            m_feedback->setText(QStringLiteral("请先选择要恢复的词"));
            return;
        }
        emit restoreRequested(words);
        m_feedback->setText(QStringLiteral("已恢复 %1 个词").arg(words.size()));
    });
    connect(clearButton, &QPushButton::clicked, this, [this] {
        emit clearRequested();
        m_feedback->setText(QStringLiteral("会话列表已清空"));
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* item = m_table->item(row, 0))
            copyWords({item->data(Qt::UserRole).toString()});
    });
}

void DetailsPanel::setEntries(const QVector<WordEntry>& entries)
{
    m_entries = entries;
    rebuild();
}

void DetailsPanel::rebuild()
{
    const QString filter = m_search->text().trimmed().toLower();

    QStringList selected;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows()) {
        if (auto* item = m_table->item(index.row(), 0))
            selected << item->data(Qt::UserRole).toString();
    }

    int totalHits = 0;
    int snoozed = 0;
    for (const WordEntry& entry : m_entries) {
        totalHits += entry.count;
        if (entry.snoozed)
            ++snoozed;
    }

    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    for (const WordEntry& entry : m_entries) {
        if (!filter.isEmpty() && !entry.word.contains(filter))
            continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto* wordItem = new QTableWidgetItem(entry.word);
        wordItem->setData(Qt::UserRole, entry.word);
        auto* countItem = new QTableWidgetItem;
        countItem->setData(Qt::DisplayRole, entry.count);
        auto* statusItem =
            new QTableWidgetItem(entry.snoozed ? QStringLiteral("疑似已掌握")
                                               : QStringLiteral("待处理"));
        statusItem->setForeground(entry.snoozed ? QColor(0x98, 0xA1, 0xB4)
                                                : QColor(0x4A, 0xDE, 0x80));
        auto* timeItem = new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(entry.firstSeen)
                .toString(QStringLiteral("HH:mm:ss")));

        m_table->setItem(row, 0, wordItem);
        m_table->setItem(row, 1, countItem);
        m_table->setItem(row, 2, statusItem);
        m_table->setItem(row, 3, timeItem);
    }
    m_table->setSortingEnabled(true);

    m_summary->setText(QStringLiteral("%1 待处理 · %2 已收起 · %3 次")
                           .arg(m_entries.size() - snoozed)
                           .arg(snoozed)
                           .arg(totalHits));

    if (!selected.isEmpty()) {
        m_table->clearSelection();
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (auto* item = m_table->item(row, 0)) {
                if (selected.contains(item->data(Qt::UserRole).toString()))
                    m_table->selectRow(row);
            }
        }
    }
}

QStringList DetailsPanel::selectedWords() const
{
    QStringList words;
    const auto rows = m_table->selectionModel()->selectedRows();
    for (const QModelIndex& index : rows) {
        if (auto* item = m_table->item(index.row(), 0)) {
            const QString word = item->data(Qt::UserRole).toString();
            if (!word.isEmpty())
                words << word;
        }
    }
    words.removeDuplicates();
    return words;
}

void DetailsPanel::copyWords(const QStringList& words)
{
    if (words.isEmpty()) {
        m_feedback->setText(QStringLiteral("没有可复制的单词"));
        return;
    }
    QGuiApplication::clipboard()->setText(words.join(QLatin1Char('\n')));
    m_feedback->setText(QStringLiteral("已复制 %1 个单词").arg(words.size()));
}
