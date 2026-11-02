#include "datastore.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <cstdio>

namespace {

int g_failed = 0;

void expect(bool cond, const char *msg)
{
    if (cond)
        return;
    ++g_failed;
    std::fprintf(stderr, "FAIL: %s\n", msg);
}

void pass(const char *msg)
{
    std::fprintf(stdout, "PASS: %s\n", msg);
}

QString testRoot()
{
    const QByteArray overrideRoot = qgetenv("STICKY_TODO_TEST_ROOT");
    if (!overrideRoot.isEmpty())
        return QDir::cleanPath(QString::fromLocal8Bit(overrideRoot));
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../testdata/datastore"));
}

QString makeDataDir()
{
    const QString root = testRoot();
    const QString dir = QDir(root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(dir)) {
        std::fprintf(stderr, "FAIL: mkpath %s\n", qPrintable(dir));
        return QString();
    }
    qputenv("STICKY_TODO_DATA_DIR", QDir::toNativeSeparators(dir).toUtf8());
    return dir;
}

void testRoundTrip()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "round-trip: data dir");
    if (dataDir.isEmpty())
        return;

    {
        DataStore store;
        expect(store.load(), "round-trip: initial load");
        const QString id1 = store.addTodo(QStringLiteral("buy milk"), QDate::currentDate());
        const QString id2 = store.addTodo(QStringLiteral("ship review"), QDate());
        expect(!id1.isEmpty() && !id2.isEmpty(), "round-trip: addTodo ids");
        expect(store.setCompleted(id1, true), "round-trip: setCompleted");
        expect(store.flush(), "round-trip: flush");
        expect(QFile::exists(QDir(dataDir).filePath(QStringLiteral("todos.json"))),
               "round-trip: todos.json exists");
    }

    {
        DataStore store;
        expect(store.load(), "round-trip: reload");
        expect(store.todos().size() == 2, "round-trip: two todos after reload");
        const TodoItem *a = nullptr;
        const TodoItem *b = nullptr;
        for (const TodoItem &item : store.todos()) {
            if (item.text == QStringLiteral("buy milk"))
                a = &item;
            if (item.text == QStringLiteral("ship review"))
                b = &item;
        }
        expect(a != nullptr && a->completed, "round-trip: completed todo restored");
        expect(b != nullptr && !b->completed && !b->dueDate.isValid(),
               "round-trip: undated todo restored");
    }

    if (g_failed == before)
        pass("JSON round-trip");
}

void testCorruptionRecovery()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "corrupt: data dir");
    if (dataDir.isEmpty())
        return;

    const QString todosPath = QDir(dataDir).filePath(QStringLiteral("todos.json"));
    const QString bakPath = todosPath + QStringLiteral(".bak");
    QString keepId;

    {
        DataStore store;
        expect(store.load(), "corrupt: first load");
        keepId = store.addTodo(QStringLiteral("keep-me"), QDate::currentDate().addDays(1));
        expect(!keepId.isEmpty(), "corrupt: seed todo");
        expect(store.flush(), "corrupt: first flush");
        expect(store.updateText(keepId, QStringLiteral("keep-me-v2")), "corrupt: update");
        expect(store.flush(), "corrupt: second flush");
        expect(QFile::exists(bakPath), "corrupt: .bak exists after second save");
    }

    {
        QFile file(todosPath);
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "corrupt: open todos.json");
        file.write("{ this is not valid json !!!");
        file.close();
    }

    {
        DataStore store;
        expect(store.load(), "corrupt: load after corruption");
        expect(store.todos().size() == 1, "corrupt: recovered one todo");
        const TodoItem *item = store.findTodo(keepId);
        expect(item != nullptr && item->text == QStringLiteral("keep-me"),
               "corrupt: todo restored from .bak");
        const QStringList notices = store.takeNotices();
        expect(!notices.isEmpty(), "corrupt: notices emitted");
        expect(store.flush(), "corrupt: flush after recovery");
    }

    if (g_failed == before)
        pass("corruption recovery from .bak");
}


void testImmediatePersistAndIsolation()
{
    const int before = g_failed;
    const QString dirA = makeDataDir();
    expect(!dirA.isEmpty(), "isolate: dir A");
    if (dirA.isEmpty())
        return;

    QString id;
    {
        DataStore store;
        expect(store.load(), "isolate: load A");
        id = store.addTodo(QStringLiteral("only-in-A"), QDate::currentDate());
        expect(!id.isEmpty(), "isolate: addTodo");
        // delayMs==0 now writes before returning; must exist without flush().
        expect(QFile::exists(QDir(dirA).filePath(QStringLiteral("todos.json"))),
               "isolate: todos.json after addTodo (immediate save)");
    }

    const QString dirB = makeDataDir();
    expect(!dirB.isEmpty(), "isolate: dir B");
    if (dirB.isEmpty())
        return;

    {
        DataStore store;
        expect(store.load(), "isolate: load B");
        expect(store.todos().isEmpty(), "isolate: B starts empty");
        expect(store.addTodo(QStringLiteral("only-in-B"), QDate()).size() > 0, "isolate: add in B");
        expect(store.flush(), "isolate: flush B");
    }

    qputenv("STICKY_TODO_DATA_DIR", QDir::toNativeSeparators(dirA).toUtf8());
    {
        DataStore store;
        expect(store.load(), "isolate: reload A");
        expect(store.todos().size() == 1, "isolate: A still has one todo");
        const TodoItem *item = store.findTodo(id);
        expect(item != nullptr && item->text == QStringLiteral("only-in-A"),
               "isolate: A content unchanged by B");
    }

    if (g_failed == before)
        pass("immediate persist + data-dir isolation");
}


TodoItem makeCompleted(const QString &text, int daysAgo)
{
    TodoItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.text = text;
    const QDateTime stamp = QDateTime::currentDateTime().addDays(-daysAgo);
    item.createdAt = stamp;
    item.updatedAt = stamp;
    item.completed = true;
    item.completedAt = stamp;
    return item;
}

void testRetentionPurge()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "retention: data dir");
    if (dataDir.isEmpty())
        return;

    QString oldId;
    QString recentId;
    QString openId;
    {
        DataStore store;
        expect(store.load(), "retention: load");
        expect(store.settings().retentionDays == 30, "retention: default N=30");

        const TodoItem oldItem = makeCompleted(QStringLiteral("old-done"), 45);
        const TodoItem recentItem = makeCompleted(QStringLiteral("recent-done"), 5);
        TodoItem openItem;
        openItem.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        openItem.text = QStringLiteral("still-open");
        openItem.createdAt = QDateTime::currentDateTime();
        openItem.updatedAt = openItem.createdAt;
        oldId = oldItem.id;
        recentId = recentItem.id;
        openId = openItem.id;
        expect(store.restoreTodo(oldItem), "retention: restore old");
        expect(store.restoreTodo(recentItem), "retention: restore recent");
        expect(store.restoreTodo(openItem), "retention: restore open");
        expect(store.todos().size() == 3, "retention: three seeded");

        const int purged = store.purgeCompletedOlderThan(30);
        expect(purged == 1, "retention: purge removes one");
        expect(store.findTodo(oldId) == nullptr, "retention: old gone");
        expect(store.findTodo(recentId) != nullptr, "retention: recent remains");
        expect(store.findTodo(openId) != nullptr, "retention: open remains");
        expect(store.flush(), "retention: flush after purge");
    }

    // Auto-purge on load: seed another old item, reload with N=30
    {
        DataStore store;
        expect(store.load(), "retention-auto: load");
        const TodoItem older = makeCompleted(QStringLiteral("auto-old"), 60);
        const TodoItem fresh = makeCompleted(QStringLiteral("auto-fresh"), 2);
        expect(store.restoreTodo(older), "retention-auto: restore older");
        expect(store.restoreTodo(fresh), "retention-auto: restore fresh");
        store.setRetentionDays(30);
        expect(store.settings().retentionDays == 30, "retention-auto: N stored");
        expect(store.flush(), "retention-auto: flush seed");
    }

    {
        DataStore store;
        expect(store.load(), "retention-auto: reload applies purge");
        bool sawFresh = false;
        bool sawOlder = false;
        for (const TodoItem &item : store.todos()) {
            if (item.text == QStringLiteral("auto-old"))
                sawOlder = true;
            if (item.text == QStringLiteral("auto-fresh"))
                sawFresh = true;
        }
        expect(!sawOlder, "retention-auto: older purged on load");
        expect(sawFresh, "retention-auto: fresh kept on load");
        expect(store.findTodo(recentId) != nullptr, "retention-auto: prior recent still there");
        expect(store.findTodo(openId) != nullptr, "retention-auto: open still there");
    }

    if (g_failed == before)
        pass("retention purge + auto on load");
}


void testDailySnapshots()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "daily: data dir");

    {
        DataStore store;
        expect(store.load(), "daily: load");
        const QString openId = store.addTodo(QStringLiteral("open task"), QDate::currentDate());
        const QString doneId = store.addTodo(QStringLiteral("done today"), QDate::currentDate());
        expect(!openId.isEmpty() && !doneId.isEmpty(), "daily: seeded todos");
        expect(store.setCompleted(doneId, true), "daily: complete today");

        // Older completed should not enter today's report
        TodoItem oldItem;
        oldItem.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        oldItem.text = QStringLiteral("done yesterday");
        oldItem.createdAt = QDateTime::currentDateTime().addDays(-2);
        oldItem.updatedAt = oldItem.createdAt;
        oldItem.dueDate = QDate::currentDate().addDays(-1);
        oldItem.completed = true;
        oldItem.completedAt = QDateTime::currentDateTime().addDays(-1);
        expect(store.restoreTodo(oldItem), "daily: restore yesterday");

        const int added = store.syncDailyFromCompleted(QDate::currentDate());
        expect(added == 1, "daily: one snapshot from today");
        const QVector<DailySnapshot> snaps = store.dailySnapshots(QDate::currentDate());
        expect(snaps.size() == 1, "daily: one line");
        expect(snaps.first().sourceTodoId == doneId, "daily: source id");
        expect(snaps.first().text == QStringLiteral("done today"), "daily: text copied");

        const QString snapId = snaps.first().id;
        expect(store.updateDailySnapshotText(snapId, QStringLiteral("edited report line")),
               "daily: edit snapshot");
        const TodoItem *todo = store.findTodo(doneId);
        expect(todo != nullptr, "daily: todo still there");
        expect(todo->text == QStringLiteral("done today"), "daily: todo text unchanged");
        expect(store.dailySnapshots(QDate::currentDate()).first().text
                   == QStringLiteral("edited report line"),
               "daily: snapshot edited");

        const QString plain = store.formatDailyPlainText(QDate::currentDate());
        expect(plain.contains(QStringLiteral("edited report line")), "daily: plain has edit");
        expect(plain.contains(QDate::currentDate().toString(Qt::ISODate)), "daily: plain has date");

        // Second sync should not duplicate
        expect(store.syncDailyFromCompleted(QDate::currentDate()) == 0, "daily: no duplicate");
        expect(store.flush(), "daily: flush");
        expect(QFile::exists(store.dailyReportsPath()), "daily: file exists");
    }

    {
        DataStore store;
        expect(store.load(), "daily-reload: load");
        const QVector<DailySnapshot> snaps = store.dailySnapshots(QDate::currentDate());
        expect(snaps.size() == 1, "daily-reload: one line");
        expect(snaps.first().text == QStringLiteral("edited report line"),
               "daily-reload: edit persisted");
        const TodoItem *todo = nullptr;
        for (const TodoItem &item : store.todos()) {
            if (item.text == QStringLiteral("done today")) {
                todo = &item;
                break;
            }
        }
        expect(todo != nullptr, "daily-reload: original todo");
        expect(todo->text == QStringLiteral("done today"), "daily-reload: todo still original");
    }

    if (g_failed == before)
        pass("daily snapshots + edit isolation + persist");
}

void testWeeklyMonthlySnapshots()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "period: data dir");
    if (dataDir.isEmpty())
        return;

    const QDate today = QDate::currentDate();
    const QDate weekStart = reportWeekStart(today);
    QString midWeekId;
    QString monthId;
    {
        DataStore store;
        expect(store.load(), "period: load");
        TodoItem mid;
        mid.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        mid.text = QStringLiteral("done midweek");
        mid.createdAt = QDateTime::currentDateTime();
        mid.updatedAt = mid.createdAt;
        mid.completed = true;
        mid.completedAt = QDateTime(weekStart.addDays(2), QTime(10, 0));
        midWeekId = mid.id;
        expect(store.restoreTodo(mid), "period: restore midweek");

        TodoItem monthItem;
        monthItem.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        monthItem.text = QStringLiteral("done this month");
        monthItem.createdAt = QDateTime::currentDateTime();
        monthItem.updatedAt = monthItem.createdAt;
        monthItem.completed = true;
        // First day of month, but outside current week if possible; still in month.
        QDate monthDay(today.year(), today.month(), 1);
        if (monthDay >= weekStart && monthDay <= reportWeekEnd(today))
            monthDay = today; // still valid month membership
        monthItem.completedAt = QDateTime(monthDay, QTime(9, 0));
        monthId = monthItem.id;
        expect(store.restoreTodo(monthItem), "period: restore month");

        expect(store.syncWeeklyFromCompleted(today) >= 1, "period: week sync adds");
        const QVector<DailySnapshot> week = store.weeklySnapshots(today);
        expect(!week.isEmpty(), "period: week nonempty");
        bool foundMid = false;
        for (const DailySnapshot &snap : week) {
            if (snap.sourceTodoId == midWeekId)
                foundMid = true;
        }
        expect(foundMid, "period: midweek in week report");

        expect(store.syncMonthlyFromCompleted(today) >= 1, "period: month sync adds");
        const QVector<DailySnapshot> month = store.monthlySnapshots(today);
        expect(!month.isEmpty(), "period: month nonempty");

        // Prefer editing the midweek snapshot when present.
        QString weekSnapId = week.first().id;
        for (const DailySnapshot &snap : week) {
            if (snap.sourceTodoId == midWeekId) {
                weekSnapId = snap.id;
                break;
            }
        }
        expect(store.updateReportSnapshotText(ReportKind::Weekly, weekSnapId, QStringLiteral("edited week")),
               "period: edit week");
        bool todoUnchanged = false;
        for (const TodoItem &item : store.todos()) {
            if (item.id == midWeekId) {
                todoUnchanged = (item.text == QStringLiteral("done midweek"));
                break;
            }
        }
        expect(todoUnchanged, "period: week edit does not write back todo");

        const QString plain = store.formatWeeklyPlainText(today);
        expect(plain.contains(QStringLiteral("周报")), "period: week plain title");
        expect(plain.contains(QStringLiteral("edited week")) || plain.contains(QStringLiteral("done midweek"))
                   || plain.contains(QStringLiteral("done this month")),
               "period: week plain has content");

        // Visibility toggles persist and do not wipe report files
        store.setShowWeeklyReport(false);
        store.setShowMonthlyReport(false);
        expect(!store.settings().showWeeklyReport, "period: weekly hidden");
        expect(store.flush(), "period: flush");
        expect(QFile::exists(store.weeklyReportsPath()), "period: weekly file kept");
        expect(QFile::exists(store.monthlyReportsPath()), "period: monthly file kept");
    }

    {
        DataStore store;
        expect(store.load(), "period-reload: load");
        expect(!store.settings().showWeeklyReport, "period-reload: weekly still hidden");
        expect(!store.settings().showMonthlyReport, "period-reload: monthly still hidden");
        expect(store.settings().showDailyReport, "period-reload: daily default on");
        expect(!store.weeklySnapshots(today).isEmpty(), "period-reload: week data kept while hidden");
        expect(!store.monthlySnapshots(today).isEmpty(), "period-reload: month data kept while hidden");
        store.setShowWeeklyReport(true);
        expect(store.settings().showWeeklyReport, "period-reload: weekly re-enabled");
    }

    if (g_failed == before)
        pass("weekly/monthly snapshots + visibility persist");
}


void testArchiveReimport()
{
    const int before = g_failed;

    {
        const QString dataDir = makeDataDir();
        expect(!dataDir.isEmpty(), "reimport: data dir");
        if (dataDir.isEmpty())
            return;

        QString archivedId;
        QString archivedText;
        QDate archivedDue;
        QDateTime archivedCompletedAt;

        DataStore store;
        expect(store.load(), "reimport: load");
        TodoItem item = makeCompleted(QStringLiteral("reimport-me"), 40);
        item.dueDate = QDate::currentDate().addDays(-2);
        archivedId = item.id;
        archivedText = item.text;
        archivedDue = item.dueDate;
        archivedCompletedAt = item.completedAt;
        expect(store.restoreTodo(item), "reimport: seed");
        expect(store.archiveCompletedOlderThan(30) == 1, "reimport: archive one");
        expect(store.findTodo(archivedId) == nullptr, "reimport: gone from main");
        expect(store.listArchived().size() == 1, "reimport: one in archive");
        expect(store.importAllArchived() == 1, "reimport: import all");
        const TodoItem *back = store.findTodo(archivedId);
        expect(back != nullptr, "reimport: back with same id");
        if (back) {
            expect(back->text == archivedText, "reimport: text intact");
            expect(back->dueDate == archivedDue, "reimport: due intact");
            expect(back->completed, "reimport: still completed");
            expect(back->completedAt == archivedCompletedAt, "reimport: completedAt intact");
        }
        expect(store.listArchived().isEmpty(), "reimport: archive emptied");
        expect(store.flush(), "reimport: flush");
    }

    // Conflict-id: live already has same id → mint new UUID, keep fields.
    {
        const QString dataDir = makeDataDir();
        expect(!dataDir.isEmpty(), "conflict: data dir");
        if (dataDir.isEmpty())
            return;

        DataStore store;
        expect(store.load(), "conflict: load");
        TodoItem live = makeCompleted(QStringLiteral("live-keep"), 3);
        live.id = QStringLiteral("fixed-conflict-id");
        TodoItem toArchive = makeCompleted(QStringLiteral("from-archive"), 50);
        toArchive.id = QStringLiteral("fixed-conflict-id");
        toArchive.dueDate = QDate::currentDate().addDays(-9);
        expect(store.restoreTodo(toArchive), "conflict: seed archive candidate");
        expect(store.archiveCompletedOlderThan(30) == 1, "conflict: archive candidate");
        expect(store.findTodo(QStringLiteral("fixed-conflict-id")) == nullptr, "conflict: main empty of id");
        expect(store.restoreTodo(live), "conflict: put live back");
        expect(store.findTodo(QStringLiteral("fixed-conflict-id")) != nullptr, "conflict: live present");
        expect(store.listArchived().size() == 1, "conflict: archive has one");
        expect(store.importAllArchived() == 1, "conflict: import with conflict");
        expect(store.findTodo(QStringLiteral("fixed-conflict-id")) != nullptr, "conflict: live id kept");
        expect(store.todos().size() == 2, "conflict: two todos after import");
        bool sawArchiveText = false;
        bool sawNewId = false;
        for (const TodoItem &item : store.todos()) {
            if (item.text == QStringLiteral("from-archive")) {
                sawArchiveText = true;
                expect(item.id != QStringLiteral("fixed-conflict-id"), "conflict: restored got new id");
                expect(item.dueDate == QDate::currentDate().addDays(-9), "conflict: due intact");
                sawNewId = item.id != QStringLiteral("fixed-conflict-id");
            }
        }
        expect(sawArchiveText && sawNewId, "conflict: archived content restored under new id");
        expect(store.listArchived().isEmpty(), "conflict: archive empty after import");
    }

    // Per-index import leaves the other entry in archive.
    {
        const QString dataDir = makeDataDir();
        expect(!dataDir.isEmpty(), "pick: data dir");
        if (dataDir.isEmpty())
            return;

        DataStore store;
        expect(store.load(), "pick: load");
        TodoItem a = makeCompleted(QStringLiteral("pick-a"), 40);
        TodoItem b = makeCompleted(QStringLiteral("pick-b"), 41);
        expect(store.restoreTodo(a), "pick: seed a");
        expect(store.restoreTodo(b), "pick: seed b");
        expect(store.archiveCompletedOlderThan(30) == 2, "pick: archive both");
        const QVector<ArchivedEntry> listed = store.listArchived();
        expect(listed.size() == 2, "pick: listed 2");
        int idxA = -1;
        for (const ArchivedEntry &e : listed) {
            if (e.item.text == QStringLiteral("pick-a"))
                idxA = e.index;
        }
        expect(idxA >= 0, "pick: found pick-a index");
        expect(store.importArchived(QVector<int>{idxA}) == 1, "pick: import one");
        expect(store.findTodo(a.id) != nullptr, "pick: pick-a back");
        expect(store.findTodo(b.id) == nullptr, "pick: pick-b still out");
        bool stillB = false;
        for (const ArchivedEntry &e : store.listArchived()) {
            if (e.item.text == QStringLiteral("pick-b"))
                stillB = true;
        }
        expect(stillB, "pick: pick-b remains in archive");
        expect(store.listArchived().size() == 1, "pick: one left in archive");
    }

    if (g_failed == before)
        pass("archive re-import + conflict id + per-index");
}

void testArchiveExpired()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "archive: data dir");
    if (dataDir.isEmpty())
        return;

    QString oldId;
    QString recentId;
    {
        DataStore store;
        expect(store.load(), "archive: load");
        const TodoItem oldItem = makeCompleted(QStringLiteral("arch-old"), 40);
        const TodoItem recentItem = makeCompleted(QStringLiteral("arch-recent"), 3);
        oldId = oldItem.id;
        recentId = recentItem.id;
        expect(store.restoreTodo(oldItem), "archive: restore old");
        expect(store.restoreTodo(recentItem), "archive: restore recent");
        const int n = store.archiveCompletedOlderThan(30);
        expect(n == 1, "archive: one archived");
        expect(store.findTodo(oldId) == nullptr, "archive: old removed from live");
        expect(store.findTodo(recentId) != nullptr, "archive: recent stays");
        expect(QFile::exists(store.archivePath()), "archive: file exists");
        expect(store.flush(), "archive: flush");

        QFile file(store.archivePath());
        expect(file.open(QIODevice::ReadOnly), "archive: open file");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        expect(doc.isObject(), "archive: root object");
        const QJsonArray arr = doc.object().value(QStringLiteral("archived")).toArray();
        expect(arr.size() == 1, "archive: one entry");
        const QJsonObject entry = arr.at(0).toObject();
        expect(!entry.value(QStringLiteral("archivedAt")).toString().isEmpty(), "archive: archivedAt set");
        expect(entry.value(QStringLiteral("item")).toObject().value(QStringLiteral("id")).toString() == oldId,
               "archive: item id matches");
    }

    // purgeAllCompleted
    {
        DataStore store;
        expect(store.load(), "purge-all: load");
        expect(store.purgeAllCompleted() >= 1, "purge-all: removes remaining completed");
        for (const TodoItem &item : store.todos())
            expect(!item.completed, "purge-all: no completed left");
    }

    if (g_failed == before)
        pass("archive expired + purge all");
}

void testTrayHideHintPersist()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "tray-hint: data dir");
    if (dataDir.isEmpty())
        return;

    {
        DataStore store;
        expect(store.load(), "tray-hint: load");
        expect(!store.settings().trayHideHintShown, "tray-hint: default off");
        store.setTrayHideHintShown(true);
        expect(store.settings().trayHideHintShown, "tray-hint: set");
        expect(store.flush(), "tray-hint: flush");
    }

    {
        DataStore store;
        expect(store.load(), "tray-hint: reload");
        expect(store.settings().trayHideHintShown, "tray-hint: persisted");
    }

    if (g_failed == before)
        pass("tray hide hint persisted");
}

void testPomodoroHelpers()
{
    const int before = g_failed;
    expect(kPomodoroDefaultSeconds == 25 * 60, "pomo: default 25 minutes");
    expect(kPomodoroDefaultMinutes == 25, "pomo: default minutes 25");
    expect(clampPomodoroMinutes(0) == 1, "pomo: clamp low");
    expect(clampPomodoroMinutes(-8) == 1, "pomo: clamp negative");
    expect(clampPomodoroMinutes(25) == 25, "pomo: clamp 25");
    expect(clampPomodoroMinutes(120) == 120, "pomo: clamp 120");
    expect(clampPomodoroMinutes(121) == 120, "pomo: clamp high");
    expect(pomodoroSecondsForMinutes(1) == 60, "pomo: 1 min = 60s");
    expect(pomodoroSecondsForMinutes(50) == 50 * 60, "pomo: 50 min seconds");
    expect(pomodoroSecondsForMinutes(999) == 120 * 60, "pomo: high minutes clamped");
    expect(formatPomodoroStartHint(50) == QStringLiteral("开始番茄钟（50 分钟）"),
           "pomo: start hint 50");
    expect(formatPomodoroClock(25 * 60) == QStringLiteral("25:00"), "pomo: format 25:00");
    expect(formatPomodoroClock(61) == QStringLiteral("01:01"), "pomo: format 01:01");
    expect(formatPomodoroClock(9) == QStringLiteral("00:09"), "pomo: format 00:09");
    expect(formatPomodoroClock(0) == QStringLiteral("00:00"), "pomo: format zero");
    expect(formatPomodoroClock(-12) == QStringLiteral("00:00"), "pomo: format negative");

    const QDateTime now(QDate(2026, 11, 2), QTime(12, 0, 0));
    expect(pomodoroRemainingSeconds(now.addSecs(90), now) == 90, "pomo: remaining 90");
    expect(pomodoroRemainingSeconds(now.addSecs(-1), now) == 0, "pomo: remaining past is 0");
    expect(pomodoroRemainingSeconds(QDateTime(), now) == 0, "pomo: invalid deadline");

    expect(pomodoroAfterToggle(QString(), QStringLiteral("a")) == QStringLiteral("a"),
           "pomo: start first");
    expect(pomodoroAfterToggle(QStringLiteral("a"), QStringLiteral("a")).isEmpty(),
           "pomo: same id stops");
    expect(pomodoroAfterToggle(QStringLiteral("a"), QStringLiteral("b")) == QStringLiteral("b"),
           "pomo: other id replaces");
    expect(pomodoroAfterToggle(QStringLiteral("a"), QString()).isEmpty() == false,
           "pomo: empty request keeps current");

    expect(pomodoroCountsAsComplete(0), "pomo: remaining 0 is complete");
    expect(pomodoroCountsAsComplete(-3), "pomo: remaining past is complete");
    expect(!pomodoroCountsAsComplete(1), "pomo: remaining 1 is mid-stop");
    expect(pomodoroElapsedSeconds(25, 25 * 60) == 0, "pomo: elapsed at start");
    expect(pomodoroElapsedSeconds(25, 24 * 60) == 60, "pomo: elapsed 1 min");
    expect(pomodoroElapsedSeconds(25, 0) == 25 * 60, "pomo: elapsed full session");
    expect(pomodoroElapsedSeconds(25, 99999) == 0, "pomo: remaining above total");
    expect(pomodoroFocusedMinutesFromSeconds(0) == 0, "pomo: 0s -> 0m");
    expect(pomodoroFocusedMinutesFromSeconds(29) == 0, "pomo: 29s -> 0m");
    expect(pomodoroFocusedMinutesFromSeconds(30) == 1, "pomo: 30s -> 1m");
    expect(pomodoroFocusedMinutesFromSeconds(89) == 1, "pomo: 89s -> 1m");
    expect(pomodoroFocusedMinutesFromSeconds(90) == 2, "pomo: 90s -> 2m");
    expect(pomodoroFocusedMinutesFromSeconds(-8) == 0, "pomo: negative seconds");
    expect(formatPomodoroFocusedDuration(0) == QStringLiteral("0h 0m"), "pomo: 0h 0m");
    expect(formatPomodoroFocusedDuration(25) == QStringLiteral("0h 25m"), "pomo: 0h 25m");
    expect(formatPomodoroFocusedDuration(60) == QStringLiteral("1h 0m"), "pomo: 1h 0m");
    expect(formatPomodoroFocusedDuration(75) == QStringLiteral("1h 15m"), "pomo: 1h 15m");
    expect(formatPomodoroFocusedDuration(-4) == QStringLiteral("0h 0m"), "pomo: neg duration");

    if (g_failed == before)
        pass("pomodoro helpers (format / remaining / mutex / stats)");
}

void testPomodoroDoesNotTouchTodos()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "pomo-json: data dir");
    if (dataDir.isEmpty())
        return;

    const QString todosPath = QDir(dataDir).filePath(QStringLiteral("todos.json"));
    QString keepId;
    QString otherId;
    {
        DataStore store;
        expect(store.load(), "pomo-json: load");
        keepId = store.addTodo(QStringLiteral("focus-me"), QDate::currentDate());
        otherId = store.addTodo(QStringLiteral("later"), QDate());
        expect(!keepId.isEmpty() && !otherId.isEmpty(), "pomo-json: seed");
        expect(store.flush(), "pomo-json: flush seed");
    }

    {
        QFile file(todosPath);
        expect(file.open(QIODevice::ReadOnly), "pomo-json: read seed");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        QJsonObject root = doc.object();
        QJsonArray todos = root.value(QStringLiteral("todos")).toArray();
        expect(todos.size() == 2, "pomo-json: two items");
        QJsonObject first = todos.at(0).toObject();
        first.insert(QStringLiteral("pomodoroEndsAt"), QStringLiteral("2099-01-01T00:00:00"));
        first.insert(QStringLiteral("pomodoroActive"), true);
        todos.replace(0, first);
        root.insert(QStringLiteral("todos"), todos);
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "pomo-json: rewrite extras");
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }

    {
        DataStore store;
        expect(store.load(), "pomo-json: load extras");
        expect(store.todos().size() == 2, "pomo-json: both survived unknown fields");
        expect(store.updateText(keepId, QStringLiteral("focus-me-v2")), "pomo-json: edit while clock session");
        expect(store.setCompleted(otherId, true), "pomo-json: complete other");
        expect(store.flush(), "pomo-json: flush after edits");
        expect(store.findTodo(keepId) != nullptr, "pomo-json: focus item still there");
        expect(store.findTodo(keepId)->text == QStringLiteral("focus-me-v2"),
               "pomo-json: text kept");
        expect(store.findTodo(otherId) != nullptr && store.findTodo(otherId)->completed,
               "pomo-json: other kept");
    }

    {
        QFile file(todosPath);
        expect(file.open(QIODevice::ReadOnly), "pomo-json: read rewritten");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        const QJsonArray todos = doc.object().value(QStringLiteral("todos")).toArray();
        expect(todos.size() == 2, "pomo-json: still two after save");
        for (const QJsonValue &value : todos) {
            const QJsonObject object = value.toObject();
            expect(!object.contains(QStringLiteral("pomodoroEndsAt")),
                   "pomo-json: session field not persisted");
            expect(!object.contains(QStringLiteral("pomodoroActive")),
                   "pomo-json: active flag not persisted");
            expect(object.contains(QStringLiteral("id")), "pomo-json: id kept");
            expect(object.contains(QStringLiteral("text")), "pomo-json: text kept");
        }
    }

    if (g_failed == before)
        pass("pomodoro session fields ignored; todos survive edits");
}

void testPomodoroMinutesPersist()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "pomo-min: data dir");
    if (dataDir.isEmpty())
        return;

    const QString settingsFile = QDir(dataDir).filePath(QStringLiteral("settings.json"));
    const QString todosFile = QDir(dataDir).filePath(QStringLiteral("todos.json"));

    {
        DataStore store;
        expect(store.load(), "pomo-min: load");
        expect(store.settings().pomodoroMinutes == kPomodoroDefaultMinutes,
               "pomo-min: default 25");
        store.setPomodoroMinutes(0);
        expect(store.settings().pomodoroMinutes == 1, "pomo-min: clamp 0 -> 1");
        store.setPomodoroMinutes(999);
        expect(store.settings().pomodoroMinutes == 120, "pomo-min: clamp 999 -> 120");
        store.setPomodoroMinutes(50);
        expect(store.settings().pomodoroMinutes == 50, "pomo-min: set 50");
        expect(store.flush(), "pomo-min: flush");
        expect(QFile::exists(settingsFile), "pomo-min: settings.json in data dir");
        expect(settingsFile.startsWith(dataDir), "pomo-min: not install/config path");
    }

    {
        DataStore store;
        expect(store.load(), "pomo-min: reload");
        expect(store.settings().pomodoroMinutes == 50, "pomo-min: 50 persisted");
        const QString id = store.addTodo(QStringLiteral("focus"), QDate::currentDate());
        expect(!id.isEmpty(), "pomo-min: seed todo");
        expect(store.flush(), "pomo-min: flush todo");
    }

    {
        QFile file(settingsFile);
        expect(file.open(QIODevice::ReadOnly), "pomo-min: read settings");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        QJsonObject root = doc.object();
        expect(root.value(QStringLiteral("pomodoroMinutes")).toInt() == 50,
               "pomo-min: json has 50");
        root.remove(QStringLiteral("pomodoroMinutes"));
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "pomo-min: rewrite missing");
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }

    {
        DataStore store;
        expect(store.load(), "pomo-min: load missing key");
        expect(store.settings().pomodoroMinutes == kPomodoroDefaultMinutes,
               "pomo-min: missing key -> 25");
    }

    {
        QFile file(settingsFile);
        expect(file.open(QIODevice::ReadOnly), "pomo-min: read for out-of-range");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        QJsonObject root = doc.object();
        root.insert(QStringLiteral("pomodoroMinutes"), 0);
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "pomo-min: write 0");
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }

    {
        DataStore store;
        expect(store.load(), "pomo-min: load zero");
        expect(store.settings().pomodoroMinutes == 1, "pomo-min: stored 0 clamps to 1");
        expect(QFile::exists(todosFile), "pomo-min: todos.json untouched path");
    }

    if (g_failed == before)
        pass("pomodoro default minutes persist + clamp");
}

void testPomodoroStatsOptInPersist()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "pomo-stats: data dir");
    if (dataDir.isEmpty())
        return;

    const QString statsFile = QDir(dataDir).filePath(QStringLiteral("pomodoro_stats.json"));
    const QString settingsFile = QDir(dataDir).filePath(QStringLiteral("settings.json"));
    const QString todosFile = QDir(dataDir).filePath(QStringLiteral("todos.json"));

    {
        DataStore store;
        expect(store.load(), "pomo-stats: load");
        expect(!store.settings().pomodoroStatsEnabled, "pomo-stats: default off");
        expect(store.pomodoroStats().completedCount == 0, "pomo-stats: default count 0");
        expect(store.pomodoroStats().focusedMinutes == 0, "pomo-stats: default minutes 0");
        store.recordPomodoroCompleted(25);
        store.recordPomodoroInterrupted(10 * 60);
        expect(store.pomodoroStats().completedCount == 0, "pomo-stats: off ignores complete");
        expect(store.pomodoroStats().focusedMinutes == 0, "pomo-stats: off ignores interrupt");
        expect(store.flush(), "pomo-stats: flush while off");
        expect(!QFile::exists(statsFile), "pomo-stats: no file while default off");
    }

    {
        DataStore store;
        expect(store.load(), "pomo-stats: reload still off");
        expect(!store.settings().pomodoroStatsEnabled, "pomo-stats: still off");
        store.setPomodoroStatsEnabled(true);
        expect(store.settings().pomodoroStatsEnabled, "pomo-stats: enabled");
        store.recordPomodoroCompleted(25);
        expect(store.pomodoroStats().completedCount == 1, "pomo-stats: complete +1");
        expect(store.pomodoroStats().focusedMinutes == 25, "pomo-stats: complete +25m");
        store.recordPomodoroInterrupted(29);
        expect(store.pomodoroStats().focusedMinutes == 25, "pomo-stats: 29s does not add");
        store.recordPomodoroInterrupted(90);
        expect(store.pomodoroStats().completedCount == 1, "pomo-stats: interrupt no +count");
        expect(store.pomodoroStats().focusedMinutes == 27, "pomo-stats: 90s -> +2m");
        const QString id = store.addTodo(QStringLiteral("focus"), QDate::currentDate());
        expect(!id.isEmpty(), "pomo-stats: seed todo");
        expect(store.flush(), "pomo-stats: flush after enable");
        expect(QFile::exists(statsFile), "pomo-stats: stats file in data dir");
        expect(statsFile.startsWith(dataDir), "pomo-stats: not install/config path");
    }

    {
        QFile file(todosFile);
        expect(file.open(QIODevice::ReadOnly), "pomo-stats: read todos");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        const QJsonObject root = doc.object();
        expect(!root.contains(QStringLiteral("completedCount")),
               "pomo-stats: todos.json has no completedCount");
        expect(!root.contains(QStringLiteral("focusedMinutes")),
               "pomo-stats: todos.json has no focusedMinutes");
        expect(!root.contains(QStringLiteral("pomodoroStatsEnabled")),
               "pomo-stats: todos.json has no stats flag");
        const QJsonArray todos = root.value(QStringLiteral("todos")).toArray();
        expect(todos.size() == 1, "pomo-stats: todo kept");
        expect(!todos.at(0).toObject().contains(QStringLiteral("completedCount")),
               "pomo-stats: todo item has no stats fields");
    }

    {
        QFile file(settingsFile);
        expect(file.open(QIODevice::ReadOnly), "pomo-stats: read settings");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        const QJsonObject root = doc.object();
        expect(root.value(QStringLiteral("pomodoroStatsEnabled")).toBool() == true,
               "pomo-stats: settings flag true");
        expect(!root.contains(QStringLiteral("completedCount")),
               "pomo-stats: settings.json has no counts");
        expect(!root.contains(QStringLiteral("focusedMinutes")),
               "pomo-stats: settings.json has no minutes");
    }

    {
        QFile file(statsFile);
        expect(file.open(QIODevice::ReadOnly), "pomo-stats: read stats json");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        const QJsonObject root = doc.object();
        expect(root.value(QStringLiteral("completedCount")).toInt() == 1,
               "pomo-stats: json count 1");
        expect(root.value(QStringLiteral("focusedMinutes")).toInt() == 27,
               "pomo-stats: json minutes 27");
        expect(!root.contains(QStringLiteral("todos")), "pomo-stats: stats file not todos");
    }

    {
        DataStore store;
        expect(store.load(), "pomo-stats: restart");
        expect(store.settings().pomodoroStatsEnabled, "pomo-stats: flag kept");
        expect(store.pomodoroStats().completedCount == 1, "pomo-stats: count kept");
        expect(store.pomodoroStats().focusedMinutes == 27, "pomo-stats: minutes kept");
        store.setPomodoroStatsEnabled(false);
        expect(!store.settings().pomodoroStatsEnabled, "pomo-stats: disabled");
        store.recordPomodoroCompleted(50);
        store.recordPomodoroInterrupted(20 * 60);
        expect(store.pomodoroStats().completedCount == 1, "pomo-stats: off keeps count");
        expect(store.pomodoroStats().focusedMinutes == 27, "pomo-stats: off keeps minutes");
        expect(store.flush(), "pomo-stats: flush after disable");
    }

    {
        DataStore store;
        expect(store.load(), "pomo-stats: reload after disable");
        expect(!store.settings().pomodoroStatsEnabled, "pomo-stats: still off");
        expect(store.pomodoroStats().completedCount == 1, "pomo-stats: numbers survive off");
        expect(store.pomodoroStats().focusedMinutes == 27, "pomo-stats: duration survives off");
        expect(QFile::exists(statsFile), "pomo-stats: file kept after disable");
    }

    {
        QFile file(statsFile);
        expect(file.open(QIODevice::ReadOnly), "pomo-stats: read for clamp");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        QJsonObject root = doc.object();
        root.insert(QStringLiteral("completedCount"), -3);
        root.insert(QStringLiteral("focusedMinutes"), -9);
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "pomo-stats: write negatives");
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }

    {
        DataStore store;
        expect(store.load(), "pomo-stats: load negatives");
        expect(store.pomodoroStats().completedCount == 0, "pomo-stats: negative count -> 0");
        expect(store.pomodoroStats().focusedMinutes == 0, "pomo-stats: negative minutes -> 0");
    }

    if (g_failed == before)
        pass("pomodoro stats opt-in persist + isolate from todos");
}

void testPomodoroStatsCorruptionRecovery()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "pomo-stats-bak: data dir");
    if (dataDir.isEmpty())
        return;

    const QString statsFile = QDir(dataDir).filePath(QStringLiteral("pomodoro_stats.json"));
    const QString bakFile = statsFile + QStringLiteral(".bak");

    {
        DataStore store;
        expect(store.load(), "pomo-stats-bak: load");
        store.setPomodoroStatsEnabled(true);
        store.recordPomodoroCompleted(25);
        expect(store.flush(), "pomo-stats-bak: flush");
        expect(QFile::copy(statsFile, bakFile), "pomo-stats-bak: seed bak");
    }

    {
        QFile file(statsFile);
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "pomo-stats-bak: corrupt");
        file.write("not-json{{{");
        file.close();
    }

    {
        DataStore store;
        expect(store.load(), "pomo-stats-bak: load corrupt");
        expect(store.pomodoroStats().completedCount == 1, "pomo-stats-bak: restored count");
        expect(store.pomodoroStats().focusedMinutes == 25, "pomo-stats-bak: restored minutes");
    }

    if (g_failed == before)
        pass("pomodoro stats recover from .bak");
}

void testReminderHelpers()
{
    const int before = g_failed;
    expect(clampReminderInterval(0) == 1, "rem: clamp low");
    expect(clampReminderInterval(60) == 60, "rem: clamp 60");
    expect(clampReminderInterval(99999) == kReminderMaxMinutes, "rem: clamp high");
    expect(formatReminderInterval(1) == QStringLiteral("每 1 分钟"), "rem: format minute");
    expect(formatReminderInterval(90) == QStringLiteral("每 90 分钟"), "rem: format 90");
    expect(formatReminderInterval(60) == QStringLiteral("每 1 小时"), "rem: format hour");

    ReminderItem item;
    item.text = QStringLiteral("喝水");
    item.intervalMinutes = 60;
    item.enabled = true;
    const QDateTime t0(QDate(2026, 11, 2), QTime(12, 0, 0));
    item.createdAt = t0;
    item.lastFiredAt = t0;
    expect(reminderNextDue(item) == t0.addSecs(3600), "rem: next due +1h");
    expect(!reminderIsDue(item, t0), "rem: not due at create");
    expect(!reminderIsDue(item, t0.addSecs(59 * 60)), "rem: not due at 59m");
    expect(reminderIsDue(item, t0.addSecs(60 * 60)), "rem: due at 60m");
    expect(reminderIsDue(item, t0.addSecs(90 * 60)), "rem: still due after miss");

    item.enabled = false;
    expect(!reminderIsDue(item, t0.addSecs(7200)), "rem: disabled never due");
    item.enabled = true;
    item.text.clear();
    expect(!reminderIsDue(item, t0.addSecs(7200)), "rem: empty text not due");

    bool ok = false;
    const ReminderItem parsed = ReminderItem::fromJson(item.toJson(), &ok);
    expect(!ok, "rem: empty text json invalid");
    Q_UNUSED(parsed);

    ReminderItem valid;
    valid.id = QStringLiteral("r1");
    valid.text = QStringLiteral("站起来");
    valid.intervalMinutes = 30;
    valid.enabled = true;
    valid.createdAt = t0;
    valid.updatedAt = t0;
    valid.lastFiredAt = t0;
    bool validOk = false;
    const ReminderItem round = ReminderItem::fromJson(valid.toJson(), &validOk);
    expect(validOk, "rem: json ok");
    expect(round.text == QStringLiteral("站起来"), "rem: json text");
    expect(round.intervalMinutes == 30, "rem: json interval");
    expect(round.enabled, "rem: json enabled");

    QJsonObject badInterval = valid.toJson();
    badInterval.insert(QStringLiteral("intervalMinutes"), 0);
    bool badOk = true;
    ReminderItem::fromJson(badInterval, &badOk);
    expect(!badOk, "rem: interval 0 rejected");

    if (g_failed == before)
        pass("reminder helpers (due / disable / json)");
}

void testReminderStorePersistAndDisable()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "rem-store: data dir");
    if (dataDir.isEmpty())
        return;

    const QString remindersFile = QDir(dataDir).filePath(QStringLiteral("reminders.json"));
    const QString todosFile = QDir(dataDir).filePath(QStringLiteral("todos.json"));
    QString keepId;
    QString todoId;
    {
        DataStore store;
        expect(store.load(), "rem-store: load");
        todoId = store.addTodo(QStringLiteral("unrelated"), QDate::currentDate());
        expect(!todoId.isEmpty(), "rem-store: seed todo");
        expect(store.flush(), "rem-store: flush todo");

        expect(store.addReminder(QString(), 60).isEmpty(), "rem-store: empty text rejected");
        expect(store.addReminder(QStringLiteral("喝水"), 0).isEmpty(), "rem-store: interval 0 rejected");
        keepId = store.addReminder(QStringLiteral("喝水"), 60);
        expect(!keepId.isEmpty(), "rem-store: add");
        expect(store.reminders().size() == 1, "rem-store: one reminder");
        expect(store.findReminder(keepId) != nullptr, "rem-store: find");
        expect(store.findReminder(keepId)->enabled, "rem-store: enabled by default");
        expect(store.findReminder(keepId)->intervalMinutes == 60, "rem-store: interval");
        expect(!reminderIsDue(*store.findReminder(keepId), QDateTime::currentDateTime()),
               "rem-store: not due immediately after add");
        expect(QFile::exists(remindersFile), "rem-store: reminders.json written");
        expect(store.flush(), "rem-store: flush");
    }

    {
        QFile todoFile(todosFile);
        expect(todoFile.open(QIODevice::ReadOnly), "rem-store: reread todos");
        const QJsonDocument doc = QJsonDocument::fromJson(todoFile.readAll());
        todoFile.close();
        const QJsonObject root = doc.object();
        expect(!root.contains(QStringLiteral("reminders")), "rem-store: todos.json has no reminders key");
        const QJsonArray todos = root.value(QStringLiteral("todos")).toArray();
        expect(todos.size() == 1, "rem-store: todo untouched count");
        expect(!todos.at(0).toObject().contains(QStringLiteral("intervalMinutes")),
               "rem-store: todo item has no reminder fields");
    }

    {
        DataStore store;
        expect(store.load(), "rem-store: reload");
        expect(store.reminders().size() == 1, "rem-store: persisted count");
        const ReminderItem *item = store.findReminder(keepId);
        expect(item != nullptr && item->text == QStringLiteral("喝水"), "rem-store: text kept");
        expect(item && item->enabled, "rem-store: still enabled");
        expect(store.findTodo(todoId) != nullptr, "rem-store: todo still there");
        expect(store.setReminderEnabled(keepId, false), "rem-store: disable");
        expect(store.findReminder(keepId) && !store.findReminder(keepId)->enabled,
               "rem-store: disabled in memory");
        expect(store.flush(), "rem-store: flush disable");
    }

    {
        DataStore store;
        expect(store.load(), "rem-store: reload disabled");
        const ReminderItem *item = store.findReminder(keepId);
        expect(item != nullptr && !item->enabled, "rem-store: disabled persisted");
        ReminderItem overdue = *item;
        overdue.lastFiredAt = QDateTime::currentDateTime().addSecs(-7200);
        expect(!reminderIsDue(overdue, QDateTime::currentDateTime()),
               "rem-store: disabled overdue does not fire");
        expect(store.setReminderEnabled(keepId, true), "rem-store: re-enable");
        const ReminderItem *on = store.findReminder(keepId);
        expect(on && on->enabled, "rem-store: enabled again");
        expect(on && !reminderIsDue(*on, QDateTime::currentDateTime()),
               "rem-store: re-enable resets lastFired, not immediately due");
        expect(store.removeReminder(keepId), "rem-store: remove");
        expect(store.findReminder(keepId) == nullptr, "rem-store: gone");
        expect(store.flush(), "rem-store: flush remove");
    }

    {
        DataStore store;
        expect(store.load(), "rem-store: reload after remove");
        expect(store.reminders().isEmpty(), "rem-store: empty after remove");
        expect(store.findTodo(todoId) != nullptr, "rem-store: todo survives reminder delete");
    }

    if (g_failed == before)
        pass("reminder persist / disable / isolate from todos");
}

void testReminderDueThenMarkFired()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "rem-due: data dir");
    if (dataDir.isEmpty())
        return;

    const QDateTime t0 = QDateTime::currentDateTime();
    QString id;
    {
        DataStore store;
        expect(store.load(), "rem-due: load");
        QJsonObject root;
        root.insert(QStringLiteral("schemaVersion"), 1);
        QJsonObject row;
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("text"), QStringLiteral("喝水"));
        row.insert(QStringLiteral("intervalMinutes"), 1);
        row.insert(QStringLiteral("enabled"), true);
        row.insert(QStringLiteral("lastFiredAt"), t0.addSecs(-120).toString(Qt::ISODateWithMs));
        row.insert(QStringLiteral("createdAt"), t0.addSecs(-600).toString(Qt::ISODateWithMs));
        row.insert(QStringLiteral("updatedAt"), t0.addSecs(-120).toString(Qt::ISODateWithMs));
        root.insert(QStringLiteral("reminders"), QJsonArray{row});
        QFile file(store.remindersPath());
        expect(QDir().mkpath(dataDir), "rem-due: mkpath");
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "rem-due: write seed");
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }

    {
        DataStore store;
        expect(store.load(), "rem-due: load overdue");
        const ReminderItem *item = store.findReminder(id);
        expect(item != nullptr, "rem-due: loaded");
        expect(item && reminderIsDue(*item, t0), "rem-due: overdue is due");
        expect(store.markReminderFired(id, t0), "rem-due: mark fired");
        const ReminderItem *after = store.findReminder(id);
        expect(after && !reminderIsDue(*after, t0), "rem-due: not due after fire");
        expect(after && reminderIsDue(*after, t0.addSecs(60)), "rem-due: due again after interval");
        expect(store.flush(), "rem-due: flush");
    }

    {
        DataStore store;
        expect(store.load(), "rem-due: reload after fire");
        const ReminderItem *item = store.findReminder(id);
        expect(item != nullptr, "rem-due: still there");
        expect(item && !reminderIsDue(*item, t0), "rem-due: lastFired persisted");
        expect(store.findTodo(QStringLiteral("missing")) == nullptr, "rem-due: no stray todo");
    }

    if (g_failed == before)
        pass("reminder due + markFired advances lastFired");
}

void testReminderCorruptionRecovery()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "rem-bak: data dir");
    if (dataDir.isEmpty())
        return;

    QString keepId;
    {
        DataStore store;
        expect(store.load(), "rem-bak: load");
        keepId = store.addReminder(QStringLiteral("keep-rem"), 15);
        expect(!keepId.isEmpty(), "rem-bak: seed");
        expect(store.flush(), "rem-bak: first flush");
        expect(store.setReminderEnabled(keepId, false), "rem-bak: touch for bak");
        expect(store.flush(), "rem-bak: second flush");
        expect(QFile::exists(store.remindersPath() + QStringLiteral(".bak")),
               "rem-bak: .bak exists");
        expect(store.setReminderEnabled(keepId, true), "rem-bak: enable latest");
        expect(store.flush(), "rem-bak: third flush");
    }

    {
        QFile file(QDir(dataDir).filePath(QStringLiteral("reminders.json")));
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "rem-bak: corrupt");
        file.write("{ not-json");
        file.close();
    }

    {
        DataStore store;
        expect(store.load(), "rem-bak: load after corrupt");
        expect(store.reminders().size() == 1, "rem-bak: recovered one");
        const ReminderItem *item = store.findReminder(keepId);
        expect(item != nullptr && item->text == QStringLiteral("keep-rem"),
               "rem-bak: text from backup");
        expect(!store.takeNotices().isEmpty(), "rem-bak: notices");
    }

    if (g_failed == before)
        pass("reminder corruption recovery from .bak");
}

void testReminderCapAndSkipInvalid()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "rem-cap: data dir");
    if (dataDir.isEmpty())
        return;

    {
        DataStore store;
        expect(store.load(), "rem-cap: load");
        QJsonArray array;
        for (int i = 0; i < kReminderMaxCount + 3; ++i) {
            QJsonObject row;
            row.insert(QStringLiteral("id"), QStringLiteral("cap-%1").arg(i));
            row.insert(QStringLiteral("text"), QStringLiteral("n%1").arg(i));
            row.insert(QStringLiteral("intervalMinutes"), 10);
            row.insert(QStringLiteral("enabled"), true);
            row.insert(QStringLiteral("lastFiredAt"),
                       QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
            row.insert(QStringLiteral("createdAt"),
                       QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
            array.append(row);
        }
        QJsonObject skip;
        skip.insert(QStringLiteral("id"), QStringLiteral("bad"));
        skip.insert(QStringLiteral("text"), QString());
        skip.insert(QStringLiteral("intervalMinutes"), 10);
        array.append(skip);
        QJsonObject root;
        root.insert(QStringLiteral("schemaVersion"), 1);
        root.insert(QStringLiteral("reminders"), array);
        QFile file(store.remindersPath());
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "rem-cap: write");
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }

    {
        DataStore store;
        expect(store.load(), "rem-cap: reload");
        expect(store.reminders().size() == kReminderMaxCount, "rem-cap: capped");
        expect(store.addReminder(QStringLiteral("overflow"), 5).isEmpty(), "rem-cap: add rejected at cap");
    }

    if (g_failed == before)
        pass("reminder cap + skip invalid rows");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QDir().mkpath(testRoot());

    testRoundTrip();
    testCorruptionRecovery();
    testImmediatePersistAndIsolation();
    testRetentionPurge();
    testDailySnapshots();
    testWeeklyMonthlySnapshots();
    testArchiveExpired();
    testArchiveReimport();
    testTrayHideHintPersist();
    testPomodoroHelpers();
    testPomodoroDoesNotTouchTodos();
    testPomodoroMinutesPersist();
    testPomodoroStatsOptInPersist();
    testPomodoroStatsCorruptionRecovery();
    testReminderHelpers();
    testReminderStorePersistAndDisable();
    testReminderDueThenMarkFired();
    testReminderCorruptionRecovery();
    testReminderCapAndSkipInvalid();

    if (g_failed != 0) {
        std::fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    std::fprintf(stdout, "ALL GREEN\n");
    return 0;
}
