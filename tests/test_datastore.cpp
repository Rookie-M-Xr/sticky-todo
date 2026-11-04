#include "datastore.h"
#include "theme.h"

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

bool writeRawFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

QByteArray readRawFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool writeTodosList(const QString &path, const QString &id, const QString &text)
{
    QJsonObject item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("text"), text);
    item.insert(QStringLiteral("createdAt"),
                QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    item.insert(QStringLiteral("updatedAt"), item.value(QStringLiteral("createdAt")));
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("todos"), QJsonArray{item});
    return writeRawFile(path, QJsonDocument(root).toJson(QJsonDocument::Indented));
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
    const QString keepId = QStringLiteral("keep-id-107");
    const QString bakText = QStringLiteral("keep-me-from-bak");
    const QByteArray corrupt = QByteArrayLiteral("{ this is not valid json !!!");
    expect(writeTodosList(bakPath, keepId, bakText), "corrupt: seed readable bak");
    expect(writeRawFile(todosPath, corrupt), "corrupt: write broken primary");

    {
        DataStore store;
        expect(store.load(), "corrupt: load after corruption");
        expect(store.todos().isEmpty(), "corrupt: no silent restore");
        expect(store.todosLoadFailed(), "corrupt: load marked failed");
        expect(store.todoBackupAvailable(), "corrupt: readable bak offered");
        expect(readRawFile(todosPath) == corrupt, "corrupt: primary not overwritten on load");
        expect(store.addTodo(QStringLiteral("should-not-write"), QDate()).isEmpty(),
               "corrupt: add blocked while failed");
        expect(readRawFile(todosPath) == corrupt, "corrupt: add did not wipe primary");
        expect(!store.takeNotices().isEmpty(), "corrupt: failure notice");

        expect(store.restoreTodosFromBackup(), "corrupt: confirm restore");
        expect(!store.todosLoadFailed(), "corrupt: failed flag cleared");
        expect(!store.todoBackupAvailable(), "corrupt: offer consumed");
        expect(store.todos().size() == 1, "corrupt: recovered one todo");
        const TodoItem *item = store.findTodo(keepId);
        expect(item != nullptr && item->text == bakText, "corrupt: list matches bak");
        expect(store.flush(), "corrupt: flush after recovery");
    }

    {
        DataStore store;
        expect(store.load(), "corrupt: reload after restore");
        expect(!store.todosLoadFailed(), "corrupt: reload healthy");
        expect(store.todos().size() == 1, "corrupt: restored list persisted");
        const TodoItem *item = store.findTodo(keepId);
        expect(item != nullptr && item->text == bakText, "corrupt: persisted text matches bak");
    }

    if (g_failed == before)
        pass("corrupt primary + good bak: opt-in restore matches bak");
}

void testCorruptPrimaryWithoutBackup()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "no-bak: data dir");
    if (dataDir.isEmpty())
        return;

    const QString todosPath = QDir(dataDir).filePath(QStringLiteral("todos.json"));
    const QByteArray corrupt = QByteArrayLiteral("not-json-at-all");
    expect(writeRawFile(todosPath, corrupt), "no-bak: write broken primary");

    DataStore store;
    expect(store.load(), "no-bak: load");
    expect(store.todos().isEmpty(), "no-bak: empty in memory");
    expect(store.todosLoadFailed(), "no-bak: explicit failure");
    expect(!store.todoBackupAvailable(), "no-bak: no restore offer");
    expect(readRawFile(todosPath) == corrupt, "no-bak: primary kept");
    expect(store.addTodo(QStringLiteral("ghost"), QDate()).isEmpty(), "no-bak: write blocked");
    expect(store.flush(), "no-bak: flush without wiping");
    expect(readRawFile(todosPath) == corrupt, "no-bak: flush did not write empty success");
    expect(!store.takeNotices().isEmpty(), "no-bak: failure notice");

    if (g_failed == before)
        pass("corrupt primary + no bak: explicit failure, no silent empty write");
}

void testCorruptPrimaryBadBackup()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "bad-bak: data dir");
    if (dataDir.isEmpty())
        return;

    const QString todosPath = QDir(dataDir).filePath(QStringLiteral("todos.json"));
    const QString bakPath = todosPath + QStringLiteral(".bak");
    const QByteArray corrupt = QByteArrayLiteral("{ broken primary");
    expect(writeRawFile(todosPath, corrupt), "bad-bak: write broken primary");
    expect(writeRawFile(bakPath, QByteArrayLiteral("{ broken bak too")), "bad-bak: write broken bak");

    DataStore store;
    expect(store.load(), "bad-bak: load");
    expect(store.todos().isEmpty(), "bad-bak: empty in memory");
    expect(store.todosLoadFailed(), "bad-bak: explicit failure");
    expect(!store.todoBackupAvailable(), "bad-bak: unreadable bak is not offered");
    expect(!store.restoreTodosFromBackup(), "bad-bak: restore refused");
    expect(readRawFile(todosPath) == corrupt, "bad-bak: primary kept");
    expect(store.flush(), "bad-bak: flush");
    expect(readRawFile(todosPath) == corrupt, "bad-bak: no silent empty write");

    if (g_failed == before)
        pass("corrupt primary + bad bak: explicit failure, no silent empty write");
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

void testArchiveCurrentItem()
{
    const int before = g_failed;

    {
        const QString dataDir = makeDataDir();
        expect(!dataDir.isEmpty(), "row-archive: data dir");
        if (dataDir.isEmpty())
            return;

        DataStore store;
        expect(store.load(), "row-archive: load");
        const QDate due = QDate::currentDate().addDays(2);
        const QString openId = store.addTodo(QStringLiteral("hide-for-now"), due);
        const QString stayId = store.addTodo(QStringLiteral("stay-on-list"), QDate());
        expect(!openId.isEmpty() && !stayId.isEmpty(), "row-archive: seed ids");
        expect(!store.archiveTodo(QString()), "row-archive: empty id no-op");
        expect(!store.archiveTodo(QStringLiteral("missing-id")), "row-archive: missing id no-op");
        expect(store.archiveTodo(openId), "row-archive: archive open item");
        expect(store.findTodo(openId) == nullptr, "row-archive: gone from main");
        expect(store.findTodo(stayId) != nullptr, "row-archive: other stays");
        const QVector<ArchivedEntry> listed = store.listArchived();
        expect(listed.size() == 1, "row-archive: one in archive");
        if (!listed.isEmpty()) {
            expect(listed.at(0).item.id == openId, "row-archive: id intact");
            expect(listed.at(0).item.text == QStringLiteral("hide-for-now"), "row-archive: text intact");
            expect(listed.at(0).item.dueDate == due, "row-archive: due intact");
            expect(!listed.at(0).item.completed, "row-archive: still incomplete");
            expect(!listed.at(0).archivedAt.isEmpty(), "row-archive: archivedAt set");
        }
        expect(store.importAllArchived() == 1, "row-archive: restore");
        const TodoItem *back = store.findTodo(openId);
        expect(back != nullptr, "row-archive: back with same id");
        if (back) {
            expect(back->text == QStringLiteral("hide-for-now"), "row-archive: restored text");
            expect(back->dueDate == due, "row-archive: restored due");
            expect(!back->completed, "row-archive: restored still open");
        }
        expect(store.listArchived().isEmpty(), "row-archive: archive emptied");
    }

    {
        const QString dataDir = makeDataDir();
        expect(!dataDir.isEmpty(), "row-archive-done: data dir");
        if (dataDir.isEmpty())
            return;

        DataStore store;
        expect(store.load(), "row-archive-done: load");
        TodoItem done = makeCompleted(QStringLiteral("done-hide"), 3);
        const QString doneId = done.id;
        TodoItem openItem;
        openItem.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        openItem.text = QStringLiteral("open-keep");
        openItem.createdAt = QDateTime::currentDateTime();
        openItem.updatedAt = openItem.createdAt;
        expect(store.restoreTodo(done), "row-archive-done: seed done");
        expect(store.restoreTodo(openItem), "row-archive-done: seed open");
        expect(store.archiveTodo(doneId), "row-archive-done: archive completed");
        expect(store.findTodo(doneId) == nullptr, "row-archive-done: done gone");
        expect(store.findTodo(openItem.id) != nullptr, "row-archive-done: open stays");
        expect(store.archiveCompletedOlderThan(30) == 0, "row-archive-done: open not batch-archived");
        expect(store.findTodo(openItem.id) != nullptr, "row-archive-done: open still live");
        const QVector<ArchivedEntry> listed = store.listArchived();
        expect(listed.size() == 1, "row-archive-done: only the row");
        const int restoreIdx = listed.isEmpty() ? -1 : listed.at(0).index;
        expect(store.importArchived(QVector<int>{restoreIdx}) == 1,
               "row-archive-done: pick restore");
        const TodoItem *back = store.findTodo(doneId);
        expect(back != nullptr && back->completed, "row-archive-done: completed restored");
        expect(store.listArchived().isEmpty(), "row-archive-done: archive emptied");
    }

    if (g_failed == before)
        pass("archive current item + restore");
}

void testPinGrouping()
{
    const int before = g_failed;
    const QDate today(2026, 11, 2);
    const QDateTime t0(today, QTime(8, 0));

    auto makeOpen = [&](const QString &id, const QString &text, QDate due, int createdOffset) {
        TodoItem item;
        item.id = id;
        item.text = text;
        item.dueDate = due;
        item.createdAt = t0.addSecs(createdOffset);
        item.updatedAt = item.createdAt;
        return item;
    };

    TodoItem later = makeOpen(QStringLiteral("later-1"), QStringLiteral("later-work"),
                              today.addDays(20), 0);
    TodoItem todayItem = makeOpen(QStringLiteral("today-1"), QStringLiteral("today-work"),
                                  today, 10);
    TodoItem nodate = makeOpen(QStringLiteral("nodate-1"), QStringLiteral("inbox"), QDate(), 20);

    QVector<TodoGroup> groups = buildGroups({later, todayItem, nodate}, today);
    expect(!groups.isEmpty() && groups.at(0).id != GroupId::Pinned,
           "pin-group: no pin group before pin");
    bool laterInLater = false;
    for (const TodoGroup &group : groups) {
        if (group.id != GroupId::Later)
            continue;
        for (const TodoItem &item : group.items) {
            if (item.id == later.id)
                laterInLater = true;
        }
    }
    expect(laterInLater, "pin-group: unpinned later item in Later");

    later.pinnedAt = t0.addSecs(100);
    groups = buildGroups({later, todayItem, nodate}, today);
    expect(!groups.isEmpty() && groups.at(0).id == GroupId::Pinned,
           "pin-group: pinned group first");
    expect(groups.at(0).items.size() == 1, "pin-group: one pinned item");
    expect(groups.at(0).pendingCount == 1, "pin-group: pending 1");
    expect(groups.at(0).items.at(0).id == later.id, "pin-group: later item lifted");
    laterInLater = false;
    for (const TodoGroup &group : groups) {
        expect(group.id != GroupId::Pinned || group.items.size() == 1,
               "pin-group: only the pin section holds the pin");
        if (group.id != GroupId::Later)
            continue;
        for (const TodoItem &item : group.items) {
            if (item.id == later.id)
                laterInLater = true;
        }
    }
    expect(!laterInLater, "pin-group: pinned later item left Later");

    TodoItem later2 = makeOpen(QStringLiteral("later-2"), QStringLiteral("later-2"),
                               today.addDays(21), 5);
    later2.pinnedAt = t0.addSecs(200); // newer pin
    groups = buildGroups({later, later2, todayItem}, today);
    expect(groups.at(0).id == GroupId::Pinned && groups.at(0).items.size() == 2,
           "pin-group: two pins");
    expect(groups.at(0).items.at(0).id == later2.id, "pin-group: newest pin first");
    expect(groups.at(0).items.at(1).id == later.id, "pin-group: older pin second");

    TodoItem done = later;
    done.completed = true;
    done.completedAt = t0.addSecs(300);
    groups = buildGroups({done, todayItem}, today);
    bool sawPinGroup = false;
    bool foundDone = false;
    GroupId doneGroup = GroupId::Today;
    for (const TodoGroup &group : groups) {
        if (group.id == GroupId::Pinned)
            sawPinGroup = true;
        for (const TodoItem &item : group.items) {
            if (item.id != done.id)
                continue;
            foundDone = true;
            doneGroup = group.id;
        }
    }
    expect(!sawPinGroup, "pin-group: completed pin is not in pin group");
    expect(foundDone && doneGroup == GroupId::PastCompleted,
           "pin-group: completed pin joins 已完成, not the timeline");

    later.pinnedAt = QDateTime();
    groups = buildGroups({later, todayItem}, today);
    bool backInLater = false;
    for (const TodoGroup &group : groups) {
        expect(group.id != GroupId::Pinned, "pin-group: unpin drops pin group");
        if (group.id != GroupId::Later)
            continue;
        for (const TodoItem &item : group.items) {
            if (item.id == later.id)
                backInLater = true;
        }
    }
    expect(backInLater, "pin-group: unpinned later item returns to Later");

    if (g_failed == before)
        pass("pin grouping (newest first + unpin restores timeline)");
}

void testPinPersist()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "pin-store: data dir");
    if (dataDir.isEmpty())
        return;

    const QDate today = QDate::currentDate();
    QString pinId;
    QString otherId;
    QDateTime pinStamp;

    {
        DataStore store;
        expect(store.load(), "pin-store: load");
        pinId = store.addTodo(QStringLiteral("pin-me"), today.addDays(20));
        otherId = store.addTodo(QStringLiteral("leave-me"), today);
        expect(!pinId.isEmpty() && !otherId.isEmpty(), "pin-store: seed ids");
        expect(!store.setTodoPinned(QString(), true), "pin-store: empty id no-op");
        expect(!store.setTodoPinned(QStringLiteral("missing-id"), true),
               "pin-store: missing id no-op");
        expect(store.setTodoPinned(pinId, true), "pin-store: pin");
        const TodoItem *pinned = store.findTodo(pinId);
        expect(pinned != nullptr && pinned->isPinned(), "pin-store: in-memory pinned");
        expect(store.findTodo(otherId) != nullptr && !store.findTodo(otherId)->isPinned(),
               "pin-store: other stays unpinned");
        pinStamp = pinned->pinnedAt;
        expect(store.setTodoPinned(pinId, true), "pin-store: re-pin is no-op");
        expect(store.findTodo(pinId)->pinnedAt == pinStamp, "pin-store: pin time stable");
        expect(store.flush(), "pin-store: flush after pin");

        QFile file(store.todosPath());
        expect(file.open(QIODevice::ReadOnly), "pin-store: open todos.json");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        bool sawPinnedAt = false;
        bool sawEmptyOther = false;
        for (const QJsonValue &value : doc.object().value(QStringLiteral("todos")).toArray()) {
            const QJsonObject row = value.toObject();
            if (row.value(QStringLiteral("id")).toString() == pinId) {
                sawPinnedAt = !row.value(QStringLiteral("pinnedAt")).toString().isEmpty();
            } else if (row.value(QStringLiteral("id")).toString() == otherId) {
                sawEmptyOther = row.value(QStringLiteral("pinnedAt")).toString().isEmpty();
            }
        }
        expect(sawPinnedAt, "pin-store: pinnedAt written");
        expect(sawEmptyOther, "pin-store: other pinnedAt empty");
    }

    {
        DataStore store;
        expect(store.load(), "pin-store: reload");
        const TodoItem *pinned = store.findTodo(pinId);
        expect(pinned != nullptr && pinned->isPinned(), "pin-store: restart keeps pin");
        expect(pinned->pinnedAt == pinStamp, "pin-store: pin time survives restart");
        expect(store.setTodoPinned(pinId, false), "pin-store: unpin");
        expect(!store.findTodo(pinId)->isPinned(), "pin-store: unpinned in memory");
        expect(store.flush(), "pin-store: flush after unpin");
    }

    {
        DataStore store;
        expect(store.load(), "pin-store: reload after unpin");
        expect(store.findTodo(pinId) != nullptr && !store.findTodo(pinId)->isPinned(),
               "pin-store: restart keeps unpin");
        expect(store.setTodoPinned(pinId, true), "pin-store: pin again");
        pinStamp = store.findTodo(pinId)->pinnedAt;
        expect(store.archiveTodo(pinId), "pin-store: archive pinned item");
        expect(store.findTodo(pinId) == nullptr, "pin-store: gone from main");
        expect(store.importAllArchived() == 1, "pin-store: restore from archive");
        const TodoItem *back = store.findTodo(pinId);
        expect(back != nullptr && back->isPinned() && back->pinnedAt == pinStamp,
               "pin-store: archive round-trip keeps pin");
        expect(store.flush(), "pin-store: flush restored pin");
    }

    {
        const QString todosPath = QDir(dataDir).filePath(QStringLiteral("todos.json"));
        QFile file(todosPath);
        expect(file.open(QIODevice::ReadOnly), "pin-store: read for legacy");
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QJsonArray todos = root.value(QStringLiteral("todos")).toArray();
        expect(!todos.isEmpty(), "pin-store: todos present");
        QJsonObject first = todos.at(0).toObject();
        first.remove(QStringLiteral("pinnedAt"));
        todos.replace(0, first);
        root.insert(QStringLiteral("todos"), todos);
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
               "pin-store: rewrite without pinnedAt");
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();

        DataStore store;
        expect(store.load(), "pin-store: load legacy");
        const QString legacyId = first.value(QStringLiteral("id")).toString();
        const TodoItem *legacy = store.findTodo(legacyId);
        expect(legacy != nullptr && !legacy->isPinned(),
               "pin-store: missing pinnedAt is unpinned");
    }

    bool jsonOk = false;
    QJsonObject legacyObj;
    legacyObj.insert(QStringLiteral("id"), QStringLiteral("legacy-1"));
    legacyObj.insert(QStringLiteral("text"), QStringLiteral("old-row"));
    legacyObj.insert(QStringLiteral("createdAt"),
                     QDateTime(today, QTime(9, 0)).toString(Qt::ISODateWithMs));
    legacyObj.insert(QStringLiteral("completed"), false);
    const TodoItem parsed = TodoItem::fromJson(legacyObj, &jsonOk);
    expect(jsonOk && !parsed.isPinned(), "pin-json: fromJson without pinnedAt");

    TodoItem stamped;
    stamped.id = QStringLiteral("stamped");
    stamped.text = QStringLiteral("has-pin");
    stamped.createdAt = QDateTime(today, QTime(9, 0));
    stamped.updatedAt = stamped.createdAt;
    stamped.pinnedAt = QDateTime(today, QTime(10, 0));
    bool roundOk = false;
    const TodoItem round = TodoItem::fromJson(stamped.toJson(), &roundOk);
    expect(roundOk && round.isPinned() && round.pinnedAt == stamped.pinnedAt,
           "pin-json: toJson/fromJson pinnedAt");

    if (g_failed == before)
        pass("pin persist + archive round-trip + legacy");
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

    expect(pomodoroOnChipClick(QString(), false, QStringLiteral("a")) == PomodoroClickAction::Start,
           "pomo: idle chip starts");
    expect(pomodoroOnChipClick(QStringLiteral("a"), false, QStringLiteral("a"))
               == PomodoroClickAction::Pause,
           "pomo: running same id pauses");
    expect(pomodoroOnChipClick(QStringLiteral("a"), true, QStringLiteral("a"))
               == PomodoroClickAction::Resume,
           "pomo: paused same id resumes");
    expect(pomodoroOnChipClick(QStringLiteral("a"), false, QStringLiteral("b"))
               == PomodoroClickAction::Start,
           "pomo: running other id replaces");
    expect(pomodoroOnChipClick(QStringLiteral("a"), true, QStringLiteral("b"))
               == PomodoroClickAction::Ignore,
           "pomo: paused other id blocked");
    expect(pomodoroOnChipClick(QStringLiteral("a"), true, QString()) == PomodoroClickAction::Ignore,
           "pomo: empty request ignored");
    expect(pomodoroFrozenSeconds(90) == 90, "pomo: frozen 90");
    expect(pomodoroFrozenSeconds(0) == 0, "pomo: frozen 0");
    expect(pomodoroFrozenSeconds(-4) == 0, "pomo: frozen negative");
    expect(pomodoroResumeDeadline(90, now) == now.addSecs(90), "pomo: resume +90s");
    expect(!pomodoroResumeDeadline(0, now).isValid(), "pomo: resume 0 invalid");
    expect(!pomodoroResumeDeadline(-3, now).isValid(), "pomo: resume neg invalid");
    expect(pomodoroSessionSecondsLeft(now.addSecs(40), false, 99, now) == 40,
           "pomo: running uses deadline");
    expect(pomodoroSessionSecondsLeft(now.addSecs(40), true, 12, now) == 12,
           "pomo: paused uses frozen");
    expect(pomodoroSessionSecondsLeft(QDateTime(), true, 7, now) == 7,
           "pomo: paused ignores deadline");
    expect(pomodoroSessionSecondsLeft(now.addSecs(40), true, -2, now) == 0,
           "pomo: paused clamps neg");
    expect(formatPomodoroPauseHint(90) == QStringLiteral("暂停番茄钟  01:30"),
           "pomo: pause hint");
    expect(formatPomodoroResumeHint(9) == QStringLiteral("继续番茄钟  00:09"),
           "pomo: resume hint");
    {
        const int left = pomodoroFrozenSeconds(24 * 60);
        expect(!pomodoroCountsAsComplete(left), "pomo: paused remaining not complete");
        expect(pomodoroElapsedSeconds(25, left) == 60, "pomo: pause does not add elapsed");
        const QDateTime later = now.addSecs(600);
        expect(pomodoroSessionSecondsLeft(QDateTime(), true, left, later) == 24 * 60,
               "pomo: wall clock during pause ignored");
        const QDateTime resumed = pomodoroResumeDeadline(left, later);
        expect(pomodoroRemainingSeconds(resumed, later) == 24 * 60, "pomo: resume keeps remaining");
        expect(pomodoroRemainingSeconds(resumed, later.addSecs(15)) == 24 * 60 - 15,
               "pomo: after resume countdown continues");
    }

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
        pass("pomodoro helpers (format / remaining / mutex / pause / stats)");
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

void testPomodoroStatsClear()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "pomo-clear: data dir");
    if (dataDir.isEmpty())
        return;

    const QString statsFile = QDir(dataDir).filePath(QStringLiteral("pomodoro_stats.json"));
    const QString settingsFile = QDir(dataDir).filePath(QStringLiteral("settings.json"));

    {
        DataStore store;
        expect(store.load(), "pomo-clear: load");
        store.setPomodoroStatsEnabled(true);
        store.recordPomodoroCompleted(25);
        store.recordPomodoroInterrupted(90);
        expect(store.pomodoroStats().completedCount == 1, "pomo-clear: seeded count");
        expect(store.pomodoroStats().focusedMinutes == 27, "pomo-clear: seeded minutes");
        expect(store.flush(), "pomo-clear: flush seed");
        store.clearPomodoroStats();
        expect(store.pomodoroStats().completedCount == 0, "pomo-clear: count 0");
        expect(store.pomodoroStats().focusedMinutes == 0, "pomo-clear: minutes 0");
        expect(store.settings().pomodoroStatsEnabled, "pomo-clear: switch stays on");
        expect(store.flush(), "pomo-clear: flush after clear");
    }

    {
        QFile file(statsFile);
        expect(file.open(QIODevice::ReadOnly), "pomo-clear: read stats");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        const QJsonObject root = doc.object();
        expect(root.value(QStringLiteral("completedCount")).toInt() == 0,
               "pomo-clear: json count 0");
        expect(root.value(QStringLiteral("focusedMinutes")).toInt() == 0,
               "pomo-clear: json minutes 0");
    }

    {
        QFile file(settingsFile);
        expect(file.open(QIODevice::ReadOnly), "pomo-clear: read settings");
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        expect(doc.object().value(QStringLiteral("pomodoroStatsEnabled")).toBool() == true,
               "pomo-clear: settings flag still true");
    }

    {
        DataStore store;
        expect(store.load(), "pomo-clear: restart");
        expect(store.settings().pomodoroStatsEnabled, "pomo-clear: restart switch on");
        expect(store.pomodoroStats().completedCount == 0, "pomo-clear: restart count 0");
        expect(store.pomodoroStats().focusedMinutes == 0, "pomo-clear: restart minutes 0");
        store.setPomodoroStatsEnabled(false);
        store.recordPomodoroCompleted(50);
        expect(store.pomodoroStats().completedCount == 0, "pomo-clear: off no increment");
        store.setPomodoroStatsEnabled(true);
        store.recordPomodoroCompleted(25);
        expect(store.pomodoroStats().completedCount == 1, "pomo-clear: re-enable records");
        expect(store.pomodoroStats().focusedMinutes == 25, "pomo-clear: re-enable minutes");
        store.setPomodoroStatsEnabled(false);
        store.clearPomodoroStats();
        expect(store.pomodoroStats().completedCount == 0, "pomo-clear: clear while off");
        expect(store.pomodoroStats().focusedMinutes == 0, "pomo-clear: minutes while off");
        expect(!store.settings().pomodoroStatsEnabled, "pomo-clear: switch stays off");
        expect(store.flush(), "pomo-clear: flush after off clear");
    }

    {
        DataStore store;
        expect(store.load(), "pomo-clear: reload after off clear");
        expect(!store.settings().pomodoroStatsEnabled, "pomo-clear: still off");
        expect(store.pomodoroStats().completedCount == 0, "pomo-clear: still 0");
        expect(store.pomodoroStats().focusedMinutes == 0, "pomo-clear: duration still 0");
        store.clearPomodoroStats();
        expect(store.pomodoroStats().completedCount == 0, "pomo-clear: idempotent count");
        expect(store.pomodoroStats().focusedMinutes == 0, "pomo-clear: idempotent minutes");
        expect(!store.settings().pomodoroStatsEnabled, "pomo-clear: idempotent switch");
    }

    if (g_failed == before)
        pass("pomodoro stats clear persist + keep switch");
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
    expect(round.activeStartMinutes == 0, "rem: json default start all-day");
    expect(round.activeEndMinutes == kReminderDayMinutes, "rem: json default end all-day");
    expect(round.enabled, "rem: json enabled");

    QJsonObject badInterval = valid.toJson();
    badInterval.insert(QStringLiteral("intervalMinutes"), 0);
    bool badOk = true;
    ReminderItem::fromJson(badInterval, &badOk);
    expect(!badOk, "rem: interval 0 rejected");

    if (g_failed == before)
        pass("reminder helpers (due / disable / json)");
}

void testReminderSnooze()
{
    const int before = g_failed;
    expect(reminderSnoozeMinutesValid(kReminderSnoozeMinutes5), "snooze: 5 valid");
    expect(reminderSnoozeMinutesValid(kReminderSnoozeMinutes10), "snooze: 10 valid");
    expect(reminderSnoozeMinutesValid(kReminderSnoozeMinutes30), "snooze: 30 valid");
    expect(reminderSnoozeMinutesValid(1), "snooze: 1 custom valid");
    expect(reminderSnoozeMinutesValid(7), "snooze: 7 custom valid");
    expect(reminderSnoozeMinutesValid(20), "snooze: 20 custom valid");
    expect(reminderSnoozeMinutesValid(kReminderMaxMinutes), "snooze: max valid");
    expect(!reminderSnoozeMinutesValid(0), "snooze: 0 rejected");
    expect(!reminderSnoozeMinutesValid(-3), "snooze: negative rejected");
    expect(!reminderSnoozeMinutesValid(kReminderMaxMinutes + 1), "snooze: above max rejected");

    bool parseOk = false;
    expect(parseReminderSnoozeMinutes(QStringLiteral("7"), &parseOk) == 7 && parseOk,
           "snooze-parse: 7");
    expect(parseReminderSnoozeMinutes(QStringLiteral(" 7 "), &parseOk) == 7 && parseOk,
           "snooze-parse: padded");
    expect(parseReminderSnoozeMinutes(QStringLiteral("7分钟"), &parseOk) == 7 && parseOk,
           "snooze-parse: 分钟 suffix");
    expect(parseReminderSnoozeMinutes(QStringLiteral("07"), &parseOk) == 7 && parseOk,
           "snooze-parse: leading zero");
    expect(parseReminderSnoozeMinutes(QStringLiteral("30"), &parseOk) == 30 && parseOk,
           "snooze-parse: 30");
    expect(parseReminderSnoozeMinutes(QString(), &parseOk) == 0 && !parseOk,
           "snooze-parse: empty");
    expect(parseReminderSnoozeMinutes(QStringLiteral("0"), &parseOk) == 0 && !parseOk,
           "snooze-parse: 0");
    expect(parseReminderSnoozeMinutes(QStringLiteral("7.5"), &parseOk) == 0 && !parseOk,
           "snooze-parse: fraction");
    expect(parseReminderSnoozeMinutes(QStringLiteral("7abc"), &parseOk) == 0 && !parseOk,
           "snooze-parse: junk");
    expect(parseReminderSnoozeMinutes(QStringLiteral("abc"), &parseOk) == 0 && !parseOk,
           "snooze-parse: letters");
    expect(parseReminderSnoozeMinutes(QStringLiteral("10081"), &parseOk) == 0 && !parseOk,
           "snooze-parse: above max");

    ReminderItem item;
    item.text = QStringLiteral("喝水");
    item.intervalMinutes = 60;
    item.enabled = true;
    const QDateTime t0(QDate(2026, 11, 2), QTime(12, 0, 0));
    item.createdAt = t0;
    item.lastFiredAt = t0;

    expect(!reminderSnoozeUntil(t0, 0).isValid(), "snooze: invalid empty");
    const QDateTime snooze5 = reminderSnoozeUntil(t0, kReminderSnoozeMinutes5);
    const QDateTime snooze10 = reminderSnoozeUntil(t0, kReminderSnoozeMinutes10);
    const QDateTime snooze30 = reminderSnoozeUntil(t0, kReminderSnoozeMinutes30);
    const QDateTime snooze7 = reminderSnoozeUntil(t0, 7);
    expect(snooze5 == t0.addSecs(5 * 60), "snooze: +5 min");
    expect(snooze10 == t0.addSecs(10 * 60), "snooze: +10 min");
    expect(snooze30 == t0.addSecs(30 * 60), "snooze: +30 min");
    expect(snooze7 == t0.addSecs(7 * 60), "snooze: custom +7 min");
    expect(reminderNextDue(item) == t0.addSecs(3600), "snooze: regular next unchanged");
    expect(item.intervalMinutes == 60, "snooze: interval field untouched");
    expect(reminderNextDue(item, snooze5) == snooze5, "snooze: next due is override");
    expect(!reminderIsDue(item, t0.addSecs(4 * 60), snooze5), "snooze: quiet at +4");
    expect(reminderIsDue(item, t0.addSecs(5 * 60), snooze5), "snooze: due at +5");
    expect(!reminderIsDue(item, t0.addSecs(5 * 60)), "snooze: no override keeps interval");
    expect(!reminderIsDue(item, t0.addSecs(9 * 60), snooze10), "snooze: 10min quiet at +9");
    expect(reminderIsDue(item, t0.addSecs(10 * 60), snooze10), "snooze: 10min due at +10");
    expect(!reminderIsDue(item, t0.addSecs(6 * 60), snooze7), "snooze: custom quiet at +6");
    expect(reminderIsDue(item, t0.addSecs(7 * 60), snooze7), "snooze: custom due at +7");
    expect(!reminderIsDue(item, t0.addSecs(29 * 60), snooze30), "snooze: 30min quiet at +29");
    expect(reminderIsDue(item, t0.addSecs(30 * 60), snooze30), "snooze: 30min due at +30");

    item.intervalMinutes = 1;
    expect(reminderIsDue(item, t0.addSecs(60)), "snooze: 1min interval due without override");
    expect(!reminderIsDue(item, t0.addSecs(60), snooze5), "snooze: suppress short interval");
    expect(reminderIsDue(item, t0.addSecs(5 * 60), snooze5), "snooze: short interval fires at snooze");
    item.intervalMinutes = 60;
    expect(!reminderIsDue(item, t0.addSecs(5 * 60)), "snooze: after clear uses lastFired+interval");
    expect(reminderIsDue(item, t0.addSecs(60 * 60)), "snooze: original interval after clear");

    item.enabled = false;
    expect(!reminderIsDue(item, t0.addSecs(5 * 60), snooze5), "snooze: disabled never due");
    item.enabled = true;
    item.text.clear();
    expect(!reminderIsDue(item, t0.addSecs(5 * 60), snooze5), "snooze: deleted/empty text silent");
    item.text = QStringLiteral("喝水");

    item.activeStartMinutes = 9 * 60;
    item.activeEndMinutes = 18 * 60;
    const QDateTime evening(QDate(2026, 11, 2), QTime(20, 0));
    item.lastFiredAt = evening.addSecs(-3600);
    const QDateTime snoozeEve = reminderSnoozeUntil(evening, kReminderSnoozeMinutes5);
    expect(!reminderIsDue(item, evening.addSecs(5 * 60), snoozeEve),
           "snooze: outside window still silent");
    const QDateTime morning(QDate(2026, 11, 3), QTime(9, 0));
    expect(reminderIsDue(item, morning, snoozeEve), "snooze: fires when window opens");
    expect(item.intervalMinutes == 60, "snooze: interval still 60");
    expect(item.activeStartMinutes == 9 * 60 && item.activeEndMinutes == 18 * 60,
           "snooze: window fields untouched");

    const QJsonObject json = item.toJson();
    expect(!json.contains(QStringLiteral("snoozeUntil")), "snooze: not serialized");
    expect(json.value(QStringLiteral("intervalMinutes")).toInt() == 60, "snooze: json interval same");

    if (g_failed == before)
        pass("reminder in-process snooze (custom minutes + 5/10/30, disable, window)");
}

void testReminderDismissThisOccurrence()
{
    const int before = g_failed;
    ReminderItem item;
    item.text = QStringLiteral("喝水");
    item.intervalMinutes = 60;
    item.enabled = true;
    item.activeStartMinutes = 9 * 60;
    item.activeEndMinutes = 18 * 60;
    const QDateTime t0(QDate(2026, 11, 2), QTime(12, 0, 0));
    item.createdAt = t0;
    // After a fire, lastFired is already now. Dismiss = no snooze override.
    item.lastFiredAt = t0;

    const QDateTime noSnooze;
    expect(!reminderIsDue(item, t0, noSnooze), "dismiss: quiet at fire instant");
    expect(!reminderIsDue(item, t0.addSecs(59 * 60), noSnooze),
           "dismiss: quiet before next interval");
    expect(reminderIsDue(item, t0.addSecs(60 * 60), noSnooze),
           "dismiss: next cycle still fires");
    expect(item.enabled, "dismiss: still enabled");
    expect(item.intervalMinutes == 60, "dismiss: interval unchanged");
    expect(item.activeStartMinutes == 9 * 60 && item.activeEndMinutes == 18 * 60,
           "dismiss: window unchanged");
    expect(item.text == QStringLiteral("喝水"), "dismiss: text unchanged");

    const QJsonObject json = item.toJson();
    expect(json.value(QStringLiteral("enabled")).toBool(), "dismiss: json still enabled");
    expect(json.value(QStringLiteral("intervalMinutes")).toInt() == 60,
           "dismiss: json interval same");
    expect(!json.contains(QStringLiteral("snoozeUntil")), "dismiss: no snooze field");

    if (g_failed == before)
        pass("reminder dismiss this occurrence keeps schedule (#68)");
}

void testReminderFireLabel()
{
    const int before = g_failed;
    expect(reminderFireLabel(QString()) == QStringLiteral("定时提醒"),
           "fire-label: empty → fallback");
    expect(reminderFireLabel(QStringLiteral("   ")) == QStringLiteral("定时提醒"),
           "fire-label: whitespace → fallback");
    expect(reminderFireLabel(QStringLiteral("  喝水  ")) == QStringLiteral("喝水"),
           "fire-label: trim");

    const QString medium = QStringLiteral("去一楼会议室拿文件了");
    expect(medium.size() >= 10, "fire-label: medium fixture ≥10 chars");
    expect(reminderFireLabel(medium) == medium, "fire-label: medium kept whole");
    expect(!reminderFireLabel(medium).contains(QStringLiteral("提醒：")),
           "fire-label: no 提醒： prefix");
    expect(!reminderFireLabel(medium).contains(QStringLiteral("…")),
           "fire-label: no pre-elide ellipsis");

    const QString longText = QStringLiteral("去").repeated(80);
    expect(reminderFireLabel(longText) == longText, "fire-label: long not pre-elided");
    expect(reminderSnoozeMenuLabel() == QStringLiteral("延后 ▾"),
           "fire-label: snooze overflow menu");
    expect(reminderDismissLabel() == QStringLiteral("忽略"),
           "fire-label: dismiss this occurrence");
    expect(reminderSnoozePresetLabel(kReminderSnoozeMinutes30) == QStringLiteral("延后 30"),
           "fire-label: 30-min preset");
    expect(reminderCustomSnoozePlaceholder() == QStringLiteral("分钟"),
           "fire-label: custom minutes placeholder");

    if (g_failed == before)
        pass("reminder fire-time banner label (#77 + #68)");
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

void testReminderActiveWindowAndInterval()
{
    const int before = g_failed;
    const QDate day(2026, 11, 2);
    auto at = [&](int hour, int minute, int second = 0) {
        return QDateTime(day, QTime(hour, minute, second));
    };

    expect(reminderWindowValid(9 * 60, 18 * 60), "rem-win: 09-18 valid");
    expect(reminderWindowValid(0, kReminderDayMinutes), "rem-win: all-day valid");
    expect(reminderWindowValid(22 * 60, 6 * 60), "rem-win: 22-06 valid");
    expect(reminderWindowValid(18 * 60, 9 * 60), "rem-win: cross-midnight valid");
    expect(reminderWindowValid(22 * 60, 0), "rem-win: wrap to midnight valid");
    expect(reminderWindowValid(9 * 60, 9 * 60), "rem-win: equal times wrap all-day");
    expect(!reminderWindowValid(-1, 18 * 60), "rem-win: start below range");
    expect(!reminderWindowValid(kReminderDayMinutes, 6 * 60), "rem-win: start at 24:00 invalid");
    expect(!reminderWindowValid(9 * 60, -1), "rem-win: end below range");
    expect(!reminderWindowValid(9 * 60, kReminderDayMinutes + 1), "rem-win: end above range");
    expect(formatReminderTime(9 * 60) == QStringLiteral("09:00"), "rem-win: format 09:00");
    expect(formatReminderTime(kReminderDayMinutes) == QStringLiteral("24:00"), "rem-win: format 24:00");
    expect(formatReminderWindow(9 * 60, 18 * 60) == QStringLiteral("09:00–18:00"),
           "rem-win: format window");

    int parsed = -1;
    expect(parseReminderTime(QStringLiteral("09:00"), &parsed, false) && parsed == 9 * 60,
           "rem-win: parse 09:00");
    expect(parseReminderTime(QStringLiteral("24:00"), &parsed, true) && parsed == kReminderDayMinutes,
           "rem-win: parse 24:00 end");
    expect(!parseReminderTime(QStringLiteral("24:00"), &parsed, false), "rem-win: 24:00 not start");

    ReminderItem item;
    item.text = QStringLiteral("喝水");
    item.intervalMinutes = 60;
    item.activeStartMinutes = 9 * 60;
    item.activeEndMinutes = 18 * 60;
    item.enabled = true;
    item.createdAt = at(8, 0);
    item.lastFiredAt = at(8, 0);

    expect(!reminderInActiveWindow(item, at(8, 59, 59)), "rem-win: 08:59 outside");
    expect(reminderInActiveWindow(item, at(9, 0)), "rem-win: 09:00 inclusive");
    expect(reminderInActiveWindow(item, at(17, 59, 59)), "rem-win: 17:59 inside");
    expect(!reminderInActiveWindow(item, at(18, 0)), "rem-win: 18:00 exclusive");

    expect(!reminderIsDue(item, at(8, 59, 59)), "rem-win: no toast before window");
    expect(reminderIsDue(item, at(9, 0)), "rem-win: due at window open after interval");
    expect(!reminderIsDue(item, at(8, 0).addSecs(59 * 60)), "rem-win: still outside at 08:59");

    item.lastFiredAt = at(12, 0);
    expect(!reminderIsDue(item, at(12, 59)), "rem-win: not due before interval");
    expect(reminderIsDue(item, at(13, 0)), "rem-win: due after 60min inside");
    expect(reminderIsDue(item, at(17, 30)), "rem-win: still due after miss inside");
    expect(!reminderIsDue(item, at(18, 0)), "rem-win: no toast at exclusive end");
    expect(!reminderIsDue(item, at(20, 0)), "rem-win: no toast outside evening");

    item.lastFiredAt = at(16, 0);
    expect(reminderIsDue(item, at(17, 0)), "rem-win: 16:00+60 due at 17:00");
    expect(!reminderIsDue(item, at(18, 0)), "rem-win: overdue still gated by window");

    item.lastFiredAt = at(12, 0);
    item.intervalMinutes = 30;
    expect(!reminderIsDue(item, at(12, 29)), "rem-win: new 30min not yet");
    expect(reminderIsDue(item, at(12, 30)), "rem-win: new 30min due");
    item.intervalMinutes = 90;
    expect(!reminderIsDue(item, at(13, 0)), "rem-win: longer interval delays");
    expect(reminderIsDue(item, at(13, 30)), "rem-win: 90min due");

    ReminderItem overnight = item;
    overnight.intervalMinutes = 60;
    overnight.activeStartMinutes = 22 * 60;
    overnight.activeEndMinutes = 6 * 60;
    overnight.createdAt = at(20, 0);
    overnight.lastFiredAt = at(21, 0);
    expect(reminderWindowValid(overnight.activeStartMinutes, overnight.activeEndMinutes),
           "rem-win: overnight helper valid");
    expect(!reminderInActiveWindow(overnight, at(12, 0)), "rem-win: 12:00 outside overnight");
    expect(!reminderInActiveWindow(overnight, at(21, 59, 59)), "rem-win: 21:59 outside overnight");
    expect(reminderInActiveWindow(overnight, at(22, 0)), "rem-win: 22:00 inclusive");
    expect(reminderInActiveWindow(overnight, at(23, 30)), "rem-win: 23:30 inside");
    expect(reminderInActiveWindow(overnight, at(0, 0)), "rem-win: 00:00 inside overnight");
    expect(reminderInActiveWindow(overnight, at(1, 0)), "rem-win: 01:00 inside overnight");
    expect(reminderInActiveWindow(overnight, at(5, 59, 59)), "rem-win: 05:59 inside overnight");
    expect(!reminderInActiveWindow(overnight, at(6, 0)), "rem-win: 06:00 exclusive");
    overnight.lastFiredAt = at(11, 0);
    expect(!reminderIsDue(overnight, at(12, 0)), "rem-win: no toast at 12:xx");
    overnight.lastFiredAt = at(22, 0);
    expect(reminderIsDue(overnight, at(23, 0)), "rem-win: toast at 23:xx");
    overnight.lastFiredAt = at(0, 0);
    expect(reminderIsDue(overnight, at(1, 0)), "rem-win: toast at 01:xx");
    overnight.lastFiredAt = at(23, 0);
    expect(reminderIsDue(overnight, QDateTime(day.addDays(1), QTime(1, 0))),
           "rem-win: toast 01:xx next calendar day");
    overnight.lastFiredAt = at(21, 0);
    expect(!reminderIsDue(overnight, at(21, 30)), "rem-win: overdue still gated before window");
    expect(reminderIsDue(overnight, at(22, 0)), "rem-win: fires when overnight window opens");

    ReminderItem wrapEqual;
    wrapEqual.text = QStringLiteral("全天");
    wrapEqual.intervalMinutes = 60;
    wrapEqual.activeStartMinutes = 9 * 60;
    wrapEqual.activeEndMinutes = 9 * 60;
    wrapEqual.lastFiredAt = at(8, 0);
    expect(reminderInActiveWindow(wrapEqual, at(12, 0)), "rem-win: 09-09 covers noon");
    expect(reminderInActiveWindow(wrapEqual, at(3, 0)), "rem-win: 09-09 covers night");
    expect(reminderIsDue(wrapEqual, at(9, 0)), "rem-win: 09-09 due at start");

    ReminderItem allDay;
    allDay.text = QStringLiteral("旧");
    allDay.intervalMinutes = 60;
    allDay.lastFiredAt = at(22, 0);
    expect(allDay.activeStartMinutes == 0, "rem-win: default start 00:00");
    expect(allDay.activeEndMinutes == kReminderDayMinutes, "rem-win: default end 24:00");
    expect(reminderInActiveWindow(allDay, at(23, 30)), "rem-win: all-day covers night");
    expect(reminderIsDue(allDay, at(23, 0)), "rem-win: all-day due at night");

    bool windowOk = false;
    item.activeStartMinutes = 9 * 60;
    item.activeEndMinutes = 18 * 60;
    item.lastFiredAt = at(12, 0);
    item.createdAt = at(12, 0);
    item.updatedAt = at(12, 0);
    item.id = QStringLiteral("win-1");
    const ReminderItem written = ReminderItem::fromJson(item.toJson(), &windowOk);
    expect(windowOk, "rem-win: json window ok");
    expect(written.activeStartMinutes == 9 * 60, "rem-win: json start");
    expect(written.activeEndMinutes == 18 * 60, "rem-win: json end");

    QJsonObject overnightJson = item.toJson();
    overnightJson.insert(QStringLiteral("activeStart"), QStringLiteral("22:00"));
    overnightJson.insert(QStringLiteral("activeEnd"), QStringLiteral("06:00"));
    bool overnightOk = false;
    const ReminderItem overnightRound = ReminderItem::fromJson(overnightJson, &overnightOk);
    expect(overnightOk, "rem-win: json overnight accepted");
    expect(overnightRound.activeStartMinutes == 22 * 60, "rem-win: json overnight start");
    expect(overnightRound.activeEndMinutes == 6 * 60, "rem-win: json overnight end");

    if (g_failed == before)
        pass("reminder active window + interval");
}

void testReminderWindowPersistAndLegacyMigrate()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "rem-win-store: data dir");
    if (dataDir.isEmpty())
        return;

    QString keepId;
    QString overnightId;
    {
        DataStore store;
        expect(store.load(), "rem-win-store: load");
        overnightId = store.addReminder(QStringLiteral("夜间"), 60, 22 * 60, 6 * 60);
        expect(!overnightId.isEmpty(), "rem-win-store: cross-midnight accepted");
        keepId = store.addReminder(QStringLiteral("喝水"), 60, 9 * 60, 18 * 60);
        expect(!keepId.isEmpty(), "rem-win-store: add windowed");
        const ReminderItem *item = store.findReminder(keepId);
        expect(item && item->activeStartMinutes == 9 * 60, "rem-win-store: start in memory");
        expect(item && item->activeEndMinutes == 18 * 60, "rem-win-store: end in memory");
        const ReminderItem *night = store.findReminder(overnightId);
        expect(night && night->activeStartMinutes == 22 * 60, "rem-win-store: overnight start");
        expect(night && night->activeEndMinutes == 6 * 60, "rem-win-store: overnight end");
        expect(store.flush(), "rem-win-store: flush");
    }

    {
        DataStore store;
        expect(store.load(), "rem-win-store: reload");
        const ReminderItem *item = store.findReminder(keepId);
        expect(item && item->text == QStringLiteral("喝水"), "rem-win-store: text persisted");
        expect(item && item->intervalMinutes == 60, "rem-win-store: interval persisted");
        expect(item && item->activeStartMinutes == 9 * 60, "rem-win-store: start persisted");
        expect(item && item->activeEndMinutes == 18 * 60, "rem-win-store: end persisted");
        const ReminderItem *night = store.findReminder(overnightId);
        expect(night && night->text == QStringLiteral("夜间"), "rem-win-store: overnight text");
        expect(night && night->intervalMinutes == 60, "rem-win-store: overnight interval");
        expect(night && night->activeStartMinutes == 22 * 60,
               "rem-win-store: overnight start persisted");
        expect(night && night->activeEndMinutes == 6 * 60,
               "rem-win-store: overnight end persisted");
        if (!night)
            return;
        const QDate day(2026, 11, 2);
        ReminderItem dueNight = *night;
        dueNight.lastFiredAt = QDateTime(day, QTime(22, 0));
        expect(reminderIsDue(dueNight, QDateTime(day, QTime(23, 0))),
               "rem-win-store: persisted 23:xx due");
        dueNight.lastFiredAt = QDateTime(day, QTime(0, 0));
        expect(reminderIsDue(dueNight, QDateTime(day, QTime(1, 0))),
               "rem-win-store: persisted 01:xx due");
        dueNight.lastFiredAt = QDateTime(day, QTime(11, 0));
        expect(!reminderIsDue(dueNight, QDateTime(day, QTime(12, 0))),
               "rem-win-store: persisted 12:xx gated");
        expect(store.reminders().size() == 2, "rem-win-store: count");
    }

    QString legacyId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        DataStore store;
        expect(store.load(), "rem-win-store: load for legacy");
        QJsonObject row;
        row.insert(QStringLiteral("id"), legacyId);
        row.insert(QStringLiteral("text"), QStringLiteral("旧提醒"));
        row.insert(QStringLiteral("intervalMinutes"), 15);
        row.insert(QStringLiteral("enabled"), true);
        row.insert(QStringLiteral("lastFiredAt"),
                   QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        row.insert(QStringLiteral("createdAt"),
                   QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        QJsonObject root;
        root.insert(QStringLiteral("schemaVersion"), 1);
        root.insert(QStringLiteral("reminders"), QJsonArray{row});
        QFile file(store.remindersPath());
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "rem-win-store: write legacy");
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }

    {
        DataStore store;
        expect(store.load(), "rem-win-store: load legacy");
        const ReminderItem *item = store.findReminder(legacyId);
        expect(item != nullptr, "rem-win-store: legacy loaded");
        expect(item && item->text == QStringLiteral("旧提醒"), "rem-win-store: legacy text");
        expect(item && item->intervalMinutes == 15, "rem-win-store: legacy interval");
        expect(item && item->activeStartMinutes == 0, "rem-win-store: legacy start 00:00");
        expect(item && item->activeEndMinutes == kReminderDayMinutes,
               "rem-win-store: legacy end 24:00");
        const QDateTime night(QDate::currentDate(), QTime(23, 0));
        ReminderItem overdue = *item;
        overdue.lastFiredAt = night.addSecs(-3600);
        expect(reminderInActiveWindow(overdue, night), "rem-win-store: legacy all-day at 23:00");
        expect(reminderIsDue(overdue, night), "rem-win-store: legacy can fire at night");
    }

    if (g_failed == before)
        pass("reminder window persist + legacy migrate");
}

void testQuickAddMatchesComposerGrouping()
{
    const int before = g_failed;
    const QDate today = QDate::currentDate();
    expect(defaultNewTodoDue() == today, "quick-add: default due is today");
    expect(defaultNewTodoDue(today) == today, "quick-add: explicit today");
    expect(defaultNewTodoDue(QDate()) == today, "quick-add: invalid today falls back");

    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "quick-add: data dir");
    if (dataDir.isEmpty())
        return;

    DataStore store;
    expect(store.load(), "quick-add: load");
    const int startCount = store.todos().size();
    expect(store.addTodo(QStringLiteral("   \t  "), defaultNewTodoDue(today)).isEmpty(),
           "quick-add: whitespace does not write");
    expect(store.todos().size() == startCount, "quick-add: still empty after blank");

    const QString id = store.addTodo(QStringLiteral("  tray-quick  "), defaultNewTodoDue(today));
    expect(!id.isEmpty(), "quick-add: id");
    const TodoItem *item = store.findTodo(id);
    expect(item != nullptr, "quick-add: stored");
    expect(item && item->text == QStringLiteral("tray-quick"), "quick-add: trimmed text");
    expect(item && !item->completed, "quick-add: incomplete");
    expect(item && !item->completedAt.isValid(), "quick-add: no completedAt");
    expect(item && item->dueDate == today, "quick-add: due today");
    expect(item && groupFor(*item, today) == GroupId::Today, "quick-add: Today group");
    expect(store.todos().size() == startCount + 1, "quick-add: one row written");
    expect(store.flush(), "quick-add: flush");

    {
        DataStore reload;
        expect(reload.load(), "quick-add: reload");
        const TodoItem *again = reload.findTodo(id);
        expect(again && again->text == QStringLiteral("tray-quick") && !again->completed
                   && again->dueDate == today,
               "quick-add: persist incomplete today item");
        expect(again && groupFor(*again, today) == GroupId::Today,
               "quick-add: persisted item still Today");
    }

    if (g_failed == before)
        pass("tray quick-add matches composer today/incomplete grouping");
}

void testCompletedSectionGrouping()
{
    const int before = g_failed;
    const QDate today(2026, 11, 3);
    const QDateTime t0(today, QTime(8, 0));

    auto makeItem = [&](const QString &id, const QString &text, QDate due, bool completed,
                        int completedOffset) {
        TodoItem item;
        item.id = id;
        item.text = text;
        item.dueDate = due;
        item.createdAt = t0;
        item.updatedAt = t0.addSecs(completedOffset);
        item.completed = completed;
        if (completed)
            item.completedAt = t0.addSecs(completedOffset);
        return item;
    };

    const TodoItem openToday = makeItem(QStringLiteral("open-today"), QStringLiteral("未办今天"),
                                        today, false, 0);
    const TodoItem doneToday = makeItem(QStringLiteral("done-today"), QStringLiteral("已办今天"),
                                        today, true, 10);
    const TodoItem doneNodate = makeItem(QStringLiteral("done-nodate"), QStringLiteral("已办无日期"),
                                         QDate(), true, 20);
    const TodoItem doneLater = makeItem(QStringLiteral("done-later"), QStringLiteral("已办更晚"),
                                        today.addDays(20), true, 30);
    const TodoItem donePast = makeItem(QStringLiteral("done-past"), QStringLiteral("已办逾期日"),
                                       today.addDays(-2), true, 5);
    const TodoItem openNodate = makeItem(QStringLiteral("open-nodate"), QStringLiteral("未办无日期"),
                                         QDate(), false, 0);

    expect(groupFor(doneToday, today) == GroupId::PastCompleted, "done-group: today completed");
    expect(groupFor(doneNodate, today) == GroupId::PastCompleted, "done-group: nodate completed");
    expect(groupFor(doneLater, today) == GroupId::PastCompleted, "done-group: later completed");
    expect(groupFor(donePast, today) == GroupId::PastCompleted, "done-group: past-due completed");
    expect(groupFor(openToday, today) == GroupId::Today, "done-group: today open stays Today");
    expect(groupFor(openNodate, today) == GroupId::NoDate, "done-group: nodate open stays NoDate");
    expect(groupTitle(GroupId::PastCompleted) == QStringLiteral("已完成"),
           "done-group: section title");

    const QVector<TodoItem> source = {openToday, doneToday, doneNodate, doneLater, donePast,
                                      openNodate};
    const QVector<TodoGroup> groups = buildGroups(source, today);
    const TodoGroup *doneGroup = nullptr;
    const TodoGroup *todayGroup = nullptr;
    const TodoGroup *nodateGroup = nullptr;
    for (const TodoGroup &group : groups) {
        if (group.id == GroupId::PastCompleted)
            doneGroup = &group;
        else if (group.id == GroupId::Today)
            todayGroup = &group;
        else if (group.id == GroupId::NoDate)
            nodateGroup = &group;
        for (const TodoItem &item : group.items)
            expect(group.id != GroupId::PastCompleted || item.completed,
                   "done-group: only completed in 已完成");
        if (group.id != GroupId::PastCompleted) {
            for (const TodoItem &item : group.items)
                expect(!item.completed, "done-group: incomplete groups have no completed rows");
        }
    }
    expect(doneGroup != nullptr && doneGroup->items.size() == 4, "done-group: four completed");
    expect(doneGroup && groupHeaderCount(*doneGroup) == 4, "done-group: header count is 4");
    expect(doneGroup && doneGroup->pendingCount == 0, "done-group: pending stays 0");
    expect(todayGroup && todayGroup->items.size() == 1
               && todayGroup->items.at(0).id == openToday.id,
           "done-group: Today only has the open item");
    expect(nodateGroup && nodateGroup->items.size() == 1
               && nodateGroup->items.at(0).id == openNodate.id,
           "done-group: NoDate only has the open item");
    expect(doneGroup && doneGroup->items.at(0).id == doneLater.id,
           "done-group: newest completed first");
    expect(doneGroup && doneGroup->items.at(3).id == donePast.id,
           "done-group: oldest completed last");

    expect(source.size() == 6 && source.at(1).completed && source.at(1).id == doneToday.id,
           "done-group: buildGroups does not mutate source items");

    if (g_failed == before)
        pass("completed items fold into 已完成 without mutating data");
}

void testCompletedSectionDefaultCollapsed()
{
    const int before = g_failed;
    expect(!defaultGroupExpanded(GroupId::PastCompleted), "done-fold: default collapsed");
    expect(defaultGroupExpanded(GroupId::Today), "done-fold: Today still open");
    expect(defaultGroupExpanded(GroupId::Pinned), "done-fold: Pinned still open");
    expect(defaultGroupExpanded(GroupId::NoDate), "done-fold: NoDate still open");

    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "done-fold: data dir");
    if (dataDir.isEmpty())
        return;

    QString openId;
    QString doneId;
    {
        DataStore store;
        expect(store.load(), "done-fold: load");
        expect(!store.groupExpanded(GroupId::PastCompleted),
               "done-fold: missing key defaults collapsed");
        expect(store.groupExpanded(GroupId::Today), "done-fold: Today default expanded");
        openId = store.addTodo(QStringLiteral("还没做"), QDate::currentDate());
        doneId = store.addTodo(QStringLiteral("已经做了"), QDate::currentDate());
        expect(!openId.isEmpty() && !doneId.isEmpty(), "done-fold: seeded ids");
        expect(store.setCompleted(doneId, true), "done-fold: complete one");
        expect(store.todos().size() == 2, "done-fold: both rows still live");
        expect(store.findTodo(doneId) && store.findTodo(doneId)->completed,
               "done-fold: completed flag only, not deleted");
        store.setGroupExpanded(GroupId::PastCompleted, true);
        expect(store.groupExpanded(GroupId::PastCompleted), "done-fold: user expand honored");
        expect(store.flush(), "done-fold: flush settings + todos");
    }

    {
        DataStore reload;
        expect(reload.load(), "done-fold: reload");
        expect(reload.groupExpanded(GroupId::PastCompleted),
               "done-fold: expanded preference persisted");
        expect(reload.todos().size() == 2, "done-fold: reload still has two todos");
        const TodoItem *done = reload.findTodo(doneId);
        const TodoItem *open = reload.findTodo(openId);
        expect(done && done->completed && done->text == QStringLiteral("已经做了"),
               "done-fold: completed text intact");
        expect(open && !open->completed && open->text == QStringLiteral("还没做"),
               "done-fold: open text intact");
        reload.setGroupExpanded(GroupId::PastCompleted, false);
        expect(!reload.groupExpanded(GroupId::PastCompleted), "done-fold: collapse again");
        expect(reload.flush(), "done-fold: flush collapse");
    }

    {
        DataStore again;
        expect(again.load(), "done-fold: reload after collapse");
        expect(!again.groupExpanded(GroupId::PastCompleted),
               "done-fold: collapsed preference persisted");
        expect(again.todos().size() == 2, "done-fold: still no data delete");
    }

    if (g_failed == before)
        pass("已完成 default-collapsed + persist fold, no todo mutation");
}

const TodoGroup *findGroup(const QVector<TodoGroup> &groups, GroupId id)
{
    for (const TodoGroup &group : groups) {
        if (group.id == id)
            return &group;
    }
    return nullptr;
}

bool groupHasId(const TodoGroup *group, const QString &id)
{
    if (!group)
        return false;
    for (const TodoItem &item : group->items) {
        if (item.id == id)
            return true;
    }
    return false;
}

void testLastCompleteUndoSlotAndLatestOnly()
{
    const int before = g_failed;
    LastCompleteUndo slot;
    expect(!slot.available(), "complete-undo: empty at start");
    expect(slot.take().isEmpty(), "complete-undo: take empty is empty");

    slot.remember(QStringLiteral("first"));
    slot.remember(QStringLiteral("second"));
    expect(slot.available() && slot.id == QStringLiteral("second"),
           "complete-undo: only latest id is kept");
    expect(slot.take() == QStringLiteral("second"), "complete-undo: take returns latest");
    expect(!slot.available() && slot.take().isEmpty(),
           "complete-undo: take clears the slot");

    slot.remember(QStringLiteral("stale"));
    slot.expire();
    expect(!slot.available(), "complete-undo: expire / timeout clears");
    expect(kCompleteUndoTimeoutMs == 5000, "complete-undo: toast is short-lived");
    expect(completeUndoActionLabel() == QStringLiteral("撤销"),
           "complete-undo: action label");
    expect(completeUndoToastMessage(QStringLiteral("甲")) == QStringLiteral("已完成“甲”"),
           "complete-undo: toast copy");

    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "complete-undo: data dir");
    if (dataDir.isEmpty())
        return;

    const QDate today = QDate::currentDate();
    DataStore store;
    expect(store.load(), "complete-undo: load");
    const QString firstId = store.addTodo(QStringLiteral("误完成甲"), today);
    const QString secondId = store.addTodo(QStringLiteral("误完成乙"), today);
    expect(!firstId.isEmpty() && !secondId.isEmpty(), "complete-undo: seeded");
    expect(store.setCompleted(firstId, true), "complete-undo: complete first");
    expect(store.setCompleted(secondId, true), "complete-undo: complete second");

    LastCompleteUndo live;
    live.remember(firstId);
    live.remember(secondId);
    const QString undoId = live.take();
    expect(undoId == secondId, "complete-undo: apply latest id");
    expect(store.setCompleted(undoId, false), "complete-undo: restore latest");
    expect(!live.available(), "complete-undo: slot empty after apply");

    const TodoItem *first = store.findTodo(firstId);
    const TodoItem *second = store.findTodo(secondId);
    expect(first && first->completed, "complete-undo: earlier complete stays done");
    expect(second && !second->completed, "complete-undo: latest returns incomplete");
    expect(second && !second->completedAt.isValid(),
           "complete-undo: restore clears completedAt");

    const QVector<TodoGroup> groups = buildGroups(store.todos(), today);
    const TodoGroup *done = findGroup(groups, GroupId::PastCompleted);
    const TodoGroup *todayGroup = findGroup(groups, GroupId::Today);
    expect(groupHasId(done, firstId) && !groupHasId(done, secondId),
           "complete-undo: only earlier item stays in 已完成");
    expect(groupHasId(todayGroup, secondId) && !groupHasId(todayGroup, firstId),
           "complete-undo: restored item returns to Today");
    expect(live.take().isEmpty(), "complete-undo: second apply has nothing to undo");

    if (g_failed == before)
        pass("last complete-toggle undo: happy path + latest-only slot");
}

void testStandingTodoStatus()
{
    const int before = g_failed;
    const QDate today(2026, 11, 3);
    const QDateTime t0(today, QTime(8, 0));

    auto makeItem = [&](const QString &id, const QString &text, QDate due, bool completed,
                        QDateTime pinnedAt = QDateTime()) {
        TodoItem item;
        item.id = id;
        item.text = text;
        item.dueDate = due;
        item.createdAt = t0;
        item.updatedAt = t0;
        item.completed = completed;
        if (completed)
            item.completedAt = t0;
        item.pinnedAt = pinnedAt;
        return item;
    };

    const TodoItem openToday = makeItem(QStringLiteral("t1"), QStringLiteral("今天甲"), today, false);
    const TodoItem openToday2 = makeItem(QStringLiteral("t2"), QStringLiteral("今天乙"), today, false);
    const TodoItem doneToday = makeItem(QStringLiteral("t3"), QStringLiteral("今天已办"), today, true);
    const TodoItem openOverdue = makeItem(QStringLiteral("o1"), QStringLiteral("逾期甲"),
                                          today.addDays(-1), false);
    const TodoItem openOverdue2 = makeItem(QStringLiteral("o2"), QStringLiteral("逾期乙"),
                                           today.addDays(-3), false);
    const TodoItem doneOverdue = makeItem(QStringLiteral("o3"), QStringLiteral("逾期已办"),
                                          today.addDays(-2), true);
    const TodoItem openNodate = makeItem(QStringLiteral("n1"), QStringLiteral("无日期"), QDate(),
                                         false);
    const TodoItem openTomorrow = makeItem(QStringLiteral("m1"), QStringLiteral("明天"),
                                           today.addDays(1), false);
    const TodoItem pinnedToday = makeItem(QStringLiteral("p1"), QStringLiteral("置顶今天"), today,
                                          false, t0.addSecs(10));
    const TodoItem pinnedOverdue = makeItem(QStringLiteral("p2"), QStringLiteral("置顶逾期"),
                                            today.addDays(-1), false, t0.addSecs(20));

    expect(formatStandingTodoStatus(StandingTodoCounts{}) == QStringLiteral("今日未办 0 · 逾期 0"),
           "stand: empty is 0 · 0");
    expect(formatStandingTodoStatus(StandingTodoCounts{3, 1}) == QStringLiteral("今日未办 3 · 逾期 1"),
           "stand: both slots when non-zero");
    expect(formatStandingTodoStatus(StandingTodoCounts{0, 2}) == QStringLiteral("今日未办 0 · 逾期 2"),
           "stand: keep 今日未办 0");
    expect(formatStandingTodoStatus(StandingTodoCounts{4, 0}) == QStringLiteral("今日未办 4 · 逾期 0"),
           "stand: keep 逾期 0");

    const QVector<TodoItem> mixed = {openToday, openToday2, doneToday, openOverdue, openOverdue2,
                                     doneOverdue, openNodate, openTomorrow, pinnedToday,
                                     pinnedOverdue};
    const StandingTodoCounts counts = countStandingTodos(mixed, today);
    expect(counts.todayOpen == 3, "stand: today open = unpinned today + pinned today");
    expect(counts.overdue == 3, "stand: overdue = unpinned overdue + pinned overdue");
    expect(formatStandingTodoStatus(mixed, today) == QStringLiteral("今日未办 3 · 逾期 3"),
           "stand: format from items");

    expect(groupFor(pinnedToday, today) == GroupId::Pinned, "stand: pin leaves Today group");
    expect(groupFor(pinnedOverdue, today) == GroupId::Pinned, "stand: pin leaves Overdue group");
    expect(openToday.isOverdue(today) == false, "stand: today is not overdue");
    expect(openOverdue.isOverdue(today), "stand: yesterday is overdue");
    expect(!doneOverdue.isOverdue(today), "stand: completed past-due is not overdue");

    const QVector<TodoGroup> groups = buildGroups(mixed, today);
    int todayHeader = 0;
    int overdueHeader = 0;
    for (const TodoGroup &group : groups) {
        if (group.id == GroupId::Today)
            todayHeader = groupHeaderCount(group);
        else if (group.id == GroupId::Overdue)
            overdueHeader = groupHeaderCount(group);
    }
    expect(todayHeader == 2, "stand: Today header is unpinned today only");
    expect(overdueHeader == 2, "stand: Overdue header is unpinned overdue only");
    expect(counts.todayOpen == todayHeader + 1, "stand: tray today includes the pin");
    expect(counts.overdue == overdueHeader + 1, "stand: tray overdue includes the pin");

    const StandingTodoCounts rolled = countStandingTodos(mixed, today.addDays(1));
    expect(rolled.todayOpen == 1, "stand: midnight — tomorrow becomes today");
    expect(rolled.overdue == 6, "stand: midnight — former today + overdue + pins");
    expect(formatStandingTodoStatus(mixed, today.addDays(1)) == QStringLiteral("今日未办 1 · 逾期 6"),
           "stand: midnight copy");

    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "stand-store: data dir");
    if (dataDir.isEmpty())
        return;

    {
        DataStore store;
        expect(store.load(), "stand-store: load");
        const QString a = store.addTodo(QStringLiteral("今天未办"), today);
        const QString b = store.addTodo(QStringLiteral("昨天未办"), today.addDays(-1));
        const QString c = store.addTodo(QStringLiteral("明天未办"), today.addDays(1));
        expect(!a.isEmpty() && !b.isEmpty() && !c.isEmpty(), "stand-store: seeded");
        StandingTodoCounts live = countStandingTodos(store.todos(), today);
        expect(live.todayOpen == 1 && live.overdue == 1, "stand-store: add matches 1 · 1");
        expect(store.setCompleted(a, true), "stand-store: complete today");
        live = countStandingTodos(store.todos(), today);
        expect(live.todayOpen == 0 && live.overdue == 1, "stand-store: complete drops today");
        expect(store.updateDue(b, today), "stand-store: move overdue to today");
        live = countStandingTodos(store.todos(), today);
        expect(live.todayOpen == 1 && live.overdue == 0, "stand-store: due edit moves counts");
        expect(store.addTodo(QStringLiteral("再加一条今天"), today).size() > 0,
               "stand-store: add again");
        live = countStandingTodos(store.todos(), today);
        expect(live.todayOpen == 2 && live.overdue == 0, "stand-store: add increments today");
        expect(formatStandingTodoStatus(store.todos(), today)
                   == QStringLiteral("今日未办 2 · 逾期 0"),
               "stand-store: copy after add/complete/due");
    }

    if (g_failed == before)
        pass("standing tray counts: today/overdue, pins, midnight, add/complete/due");
}

void testTooltipChromeContract()
{
    const int before = g_failed;
    const QPalette palette = Theme::applicationPalette();
    for (const QPalette::ColorGroup group :
         {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        expect(palette.color(group, QPalette::ToolTipBase) == Theme::ToolTipFill,
               "tooltip: ToolTipBase is paper fill");
        expect(palette.color(group, QPalette::ToolTipText) == Theme::ToolTipInk,
               "tooltip: ToolTipText is paper ink");
        expect(palette.color(group, QPalette::ToolTipBase).alpha() == 255,
               "tooltip: ToolTipBase opaque");
        expect(palette.color(group, QPalette::Window).alpha() == 255,
               "tooltip: Window opaque (no parent-window leak)");
    }
    expect(Theme::ToolTipRadius == 5, "tooltip: paper radius 5");
    const QString css = Theme::styleSheet();
    expect(css.contains(QStringLiteral("QToolTip")), "tooltip: QSS targets QToolTip");
    expect(css.contains(QStringLiteral("QTipLabel")),
           "tooltip: QSS targets QTipLabel (reuse path)");
    expect(css.contains(QStringLiteral("border-radius: 5px")),
           "tooltip: QSS radius matches mask");
    expect(css.contains(QStringLiteral("#fffdf3")), "tooltip: QSS paper fill");
    expect(css.contains(QStringLiteral("#4d483a")), "tooltip: QSS paper ink");

    if (g_failed == before)
        pass("tooltip chrome contract (paper + rounded, all groups)");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QDir().mkpath(testRoot());

    testRoundTrip();
    testCorruptionRecovery();
    testCorruptPrimaryWithoutBackup();
    testCorruptPrimaryBadBackup();
    testImmediatePersistAndIsolation();
    testRetentionPurge();
    testDailySnapshots();
    testWeeklyMonthlySnapshots();
    testArchiveExpired();
    testArchiveReimport();
    testArchiveCurrentItem();
    testPinGrouping();
    testPinPersist();
    testTrayHideHintPersist();
    testPomodoroHelpers();
    testPomodoroDoesNotTouchTodos();
    testPomodoroMinutesPersist();
    testPomodoroStatsOptInPersist();
    testPomodoroStatsCorruptionRecovery();
    testPomodoroStatsClear();
    testReminderHelpers();
    testReminderSnooze();
    testReminderDismissThisOccurrence();
    testReminderFireLabel();
    testReminderStorePersistAndDisable();
    testReminderDueThenMarkFired();
    testReminderCorruptionRecovery();
    testReminderCapAndSkipInvalid();
    testReminderActiveWindowAndInterval();
    testReminderWindowPersistAndLegacyMigrate();
    testQuickAddMatchesComposerGrouping();
    testCompletedSectionGrouping();
    testCompletedSectionDefaultCollapsed();
    testLastCompleteUndoSlotAndLatestOnly();
    testStandingTodoStatus();
    testTooltipChromeContract();

    if (g_failed != 0) {
        std::fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    std::fprintf(stdout, "ALL GREEN\n");
    return 0;
}
