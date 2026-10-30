#pragma once

#include "models.h"

#include <QHash>
#include <QObject>
#include <QRect>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <functional>

struct AppSettings {
    bool alwaysOnTop = true;
    bool autoHide = true;
    bool autoStart = true;
    int dockSide = 0; // 0 none, 1 left, 2 right
    bool collapsed = false;
    bool pinned = false;
    int retentionDays = 30; // completed items older than N days are auto-purged on load
    QRect windowRect;
    QString screenName;
    QString draft;
    QDate draftDue;
    QHash<QString, bool> groupExpanded;
};

class DataStore : public QObject
{
    Q_OBJECT
public:
    explicit DataStore(QObject *parent = nullptr);

    bool load();
    bool flush();

    const QVector<TodoItem> &todos() const { return m_todos; }
    const AppSettings &settings() const { return m_settings; }
    bool wasFirstRun() const { return m_firstRun; }
    QString dataDir() const { return m_dataDir; }
    QString todosPath() const;
    QString settingsPath() const;
    QString archivePath() const;
    QStringList takeNotices();

    const TodoItem *findTodo(const QString &id) const;

    QString addTodo(const QString &text, QDate dueDate);
    bool updateText(const QString &id, const QString &text);
    bool updateDue(const QString &id, QDate dueDate);
    bool setCompleted(const QString &id, bool completed);
    bool setCompleted(const QString &id, bool completed, QDateTime *completedAt);
    bool removeTodo(const QString &id, TodoItem *removed = nullptr, int *removedIndex = nullptr);
    bool restoreTodo(const TodoItem &item, int index = -1);

    void setRetentionDays(int days);
    // Remove completed todos whose completedAt is strictly older than N days. Returns count removed.
    // Auto-purge on load uses settings.retentionDays (see applyRetentionOnLoad).
    int purgeCompletedOlderThan(int days);
    int purgeAllCompleted();
    // Append matching completed items to todos-archive.json, then remove from live store. Returns count.
    int archiveCompletedOlderThan(int days);
    int archiveAllCompleted();

    void setDraft(const QString &draft);
    void setDraftDue(QDate date);
    bool groupExpanded(GroupId id) const;
    void setGroupExpanded(GroupId id, bool expanded);

    void setAlwaysOnTop(bool on);
    void setAutoHide(bool on);
    void setAutoStartPreference(bool on);
    void setDockSide(int side);
    void setCollapsed(bool collapsed);
    void setPinned(bool pinned);
    void setWindowState(const QRect &rect, const QString &screenName, int dockSide,
                        bool collapsed, bool pinned);

signals:
    void todosChanged();
    void settingsChanged();
    void errorOccurred(const QString &message);

private slots:
    void saveTodosNow();
    void saveSettingsNow();

private:
    void scheduleTodosSave(int delayMs);
    void scheduleSettingsSave(int delayMs);
    bool writeJson(const QString &path, const QJsonObject &root, QString *error);
    void loadTodos();
    void loadSettings();
    void preserveCorruptFile(const QString &path);
    bool reloadTodoBackup(QString *warning);
    bool reloadSettingsBackup(QString *warning);
    void addNotice(const QString &text);
    QString resolveDataDir() const;
    void touchSettings();
    bool validateWindowRect(const QRect &rect) const;
    void applyRetentionOnLoad();
    int removeCompletedMatching(const std::function<bool(const TodoItem &)> &pred);
    int archiveThenRemoveMatching(const std::function<bool(const TodoItem &)> &pred);
    static bool isCompletedOlderThan(const TodoItem &item, int days, const QDateTime &now);

    QVector<TodoItem> m_todos;
    AppSettings m_settings;
    QString m_dataDir;
    QStringList m_notices;
    QString m_lastError;
    QTimer m_todoSaveTimer;
    QTimer m_settingsSaveTimer;
    bool m_loaded = false;
    bool m_firstRun = false;
    bool m_todosDirty = false;
    bool m_settingsDirty = false;
};
