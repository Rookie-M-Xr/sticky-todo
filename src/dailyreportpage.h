#pragma once

#include "models.h"

#include <QWidget>

class DataStore;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QVBoxLayout;

class DailyReportPage : public QWidget
{
    Q_OBJECT

public:
    explicit DailyReportPage(DataStore *store, ReportKind kind = ReportKind::Daily,
                             QWidget *parent = nullptr);
    void refresh();
    ReportKind kind() const { return m_kind; }

signals:
    void messageRequested(const QString &text);

private:
    void buildUi();
    void copyPlainText();
    void onItemChanged(QListWidgetItem *item);

    DataStore *m_store = nullptr;
    ReportKind m_kind = ReportKind::Daily;
    QLabel *m_hintLabel = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_copyButton = nullptr;
    bool m_syncing = false;
};
