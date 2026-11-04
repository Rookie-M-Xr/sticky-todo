#pragma once

#include "models.h"

#include <QHash>
#include <QMap>
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
    bool showDailyReport = true;
    bool showWeeklyReport = true;
    bool showMonthlyReport = true;
    QRect windowRect;
    QString screenName;
    QString draft;
    QDate draftDue;
    // Empty = off. Normalized form e.g. "Ctrl+Alt+T".
    QString globalHotkey;
    // True after the first "still running in tray" balloon; never balloon again.
    bool trayHideHintShown = false;
    // Default focus length for newly started clocks (TR #31). Running clocks keep
    // the deadline they captured at start.
    int pomodoroMinutes = kPomodoroDefaultMinutes;
    // TR #35: opt-in cumulative pomodoro stats. Default off — running a clock
    // must not write stats until the user enables this.
    bool pomodoroStatsEnabled = false;
    QHash<QString, bool> groupExpanded;
};

struct PomodoroStats {
    int completedCount = 0;
    int focusedMinutes = 0;
};

struct ArchivedEntry {
    int index = -1; // position in todos-archive.json "archived" array
    QString archivedAt;
    TodoItem item;
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
    // #107: primary todos.json failed to parse. In-memory list is empty and
    // writes to the primary file are blocked until restoreTodosFromBackup().
    bool todosLoadFailed() const { return m_todosLoadFailed; }
    bool todoBackupAvailable() const { return m_todoBackupAvailable; }
    QString todoBackupPath() const;
    QString lastTodoLoadError() const { return m_todoLoadError; }
    // Copy the sibling todos.json.bak over the primary and reload.
    // Returns false if no readable backup was offered, or the copy/reload failed.
    bool restoreTodosFromBackup();
    QString settingsPath() const;
    QString archivePath() const;
    QString remindersPath() const;
    QString pomodoroStatsPath() const;
    const PomodoroStats &pomodoroStats() const { return m_pomodoroStats; }
    QStringList takeNotices();

    const TodoItem *findTodo(const QString &id) const;

    QString addTodo(const QString &text, QDate dueDate);
    bool updateText(const QString &id, const QString &text);
    bool updateDue(const QString &id, QDate dueDate);
    bool setCompleted(const QString &id, bool completed);
    bool setCompleted(const QString &id, bool completed, QDateTime *completedAt);
    // TR #60: persist pin on the TodoItem in todos.json (not a second store).
    // Pin time is kept on re-pin so newest-first order stays stable.
    bool setTodoPinned(const QString &id, bool pinned);
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
    // Move one live item (complete or not) into todos-archive.json. Same file format and restore path.
    bool archiveTodo(const QString &id);

    // Read todos-archive.json (empty if missing/unreadable). Indices match current file order.
    QVector<ArchivedEntry> listArchived() const;
    // Re-import archive entries by index back into the main list (move: removed from archive).
    // Conflict-id strategy (documented here and on TR #6):
    //   - If archived item.id is free in the live list → keep that id.
    //   - If it already exists → mint a new UUID for the restored copy; never overwrite
    //     or skip. All other fields (text/due/completed/timestamps) stay intact.
    int importArchived(const QVector<int> &indices);
    int importAllArchived();

    const QVector<ReminderItem> &reminders() const { return m_reminders; }
    const ReminderItem *findReminder(const QString &id) const;
    QString addReminder(const QString &text, int intervalMinutes,
                        int activeStartMinutes = 0,
                        int activeEndMinutes = kReminderDayMinutes);
    bool setReminderEnabled(const QString &id, bool enabled);
    bool removeReminder(const QString &id);
    bool markReminderFired(const QString &id, const QDateTime &when = QDateTime());

    QString dailyReportsPath() const;
    QString weeklyReportsPath() const;
    QString monthlyReportsPath() const;
    // Ensure completed todos in the period have snapshot lines (editable copies; never write back).
    int syncDailyFromCompleted(QDate day = QDate());
    int syncWeeklyFromCompleted(QDate day = QDate());
    int syncMonthlyFromCompleted(QDate day = QDate());
    int syncReportFromCompleted(ReportKind kind, QDate day = QDate());
    QVector<DailySnapshot> dailySnapshots(QDate day = QDate()) const;
    QVector<DailySnapshot> weeklySnapshots(QDate day = QDate()) const;
    QVector<DailySnapshot> monthlySnapshots(QDate day = QDate()) const;
    QVector<DailySnapshot> reportSnapshots(ReportKind kind, QDate day = QDate()) const;
    bool updateDailySnapshotText(const QString &snapshotId, const QString &text);
    bool updateReportSnapshotText(ReportKind kind, const QString &snapshotId, const QString &text);
    QString formatDailyPlainText(QDate day = QDate()) const;
    QString formatWeeklyPlainText(QDate day = QDate()) const;
    QString formatMonthlyPlainText(QDate day = QDate()) const;
    QString formatReportPlainText(ReportKind kind, QDate day = QDate()) const;

    void setShowDailyReport(bool on);
    void setShowWeeklyReport(bool on);
    void setShowMonthlyReport(bool on);

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
    void setGlobalHotkey(const QString &hotkey);
    void setTrayHideHintShown(bool shown);
    void setPomodoroMinutes(int minutes);
    void setPomodoroStatsEnabled(bool on);
    // No-op when stats are disabled. Complete: +1 count and the session minutes.
    // Interrupt: add rounded focused minutes so far; count unchanged.
    void recordPomodoroCompleted(int sessionMinutes);
    void recordPomodoroInterrupted(int focusedSeconds);
    // Both counters to 0 and persist. Does not change pomodoroStatsEnabled.
    void clearPomodoroStats();
    void setWindowState(const QRect &rect, const QString &screenName, int dockSide,
                        bool collapsed, bool pinned);

signals:
    void todosChanged();
    void settingsChanged();
    void remindersChanged();
    void pomodoroStatsChanged();
    void errorOccurred(const QString &message);
    void dailyReportsChanged();
    void weeklyReportsChanged();
    void monthlyReportsChanged();

private slots:
    void saveTodosNow();
    void saveSettingsNow();

private:
    void scheduleTodosSave(int delayMs);
    void scheduleSettingsSave(int delayMs);
    bool writeJson(const QString &path, const QJsonObject &root, QString *error);
    void loadTodos();
    void loadSettings();
    void loadReminders();
    void scheduleRemindersSave(int delayMs);
    void saveRemindersNow();
    bool reloadReminderBackup(QString *warning);
    void loadPomodoroStats();
    void schedulePomodoroStatsSave(int delayMs);
    void savePomodoroStatsNow();
    bool reloadPomodoroStatsBackup(QString *warning);
    void preserveCorruptFile(const QString &path);
    bool reloadSettingsBackup(QString *warning);
    bool rejectTodosWrite();
    void addNotice(const QString &text);
    QString resolveDataDir() const;
    void touchSettings();
    bool validateWindowRect(const QRect &rect) const;
    void applyRetentionOnLoad();

    void scheduleDailySave(int delayMs);
    void scheduleWeeklySave(int delayMs);
    void scheduleMonthlySave(int delayMs);
    void loadDailyReports();
    void loadWeeklyReports();
    void loadMonthlyReports();
    void saveDailyNow();
    void saveWeeklyNow();
    void saveMonthlyNow();
    void loadPeriodReportsFile(const QString &path, const QString &bucketKey,
                               QMap<QString, QVector<DailySnapshot>> *map, const QString &label);
    void savePeriodReportsFile(const QString &path, const QString &bucketKey,
                               const QMap<QString, QVector<DailySnapshot>> &map, bool *dirty,
                               QTimer *timer, const QString &failLabel);
    int syncRangeFromCompleted(QMap<QString, QVector<DailySnapshot>> *map, const QString &key,
                               QDate rangeStart, QDate rangeEnd, bool *dirtyFlag, QTimer *timer,
                               const std::function<void()> &emitChanged);
    int removeCompletedMatching(const std::function<bool(const TodoItem &)> &pred);
    int archiveThenRemoveMatching(const std::function<bool(const TodoItem &)> &pred);
    static bool isCompletedOlderThan(const TodoItem &item, int days, const QDateTime &now);
    bool readArchiveFile(QJsonArray *archived, QString *error) const;
    bool writeArchiveFile(const QJsonArray &archived, QString *error);
    int importArchivedEntries(const QVector<int> &indices);

    QVector<TodoItem> m_todos;
    QVector<ReminderItem> m_reminders;
    PomodoroStats m_pomodoroStats;
    AppSettings m_settings;
    QString m_dataDir;
    QStringList m_notices;
    QString m_lastError;
    QTimer m_todoSaveTimer;
    QTimer m_settingsSaveTimer;
    QTimer m_reminderSaveTimer;
    QTimer m_pomodoroStatsSaveTimer;
    bool m_loaded = false;
    bool m_firstRun = false;
    bool m_todosLoadFailed = false;
    bool m_todoBackupAvailable = false;
    QString m_todoLoadError;
    bool m_todosDirty = false;
    bool m_settingsDirty = false;
    bool m_remindersDirty = false;
    bool m_pomodoroStatsDirty = false;
    QMap<QString, QVector<DailySnapshot>> m_dailyByDate; // key = yyyy-MM-dd
    QMap<QString, QVector<DailySnapshot>> m_weeklyByKey; // key = yyyy-Www
    QMap<QString, QVector<DailySnapshot>> m_monthlyByKey; // key = yyyy-MM
    QTimer m_dailySaveTimer;
    QTimer m_weeklySaveTimer;
    QTimer m_monthlySaveTimer;
    bool m_dailyDirty = false;
    bool m_weeklyDirty = false;
    bool m_monthlyDirty = false;
};

