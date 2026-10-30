#include "datastore.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
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
    testArchiveExpired();

    if (g_failed != 0) {
        std::fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    std::fprintf(stdout, "ALL GREEN\n");
    return 0;
}
