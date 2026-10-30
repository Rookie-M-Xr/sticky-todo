#pragma once

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
    explicit DailyReportPage(DataStore *store, QWidget *parent = nullptr);
    void refresh();

signals:
    void messageRequested(const QString &text);

private:
    void buildUi();
    void copyPlainText();
    void onItemChanged(QListWidgetItem *item);

    DataStore *m_store = nullptr;
    QLabel *m_hintLabel = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_copyButton = nullptr;
    bool m_syncing = false;
};
