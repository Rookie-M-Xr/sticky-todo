#include "dailyreportpage.h"

#include "datastore.h"
#include "theme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDate>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

DailyReportPage::DailyReportPage(DataStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(false);
    buildUi();
}

void DailyReportPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 4, 14, 10);
    root->setSpacing(8);

    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(8);
    m_hintLabel = new QLabel(QStringLiteral("今日完成快照（改文字不回写待办）"), this);
    m_hintLabel->setFont(Theme::uiFont(11));
    m_hintLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Muted.name()));
    m_hintLabel->setWordWrap(true);
    top->addWidget(m_hintLabel, 1);

    m_copyButton = new QPushButton(QStringLiteral("一键复制"), this);
    m_copyButton->setCursor(Qt::PointingHandCursor);
    m_copyButton->setFixedHeight(28);
    m_copyButton->setFont(Theme::uiFont(12));
    m_copyButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 0 12px;"
        "}"
        "QPushButton:hover { background: %4; }"
        "QPushButton:pressed { background: %5; }")
                                    .arg(Theme::AccentSoft.name(), Theme::AccentDark.name(),
                                         QColor(180, 190, 150).name(), QColor(210, 218, 180).name(),
                                         QColor(200, 208, 170).name()));
    top->addWidget(m_copyButton, 0, Qt::AlignTop);
    root->addLayout(top);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSpacing(4);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                            | QAbstractItemView::SelectedClicked);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; outline: none; color: %1; }"
        "QListWidget::item {"
        "  background: rgba(255,255,255,54); border: 1px solid rgba(209,194,158,100);"
        "  border-radius: 10px; padding: 8px 10px; margin: 1px 0;"
        "}"
        "QListWidget::item:selected { background: rgba(220,226,194,120); }"
        "QScrollBar:vertical { width: 8px; background: transparent; }"
        "QScrollBar::handle:vertical { background: rgba(180,170,140,120); border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
                              .arg(Theme::Ink.name()));
    m_list->setFont(Theme::uiFont(13));
    root->addWidget(m_list, 1);

    connect(m_copyButton, &QPushButton::clicked, this, &DailyReportPage::copyPlainText);
    connect(m_list, &QListWidget::itemChanged, this, &DailyReportPage::onItemChanged);
}

void DailyReportPage::refresh()
{
    if (!m_store)
        return;
    m_store->syncDailyFromCompleted(QDate::currentDate());
    m_syncing = true;
    m_list->clear();
    const QVector<DailySnapshot> snaps = m_store->dailySnapshots(QDate::currentDate());
    if (snaps.isEmpty()) {
        auto *empty = new QListWidgetItem(QStringLiteral("今日还没有完成的待办"), m_list);
        empty->setFlags(empty->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsSelectable);
        empty->setForeground(Theme::Muted);
    } else {
        for (const DailySnapshot &snap : snaps) {
            auto *item = new QListWidgetItem(snap.text, m_list);
            item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            item->setData(Qt::UserRole, snap.id);
            item->setToolTip(QStringLiteral("双击编辑；不回写主列表待办"));
        }
    }
    m_syncing = false;
}

void DailyReportPage::onItemChanged(QListWidgetItem *item)
{
    if (m_syncing || !m_store || !item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return;
    m_store->updateDailySnapshotText(id, item->text());
}

void DailyReportPage::copyPlainText()
{
    if (!m_store)
        return;
    const QString text = m_store->formatDailyPlainText(QDate::currentDate());
    if (QClipboard *clip = QApplication::clipboard()) {
        clip->setText(text);
        emit messageRequested(QStringLiteral("已复制今日日报"));
    }
}
