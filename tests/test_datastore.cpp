#include "datastore.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
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

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QDir().mkpath(testRoot());

    testRoundTrip();
    testCorruptionRecovery();

    if (g_failed != 0) {
        std::fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    std::fprintf(stdout, "ALL GREEN\n");
    return 0;
}
