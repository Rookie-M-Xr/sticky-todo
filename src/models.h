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
