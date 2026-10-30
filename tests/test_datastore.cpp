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

    if (g_failed != 0) {
        std::fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    std::fprintf(stdout, "ALL GREEN\n");
    return 0;
}
