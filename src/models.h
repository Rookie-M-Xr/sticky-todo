#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QTime>
#include <QVector>

struct TodoItem {
    QString id;
    QString text;
    QDateTime createdAt;
    QDate dueDate;
    bool completed = false;
    QDateTime completedAt;
    QDateTime updatedAt;
    // TR #60: valid pinnedAt = pinned. Incomplete pins sit in GroupId::Pinned
    // (top of the incomplete list). Multiple pins: newest pinnedAt first.
    QDateTime pinnedAt;

    bool isPinned() const { return pinnedAt.isValid(); }
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

// Interval reminders (TR #19 + #42 + #50 + #51). Own file reminders.json — never a TodoItem field.
// Local-clock window: same-day [start, end) when start < end;
// cross-midnight [start, 24:00) ∪ [00:00, end) when end <= start.
// In-process snooze (TR #51) only shifts the next fire; never written to JSON.
constexpr int kReminderMinMinutes = 1;
constexpr int kReminderMaxMinutes = 7 * 24 * 60;
constexpr int kReminderMaxText = 200;
constexpr int kReminderMaxCount = 20;
constexpr int kReminderDayMinutes = 24 * 60;
constexpr int kReminderDefaultStartMinutes = 9 * 60;
constexpr int kReminderDefaultEndMinutes = 18 * 60;
constexpr int kReminderSnoozeMinutes5 = 5;
constexpr int kReminderSnoozeMinutes10 = 10;
constexpr int kReminderSnoozeMinutes15 = 15;

struct ReminderItem {
    QString id;
    QString text;
    int intervalMinutes = 60;
    int activeStartMinutes = 0;
    int activeEndMinutes = kReminderDayMinutes;
    bool enabled = true;
    QDateTime lastFiredAt;
    QDateTime createdAt;
    QDateTime updatedAt;

    QJsonObject toJson() const;
    static ReminderItem fromJson(const QJsonObject &object, bool *ok = nullptr);
};

int clampReminderInterval(int minutes);
QString formatReminderInterval(int minutes);
QString formatReminderTime(int minutes);
QString formatReminderWindow(int startMinutes, int endMinutes);
bool parseReminderTime(const QString &text, int *minutes, bool allowEndOfDay = false);
bool reminderWindowValid(int startMinutes, int endMinutes);
int reminderTimeToMinutes(const QTime &time);
bool reminderInActiveWindow(const ReminderItem &item, const QDateTime &now = QDateTime());
QDateTime reminderAnchor(const ReminderItem &item);
// snoozeUntil, when valid, replaces lastFired+interval for this next fire only.
QDateTime reminderNextDue(const ReminderItem &item,
                          const QDateTime &snoozeUntil = QDateTime());
bool reminderIsDue(const ReminderItem &item, const QDateTime &now = QDateTime(),
                   const QDateTime &snoozeUntil = QDateTime());
bool reminderSnoozeMinutesValid(int minutes);
QDateTime reminderSnoozeUntil(const QDateTime &from, int minutes);
// Fire-time banner copy (#77): the reminder text itself — no "提醒：" prefix
// and no pre-elide. Empty/whitespace → "定时提醒". The snackbar wraps or
// elides to the live label width and keeps this full string as the tooltip.
QString reminderFireLabel(const QString &text);
QString reminderSnoozeMenuLabel();

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
    Pinned,
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
// Fresh composer default (today). Tray quick-add uses the same due so the
// new incomplete item lands in GroupId::Today — not NoDate / Later.
QDate defaultNewTodoDue(QDate today = QDate());

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

// Session-only focus clock (TR #18 + #52 pause). Default minutes live in settings (TR #31).
// Optional cumulative stats (TR #35) live in pomodoro_stats.json — never todos.json.
// Pause is in-process only; remaining freezes and is not written to JSON.
constexpr int kPomodoroMinMinutes = 1;
constexpr int kPomodoroMaxMinutes = 120;
constexpr int kPomodoroDefaultMinutes = 25;
constexpr int kPomodoroDefaultSeconds = kPomodoroDefaultMinutes * 60;

int clampPomodoroMinutes(int minutes);
int pomodoroSecondsForMinutes(int minutes);
QString formatPomodoroStartHint(int minutes);
QString formatPomodoroClock(int remainingSeconds);
QString formatPomodoroPauseHint(int remainingSeconds);
QString formatPomodoroResumeHint(int remainingSeconds);
int pomodoroRemainingSeconds(const QDateTime &deadline,
                             const QDateTime &now = QDateTime());
// Same id toggles off (empty). A different id silently replaces the old one.
QString pomodoroAfterToggle(const QString &currentId, const QString &requestedId);
// Chip / 开始菜单：开、暂停、继续。暂停中点另一条为 Ignore（仍占互斥槽）。
enum class PomodoroClickAction {
    Start,
    Pause,
    Resume,
    Ignore
};
PomodoroClickAction pomodoroOnChipClick(const QString &currentId, bool paused,
                                        const QString &requestedId);
int pomodoroFrozenSeconds(int remainingSeconds);
QDateTime pomodoroResumeDeadline(int remainingSeconds,
                                 const QDateTime &now = QDateTime());
int pomodoroSessionSecondsLeft(const QDateTime &deadline, bool paused, int pausedRemaining,
                               const QDateTime &now = QDateTime());
// Complete only when remaining has hit zero. Manual mid-stop (remaining > 0) does not.
bool pomodoroCountsAsComplete(int remainingSeconds);
int pomodoroElapsedSeconds(int sessionMinutes, int remainingSeconds);
// Nearest minute, half-up: 0–29s → 0, 30–89s → 1.
int pomodoroFocusedMinutesFromSeconds(int focusedSeconds);
QString formatPomodoroFocusedDuration(int focusedMinutes);
