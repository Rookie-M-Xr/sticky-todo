#include "models.h"

#include <QJsonValue>
#include <QLocale>
#include <QUuid>
#include <algorithm>

namespace {

QDateTime parseDateTime(const QString &text, const QDateTime &fallback = QDateTime())
{
    if (text.trimmed().isEmpty())
        return fallback;
    QDateTime value = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!value.isValid())
        value = QDateTime::fromString(text, Qt::ISODate);
    if (value.isValid() && value.timeSpec() == Qt::LocalTime)
        return value;
    return value.isValid() ? value : fallback;
}

} // namespace

QJsonObject TodoItem::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("text"), text);
    object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("dueDate"), dueDate.isValid() ? dueDate.toString(Qt::ISODate) : QString());
    object.insert(QStringLiteral("completed"), completed);
    object.insert(QStringLiteral("completedAt"),
                  completedAt.isValid() ? completedAt.toString(Qt::ISODateWithMs) : QString());
    object.insert(QStringLiteral("updatedAt"), updatedAt.toString(Qt::ISODateWithMs));
    return object;
}

TodoItem TodoItem::fromJson(const QJsonObject &object, bool *ok)
{
    TodoItem item;
    bool valid = true;
    item.id = object.value(QStringLiteral("id")).toString().trimmed();
    if (item.id.isEmpty())
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.text = object.value(QStringLiteral("text")).toString().trimmed();
    if (item.text.isEmpty())
        valid = false;

    item.createdAt = parseDateTime(object.value(QStringLiteral("createdAt")).toString());
    if (!item.createdAt.isValid())
        item.createdAt = QDateTime::currentDateTime();
    item.updatedAt = parseDateTime(object.value(QStringLiteral("updatedAt")).toString(), item.createdAt);

    const QString dueText = object.value(QStringLiteral("dueDate")).toString().trimmed();
    if (!dueText.isEmpty()) {
        const QDate parsed = QDate::fromString(dueText, Qt::ISODate);
        if (parsed.isValid())
            item.dueDate = parsed;
    }

    item.completed = object.value(QStringLiteral("completed")).toBool(false);
    if (item.completed) {
        item.completedAt = parseDateTime(object.value(QStringLiteral("completedAt")).toString());
        if (!item.completedAt.isValid())
            item.completedAt = item.updatedAt;
    }

    if (item.text.size() > 4000)
        item.text = item.text.left(4000);

    if (ok)
        *ok = valid;
    return item;
}

QJsonObject DailySnapshot::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("text"), text);
    object.insert(QStringLiteral("sourceTodoId"), sourceTodoId);
    object.insert(QStringLiteral("date"), date.isValid() ? date.toString(Qt::ISODate) : QString());
    object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("updatedAt"), updatedAt.toString(Qt::ISODateWithMs));
    return object;
}

DailySnapshot DailySnapshot::fromJson(const QJsonObject &object, bool *ok)
{
    DailySnapshot item;
    bool valid = true;
    item.id = object.value(QStringLiteral("id")).toString().trimmed();
    if (item.id.isEmpty())
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.text = object.value(QStringLiteral("text")).toString();
    item.sourceTodoId = object.value(QStringLiteral("sourceTodoId")).toString().trimmed();
    const QString dateText = object.value(QStringLiteral("date")).toString().trimmed();
    if (!dateText.isEmpty()) {
        const QDate parsed = QDate::fromString(dateText, Qt::ISODate);
        if (parsed.isValid())
            item.date = parsed;
        else
            valid = false;
    } else {
        valid = false;
    }
    item.createdAt = parseDateTime(object.value(QStringLiteral("createdAt")).toString());
    if (!item.createdAt.isValid())
        item.createdAt = QDateTime::currentDateTime();
    item.updatedAt = parseDateTime(object.value(QStringLiteral("updatedAt")).toString(), item.createdAt);
    if (item.text.size() > 4000)
        item.text = item.text.left(4000);
    if (ok)
        *ok = valid;
    return item;
}

QString reportWeekKey(QDate day)
{
    if (!day.isValid())
        day = QDate::currentDate();
    int year = 0;
    const int week = day.weekNumber(&year);
    return QStringLiteral("%1-W%2").arg(year).arg(week, 2, 10, QChar(u'0'));
}

QString reportMonthKey(QDate day)
{
    if (!day.isValid())
        day = QDate::currentDate();
    return day.toString(QStringLiteral("yyyy-MM"));
}

QDate reportWeekStart(QDate day)
{
    if (!day.isValid())
        day = QDate::currentDate();
    // Qt: Monday=1 .. Sunday=7 — calendar week Mon–Sun.
    return day.addDays(1 - day.dayOfWeek());
}

QDate reportWeekEnd(QDate day)
{
    return reportWeekStart(day).addDays(6);
}

const QVector<GroupInfo> &allGroups()
{
    static const QVector<GroupInfo> groups = {
        {GroupId::Overdue, QStringLiteral("overdue"), QStringLiteral("已逾期")},
        {GroupId::Today, QStringLiteral("today"), QStringLiteral("今天")},
        {GroupId::Tomorrow, QStringLiteral("tomorrow"), QStringLiteral("明天")},
        {GroupId::Next7, QStringLiteral("next7"), QStringLiteral("未来 7 天")},
        {GroupId::Later, QStringLiteral("later"), QStringLiteral("更晚")},
        {GroupId::NoDate, QStringLiteral("nodate"), QStringLiteral("无日期")},
        {GroupId::PastCompleted, QStringLiteral("pastcompleted"), QStringLiteral("往期完成")},
    };
    return groups;
}

QString groupKey(GroupId id)
{
    for (const GroupInfo &info : allGroups()) {
        if (info.id == id)
            return info.key;
    }
    return QStringLiteral("today");
}

QString groupTitle(GroupId id)
{
    for (const GroupInfo &info : allGroups()) {
        if (info.id == id)
            return info.title;
    }
    return QStringLiteral("今天");
}

int groupOrder(GroupId id)
{
    int index = 0;
    for (const GroupInfo &info : allGroups()) {
        if (info.id == id)
            return index;
        ++index;
    }
    return index;
}

GroupId groupFor(const TodoItem &item, QDate today)
{
    if (!item.dueDate.isValid())
        return GroupId::NoDate;
    if (item.dueDate < today)
        return item.completed ? GroupId::PastCompleted : GroupId::Overdue;
    if (item.dueDate == today)
        return GroupId::Today;
    if (item.dueDate == today.addDays(1))
        return GroupId::Tomorrow;
    if (item.dueDate <= today.addDays(7))
        return GroupId::Next7;
    return GroupId::Later;
}

QVector<TodoGroup> buildGroups(const QVector<TodoItem> &items, QDate today, const QString &search)
{
    const QString query = search.trimmed();
    QVector<TodoGroup> groups;
    groups.reserve(allGroups().size());
    for (const GroupInfo &info : allGroups()) {
        TodoGroup group;
        group.id = info.id;
        groups.append(group);
    }

    for (const TodoItem &item : items) {
        if (!query.isEmpty() && !item.text.contains(query, Qt::CaseInsensitive))
            continue;
        const GroupId id = groupFor(item, today);
        const int index = groupOrder(id);
        if (index < 0 || index >= groups.size())
            continue;
        groups[index].items.append(item);
        if (!item.completed)
            ++groups[index].pendingCount;
    }

    for (TodoGroup &group : groups) {
        std::sort(group.items.begin(), group.items.end(), [](const TodoItem &a, const TodoItem &b) {
            if (a.dueDate.isValid() != b.dueDate.isValid())
                return a.dueDate.isValid();
            if (a.dueDate.isValid() && a.dueDate != b.dueDate)
                return a.dueDate < b.dueDate;
            if (a.createdAt != b.createdAt)
                return a.createdAt < b.createdAt;
            return a.id < b.id;
        });
    }

    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [](const TodoGroup &group) { return group.items.isEmpty(); }),
                 groups.end());
    return groups;
}

QString weekdayShort(QDate date)
{
    switch (date.dayOfWeek()) {
    case 1: return QStringLiteral("周一");
    case 2: return QStringLiteral("周二");
    case 3: return QStringLiteral("周三");
    case 4: return QStringLiteral("周四");
    case 5: return QStringLiteral("周五");
    case 6: return QStringLiteral("周六");
    case 7: return QStringLiteral("周日");
    default: return QString();
    }
}

QString dateHeadline(QDate date)
{
    if (!date.isValid())
        return QString();
    return QStringLiteral("%1月%2日 %3").arg(date.month()).arg(date.day()).arg(weekdayShort(date));
}

QString shortDueLabel(QDate due, QDate today)
{
    if (!due.isValid())
        return QStringLiteral("无日期");
    const qint64 diff = today.daysTo(due);
    if (diff == 0)
        return QStringLiteral("今天");
    if (diff == 1)
        return QStringLiteral("明天");
    if (diff == -1)
        return QStringLiteral("昨天");
    if (diff > 1 && diff <= 7)
        return weekdayShort(due);
    return QStringLiteral("%1月%2日").arg(due.month()).arg(due.day());
}

QString fullDueLabel(QDate due, QDate today)
{
    Q_UNUSED(today);
    if (!due.isValid())
        return QStringLiteral("无计划日期");
    return QStringLiteral("%1年%2月%3日 %4").arg(due.year()).arg(due.month()).arg(due.day()).arg(weekdayShort(due));
}

QString relativeDueHint(QDate due, QDate today)
{
    if (!due.isValid())
        return QStringLiteral("没有计划日期");
    const qint64 diff = today.daysTo(due);
    if (diff < 0)
        return QStringLiteral("已逾期 %1 天 · %2").arg(-diff).arg(fullDueLabel(due, today));
    if (diff == 0)
        return QStringLiteral("今天完成 · %1").arg(fullDueLabel(due, today));
    if (diff == 1)
        return QStringLiteral("明天完成 · %1").arg(fullDueLabel(due, today));
    return QStringLiteral("计划 %1 完成").arg(fullDueLabel(due, today));
}

int clampReminderInterval(int minutes)
{
    return qBound(kReminderMinMinutes, minutes, kReminderMaxMinutes);
}

QString formatReminderInterval(int minutes)
{
    const int n = clampReminderInterval(minutes);
    if (n % 60 == 0)
        return QStringLiteral("每 %1 小时").arg(n / 60);
    return QStringLiteral("每 %1 分钟").arg(n);
}

QDateTime reminderAnchor(const ReminderItem &item)
{
    if (item.lastFiredAt.isValid())
        return item.lastFiredAt;
    if (item.createdAt.isValid())
        return item.createdAt;
    return {};
}

QDateTime reminderNextDue(const ReminderItem &item)
{
    const QDateTime anchor = reminderAnchor(item);
    if (!anchor.isValid())
        return {};
    return anchor.addSecs(static_cast<qint64>(clampReminderInterval(item.intervalMinutes)) * 60);
}

bool reminderIsDue(const ReminderItem &item, const QDateTime &now)
{
    if (!item.enabled || item.text.trimmed().isEmpty())
        return false;
    const QDateTime due = reminderNextDue(item);
    if (!due.isValid())
        return false;
    const QDateTime origin = now.isValid() ? now : QDateTime::currentDateTime();
    return due <= origin;
}

QJsonObject ReminderItem::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("text"), text);
    object.insert(QStringLiteral("intervalMinutes"), intervalMinutes);
    object.insert(QStringLiteral("enabled"), enabled);
    object.insert(QStringLiteral("lastFiredAt"),
                  lastFiredAt.isValid() ? lastFiredAt.toString(Qt::ISODateWithMs) : QString());
    object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("updatedAt"), updatedAt.toString(Qt::ISODateWithMs));
    return object;
}

ReminderItem ReminderItem::fromJson(const QJsonObject &object, bool *ok)
{
    ReminderItem item;
    bool valid = true;
    item.id = object.value(QStringLiteral("id")).toString().trimmed();
    if (item.id.isEmpty())
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.text = object.value(QStringLiteral("text")).toString().trimmed();
    if (item.text.isEmpty())
        valid = false;
    if (item.text.size() > kReminderMaxText)
        item.text = item.text.left(kReminderMaxText);

    const QJsonValue intervalValue = object.value(QStringLiteral("intervalMinutes"));
    const int rawInterval = intervalValue.toInt(0);
    if (!intervalValue.isDouble() || rawInterval < kReminderMinMinutes
        || rawInterval > kReminderMaxMinutes) {
        valid = false;
    }
    item.intervalMinutes = clampReminderInterval(rawInterval > 0 ? rawInterval : 60);
    item.enabled = object.value(QStringLiteral("enabled")).toBool(true);

    item.createdAt = parseDateTime(object.value(QStringLiteral("createdAt")).toString());
    if (!item.createdAt.isValid())
        item.createdAt = QDateTime::currentDateTime();
    item.updatedAt = parseDateTime(object.value(QStringLiteral("updatedAt")).toString(), item.createdAt);
    item.lastFiredAt = parseDateTime(object.value(QStringLiteral("lastFiredAt")).toString());
    if (!item.lastFiredAt.isValid())
        item.lastFiredAt = item.createdAt;

    if (ok)
        *ok = valid;
    return item;
}

int clampPomodoroMinutes(int minutes)
{
    return qBound(kPomodoroMinMinutes, minutes, kPomodoroMaxMinutes);
}

int pomodoroSecondsForMinutes(int minutes)
{
    return clampPomodoroMinutes(minutes) * 60;
}

QString formatPomodoroStartHint(int minutes)
{
    return QStringLiteral("开始番茄钟（%1 分钟）").arg(clampPomodoroMinutes(minutes));
}

QString formatPomodoroClock(int remainingSeconds)
{
    const int clamped = qMax(0, remainingSeconds);
    const int minutes = clamped / 60;
    const int seconds = clamped % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar(u'0'))
        .arg(seconds, 2, 10, QChar(u'0'));
}

int pomodoroRemainingSeconds(const QDateTime &deadline, const QDateTime &now)
{
    if (!deadline.isValid())
        return 0;
    const QDateTime origin = now.isValid() ? now : QDateTime::currentDateTime();
    return qMax(0, static_cast<int>(origin.secsTo(deadline)));
}

QString pomodoroAfterToggle(const QString &currentId, const QString &requestedId)
{
    if (requestedId.isEmpty())
        return currentId;
    if (currentId == requestedId)
        return QString();
    return requestedId;
}

bool pomodoroCountsAsComplete(int remainingSeconds)
{
    return remainingSeconds <= 0;
}

int pomodoroElapsedSeconds(int sessionMinutes, int remainingSeconds)
{
    const int total = pomodoroSecondsForMinutes(sessionMinutes);
    const int left = qMax(0, remainingSeconds);
    return qMax(0, total - left);
}

int pomodoroFocusedMinutesFromSeconds(int focusedSeconds)
{
    if (focusedSeconds <= 0)
        return 0;
    return (focusedSeconds + 30) / 60;
}

QString formatPomodoroFocusedDuration(int focusedMinutes)
{
    const int minutes = qMax(0, focusedMinutes);
    return QStringLiteral("%1h %2m").arg(minutes / 60).arg(minutes % 60);
}
