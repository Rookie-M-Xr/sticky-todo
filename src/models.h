#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct TodoItem {
    QString id;
    QString text;
    QDateTime createdAt;
    QDate dueDate;
    bool completed = false;
    QDateTime completedAt;
    QDateTime updatedAt;

    bool isOverdue(QDate today) const {
        return !completed && dueDate.isValid() && dueDate < today;
    }

    QJsonObject toJson() const;
    static TodoItem fromJson(const QJsonObject &object, bool *ok = nullptr);
};

struct DailySnapshot {
    QString id;
    QString text;
    QString sourceTodoId; // completed todo id this line was snapped from; empty if none
    QDate date;
    QDateTime createdAt;
    QDateTime updatedAt;

    QJsonObject toJson() const;
    static DailySnapshot fromJson(const QJsonObject &object, bool *ok = nullptr);
};

// Interval reminders (TR #19). Own file reminders.json — never a TodoItem field.
struct ReminderItem {
    QString id;
    QString text;
    int intervalMinutes = 60;
    bool enabled = true;
    QDateTime lastFiredAt;
    QDateTime createdAt;
    QDateTime updatedAt;

    QJsonObject toJson() const;
    static ReminderItem fromJson(const QJsonObject &object, bool *ok = nullptr);
};

constexpr int kReminderMinMinutes = 1;
constexpr int kReminderMaxMinutes = 7 * 24 * 60;
constexpr int kReminderMaxText = 200;
constexpr int kReminderMaxCount = 20;

int clampReminderInterval(int minutes);
QString formatReminderInterval(int minutes);
QDateTime reminderAnchor(const ReminderItem &item);
QDateTime reminderNextDue(const ReminderItem &item);
bool reminderIsDue(const ReminderItem &item, const QDateTime &now = QDateTime());

enum class ReportKind {
    Daily,
    Weekly,
    Monthly
};

QString reportWeekKey(QDate day);
QString reportMonthKey(QDate day);
QDate reportWeekStart(QDate day);
QDate reportWeekEnd(QDate day);

enum class GroupId {

    Overdue,
    Today,
    Tomorrow,
    Next7,
    Later,
    NoDate,
    PastCompleted
};

struct GroupInfo {
    GroupId id;
    QString key;
    QString title;
};

const QVector<GroupInfo> &allGroups();
QString groupKey(GroupId id);
QString groupTitle(GroupId id);
GroupId groupFor(const TodoItem &item, QDate today);
int groupOrder(GroupId id);

struct TodoGroup {
    GroupId id = GroupId::Today;
    QVector<TodoItem> items;
    int pendingCount = 0;
};

QVector<TodoGroup> buildGroups(const QVector<TodoItem> &items, QDate today,
                               const QString &search = QString());

QString weekdayShort(QDate date);
QString dateHeadline(QDate date);
QString shortDueLabel(QDate due, QDate today);
QString fullDueLabel(QDate due, QDate today);
QString relativeDueHint(QDate due, QDate today);

// Session-only focus clock (TR #18). Default minutes live in settings (TR #31).
// Optional cumulative stats (TR #35) live in pomodoro_stats.json — never todos.json.
constexpr int kPomodoroMinMinutes = 1;
constexpr int kPomodoroMaxMinutes = 120;
constexpr int kPomodoroDefaultMinutes = 25;
constexpr int kPomodoroDefaultSeconds = kPomodoroDefaultMinutes * 60;

int clampPomodoroMinutes(int minutes);
int pomodoroSecondsForMinutes(int minutes);
QString formatPomodoroStartHint(int minutes);
QString formatPomodoroClock(int remainingSeconds);
int pomodoroRemainingSeconds(const QDateTime &deadline,
                             const QDateTime &now = QDateTime());
// Same id toggles off (empty). A different id silently replaces the old one.
QString pomodoroAfterToggle(const QString &currentId, const QString &requestedId);
// Complete only when remaining has hit zero. Manual mid-stop (remaining > 0) does not.
bool pomodoroCountsAsComplete(int remainingSeconds);
int pomodoroElapsedSeconds(int sessionMinutes, int remainingSeconds);
// Nearest minute, half-up: 0–29s → 0, 30–89s → 1.
int pomodoroFocusedMinutesFromSeconds(int focusedSeconds);
QString formatPomodoroFocusedDuration(int focusedMinutes);
