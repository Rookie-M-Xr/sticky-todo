#pragma once

#include <QWidget>

class DataStore;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QVBoxLayout;
class SwitchButton;
class TextAction;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(DataStore *store, QWidget *parent = nullptr);
    void refresh();

signals:
    void autoStartRequested(bool enabled);
    void dockRequested(int side);
    void collapseRequested();
    void resetWindowRequested();
    void openDataFolderRequested();
    void messageRequested(const QString &text);

private:
    void buildUi();
    QWidget *buildGeneralCard();
    QWidget *buildReportsCard();
    QWidget *buildWindowCard();
    QWidget *buildDataCard();
    QWidget *buildAboutCard();

    DataStore *m_store = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    SwitchButton *m_topSwitch = nullptr;
    SwitchButton *m_autoHideSwitch = nullptr;
    SwitchButton *m_autoStartSwitch = nullptr;
    SwitchButton *m_dailyReportSwitch = nullptr;
    SwitchButton *m_weeklyReportSwitch = nullptr;
    SwitchButton *m_monthlyReportSwitch = nullptr;
    QPushButton *m_dockNone = nullptr;
    QPushButton *m_dockLeft = nullptr;
    QPushButton *m_dockRight = nullptr;
    QLabel *m_pathLabel = nullptr;
    TextAction *m_collapseAction = nullptr;
    QSpinBox *m_retentionSpin = nullptr;
    TextAction *m_purgeExpiredAction = nullptr;
    TextAction *m_purgeAllAction = nullptr;
    TextAction *m_archiveExpiredAction = nullptr;
    bool m_syncing = false;
};
