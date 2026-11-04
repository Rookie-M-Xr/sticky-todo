#include "datastore.h"
#include "mainwindow.h"
#include "models.h"
#include "theme.h"
#include "todoview.h"
#include "widgets.h"
#include "winutils.h"

#include <QAction>
#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUuid>

#include <cstdio>

struct MainWindowTestAccess {
    static bool startAndFireDuePomodoro(MainWindow &window, const QString &id)
    {
        window.startPomodoro(id);
        if (window.m_pomodoroId != id)
            return false;
        window.m_pomodoroDeadline = QDateTime::currentDateTime().addSecs(-1);
        window.onPomodoroTick();
        return window.m_pomodoroId.isEmpty();
    }

    static void toggleByHotkey(MainWindow &window) { window.toggleByHotkey(); }

    static bool trayInstalled(const MainWindow &window) { return window.m_trayInstalled; }

    static bool startHiddenFlag(const MainWindow &window) { return window.m_startHiddenToTray; }
};

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

QString makeDataDir()
{
    const QString root = QDir::temp().filePath(QStringLiteral("sticky-todo-quickadd"));
    const QString dir = QDir(root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(dir))
        return QString();
    qputenv("STICKY_TODO_DATA_DIR", QDir::toNativeSeparators(dir).toUtf8());
    return dir;
}

QuickAddPopup *findPopup()
{
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets) {
        if (auto *popup = qobject_cast<QuickAddPopup *>(widget))
            return popup;
    }
    return nullptr;
}

Snackbar *findStandaloneSnackbar()
{
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets) {
        if (auto *bar = qobject_cast<Snackbar *>(widget))
            return bar;
    }
    return nullptr;
}

bool writeDueReminder(const QString &dataDir, const QString &text)
{
    const QDateTime now = QDateTime::currentDateTime();
    QJsonObject row;
    row.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    row.insert(QStringLiteral("text"), text);
    row.insert(QStringLiteral("intervalMinutes"), 1);
    row.insert(QStringLiteral("activeStart"), QStringLiteral("00:00"));
    row.insert(QStringLiteral("activeEnd"), QStringLiteral("24:00"));
    row.insert(QStringLiteral("enabled"), true);
    row.insert(QStringLiteral("lastFiredAt"), now.addSecs(-120).toString(Qt::ISODateWithMs));
    row.insert(QStringLiteral("createdAt"), now.addSecs(-3600).toString(Qt::ISODateWithMs));
    row.insert(QStringLiteral("updatedAt"), now.addSecs(-120).toString(Qt::ISODateWithMs));
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("reminders"), QJsonArray{row});
    QFile file(QDir(dataDir).filePath(QStringLiteral("reminders.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

QDateTime reminderLastFiredOnDisk(const QString &dataDir)
{
    QFile file(QDir(dataDir).filePath(QStringLiteral("reminders.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray reminders = doc.object().value(QStringLiteral("reminders")).toArray();
    if (reminders.isEmpty())
        return {};
    return QDateTime::fromString(
        reminders.at(0).toObject().value(QStringLiteral("lastFiredAt")).toString(),
        Qt::ISODateWithMs);
}

bool clickSnackbarButton(Snackbar *bar, const QString &label)
{
    if (!bar)
        return false;
    const auto buttons = bar->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button && button->isVisible() && button->text() == label) {
            button->click();
            return true;
        }
    }
    return false;
}

int todoCountOnDisk(const QString &dataDir)
{
    QFile file(QDir(dataDir).filePath(QStringLiteral("todos.json")));
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.object().value(QStringLiteral("todos")).toArray().size();
}

bool diskHasText(const QString &dataDir, const QString &text)
{
    QFile file(QDir(dataDir).filePath(QStringLiteral("todos.json")));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray todos = doc.object().value(QStringLiteral("todos")).toArray();
    for (const QJsonValue &value : todos) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("text")).toString() == text
            && !object.value(QStringLiteral("completed")).toBool())
            return true;
    }
    return false;
}

void pump()
{
    QApplication::processEvents();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}

struct TrayOverride {
    explicit TrayOverride(int available) { WinUtils::setTrayAvailableOverrideForTest(available); }
    ~TrayOverride() { WinUtils::setTrayAvailableOverrideForTest(-1); }
};

bool writeAutoStartSetting(const QString &dataDir, bool autoStart)
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("autoStart"), autoStart);
    root.insert(QStringLiteral("trayHideHintShown"), true);
    QFile file(QDir(dataDir).filePath(QStringLiteral("settings.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

void testHiddenWindowTrayAddAndCancel()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "window: data dir");
    if (dataDir.isEmpty())
        return;

    MainWindow window;
    window.show();
    pump();
    window.hide();
    pump();
    expect(!window.isVisible(), "window: hidden like tray");

    auto *action = window.findChild<QAction *>(QStringLiteral("trayQuickAdd"));
    expect(action != nullptr, "window: tray 快速添加 action");
    if (!action)
        return;

    action->trigger();
    pump();
    QTimer::singleShot(0, [] {});
    pump();

    QuickAddPopup *popup = findPopup();
    expect(popup != nullptr && popup->isVisible(), "window: popup opens while hidden");
    expect(!window.isVisible(), "window: still hidden after 快速添加");
    if (!popup)
        return;

    popup->setText(QStringLiteral("  托盘里加的  "));
    popup->confirm();
    pump();

    expect(!window.isVisible(), "window: confirm does not raise main window");
    expect(todoCountOnDisk(dataDir) == 1, "window: confirm wrote one todo");
    expect(diskHasText(dataDir, QStringLiteral("托盘里加的")),
           "window: trimmed incomplete item on disk");

    action->trigger();
    pump();
    popup = findPopup();
    expect(popup != nullptr && popup->isVisible(), "window: popup reopens");
    if (popup) {
        popup->setText(QStringLiteral("取消不应写入"));
        popup->cancel();
        pump();
    }
    expect(todoCountOnDisk(dataDir) == 1, "window: cancel does not write");
    expect(!diskHasText(dataDir, QStringLiteral("取消不应写入")),
           "window: cancelled text absent");

    window.show();
    pump();
    expect(window.isVisible(), "window: can show after add");
    bool inList = false;
    const auto rows = window.findChildren<TodoRow *>();
    for (TodoRow *row : rows) {
        if (row && row->item().text == QStringLiteral("托盘里加的") && !row->item().completed) {
            inList = true;
            expect(row->item().dueDate == QDate::currentDate(), "window: due today");
        }
    }
    expect(inList, "window: opening main list shows the new item");

    if (g_failed == before)
        pass("hidden main window tray add / cancel / show list");
}

GroupSection *findSection(QWidget *root, GroupId id)
{
    const auto sections = root->findChildren<GroupSection *>();
    for (GroupSection *section : sections) {
        if (section && section->groupId() == id)
            return section;
    }
    return nullptr;
}

void testCompletedSectionFoldedInWindow()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "fold-ui: data dir");
    if (dataDir.isEmpty())
        return;

    {
        DataStore store;
        expect(store.load(), "fold-ui: seed load");
        const QString openId = store.addTodo(QStringLiteral("未完成可读"), QDate::currentDate());
        const QString doneId = store.addTodo(QStringLiteral("堆着的已完成"), QDate::currentDate());
        expect(!openId.isEmpty() && !doneId.isEmpty(), "fold-ui: seeded");
        expect(store.setCompleted(doneId, true), "fold-ui: complete");
        expect(store.flush(), "fold-ui: flush seed");
        expect(todoCountOnDisk(dataDir) == 2, "fold-ui: both rows on disk before window");
    }

    MainWindow window;
    window.show();
    pump();
    QTimer::singleShot(0, [] {});
    pump();

    GroupSection *doneSection = findSection(&window, GroupId::PastCompleted);
    GroupSection *todaySection = findSection(&window, GroupId::Today);
    expect(doneSection != nullptr, "fold-ui: 已完成 section exists");
    expect(todaySection != nullptr, "fold-ui: Today section exists");
    expect(doneSection && !doneSection->isExpanded(), "fold-ui: 已完成 default collapsed");
    expect(todaySection && todaySection->isExpanded(), "fold-ui: Today stays expanded");
    expect(doneSection && doneSection->pendingCount() == 1, "fold-ui: header count is 1");

    bool sawOpen = false;
    bool sawDoneRow = false;
    const auto rows = window.findChildren<TodoRow *>();
    for (TodoRow *row : rows) {
        if (!row)
            continue;
        if (row->item().text == QStringLiteral("未完成可读") && !row->item().completed)
            sawOpen = true;
        if (row->item().text == QStringLiteral("堆着的已完成") && row->item().completed)
            sawDoneRow = true;
    }
    expect(sawOpen, "fold-ui: incomplete row still in the list");
    expect(sawDoneRow, "fold-ui: completed row still mounted (not deleted)");
    expect(todoCountOnDisk(dataDir) == 2, "fold-ui: disk still has both after show");

    if (doneSection) {
        doneSection->setExpanded(true, false);
        pump();
        expect(doneSection->isExpanded(), "fold-ui: can expand 已完成");
    }

    expect(todoCountOnDisk(dataDir) == 2, "fold-ui: expand does not mutate todos");

    if (g_failed == before)
        pass("window: 已完成 default-collapsed, expand, data intact");
}

void testHiddenWindowReminderDoesNotRaise()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "rem-hide: data dir");
    if (dataDir.isEmpty())
        return;
    const QString title = QStringLiteral("藏窗喝水");
    expect(writeDueReminder(dataDir, title), "rem-hide: seed overdue reminder");

    MainWindow window;
    window.show();
    window.hide();
    expect(!window.isVisible(), "rem-hide: hidden like tray before fire");
    pump();
    QTimer::singleShot(0, [] {});
    pump();

    expect(!window.isVisible(), "rem-hide: fire does not show main window");
    const QDateTime fired = reminderLastFiredOnDisk(dataDir);
    expect(fired.isValid(), "rem-hide: lastFired written");
    expect(fired.isValid() && fired.secsTo(QDateTime::currentDateTime()) < 30,
           "rem-hide: lastFired advanced at fire");

    Snackbar *onCard = window.findChild<Snackbar *>();
    expect(onCard != nullptr, "rem-hide: in-card snackbar exists");
    expect(!(onCard && onCard->isVisible()),
           "rem-hide: in-card snackbar stays off (would show parent)");

    Snackbar *standalone = findStandaloneSnackbar();
    expect(standalone != nullptr && standalone->isVisible(),
           "rem-hide: standalone reminder bar appears");
    expect(standalone && standalone != onCard, "rem-hide: bar is not the card child");
    if (standalone) {
        auto *label = standalone->findChild<QLabel *>();
        expect(label && label->toolTip() == title, "rem-hide: bar title is reminder text");
        expect(clickSnackbarButton(standalone, reminderDismissLabel()),
               "rem-hide: 忽略 is clickable");
    }
    pump();
    expect(!window.isVisible(), "rem-hide: dismiss leaves main window hidden");

    window.show();
    pump();
    expect(window.isVisible(), "rem-hide: 显示 still opens the window");
    expect(!(findStandaloneSnackbar() && findStandaloneSnackbar()->isVisible()),
           "rem-hide: standalone bar dismissed on show");

    if (g_failed == before)
        pass("hidden window reminder fire does not raise (#94)");
}

bool seedOpenTodo(const QString &dataDir, const QString &text, int pomodoroMinutes)
{
    DataStore store;
    if (!store.load())
        return false;
    store.setPomodoroMinutes(pomodoroMinutes);
    const QString id = store.addTodo(text, QDate::currentDate());
    return !id.isEmpty() && store.flush();
}

TodoRow *findRowByText(QWidget *root, const QString &text)
{
    const auto rows = root->findChildren<TodoRow *>();
    for (TodoRow *row : rows) {
        if (row && row->item().text == text && !row->item().completed)
            return row;
    }
    return nullptr;
}

void testHiddenWindowPomodoroDoesNotRaise()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "pomo-hide: data dir");
    if (dataDir.isEmpty())
        return;
    const QString title = QStringLiteral("藏窗番茄");
    expect(seedOpenTodo(dataDir, title, 1), "pomo-hide: seed open todo");

    MainWindow window;
    window.show();
    pump();
    TodoRow *row = findRowByText(&window, title);
    expect(row != nullptr, "pomo-hide: row mounted");
    const QString id = row ? row->item().id : QString();
    window.hide();
    expect(!window.isVisible(), "pomo-hide: hidden like tray before fire");
    pump();

    expect(!id.isEmpty() && MainWindowTestAccess::startAndFireDuePomodoro(window, id),
           "pomo-hide: due tick ran");
    pump();
    QTimer::singleShot(0, [] {});
    pump();

    expect(!window.isVisible(), "pomo-hide: fire does not show main window");

    Snackbar *onCard = window.findChild<Snackbar *>();
    expect(onCard != nullptr, "pomo-hide: in-card snackbar exists");
    expect(!(onCard && onCard->isVisible()),
           "pomo-hide: in-card snackbar stays off (would show parent)");

    Snackbar *standalone = findStandaloneSnackbar();
    expect(standalone != nullptr && standalone->isVisible(),
           "pomo-hide: standalone pomodoro bar appears");
    expect(standalone && standalone != onCard, "pomo-hide: bar is not the card child");
    if (standalone) {
        auto *label = standalone->findChild<QLabel *>();
        expect(label && label->toolTip().contains(QStringLiteral("1 分钟到了")),
               "pomo-hide: bar is the pomodoro-due cue");
        standalone->dismiss();
    }
    pump();
    expect(!window.isVisible(), "pomo-hide: dismiss leaves main window hidden");

    window.show();
    pump();
    expect(window.isVisible(), "pomo-hide: 显示 still opens the window");
    expect(!(findStandaloneSnackbar() && findStandaloneSnackbar()->isVisible()),
           "pomo-hide: standalone bar dismissed on show");

    if (g_failed == before)
        pass("hidden window pomodoro fire does not raise (#96)");
}

void testVisibleWindowPomodoroStillShowsToast()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "pomo-show: data dir");
    if (dataDir.isEmpty())
        return;
    const QString title = QStringLiteral("开窗番茄");
    expect(seedOpenTodo(dataDir, title, 1), "pomo-show: seed open todo");

    MainWindow window;
    window.show();
    pump();
    QTimer::singleShot(0, [] {});
    pump();

    TodoRow *row = findRowByText(&window, title);
    expect(row != nullptr, "pomo-show: row mounted");
    const QString id = row ? row->item().id : QString();
    expect(!id.isEmpty() && MainWindowTestAccess::startAndFireDuePomodoro(window, id),
           "pomo-show: due tick ran");
    pump();

    expect(window.isVisible(), "pomo-show: visible window stays visible");
    Snackbar *onCard = window.findChild<Snackbar *>();
    expect(onCard && onCard->isVisible(), "pomo-show: in-card toast still shows");
    if (onCard) {
        auto *label = onCard->findChild<QLabel *>();
        expect(label && label->toolTip().contains(QStringLiteral("1 分钟到了")),
               "pomo-show: in-card copy kept");
        onCard->dismiss();
    }
    pump();
    expect(window.isVisible(), "pomo-show: dismiss does not hide the window");
    expect(!(findStandaloneSnackbar() && findStandaloneSnackbar()->isVisible()),
           "pomo-show: no parentless bar while visible");

    if (g_failed == before)
        pass("visible window pomodoro toast unchanged (#96)");
}

void testVisibleWindowReminderStillShowsBanner()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "rem-show: data dir");
    if (dataDir.isEmpty())
        return;
    const QString title = QStringLiteral("开窗喝水");
    expect(writeDueReminder(dataDir, title), "rem-show: seed overdue reminder");

    MainWindow window;
    window.show();
    pump();
    QTimer::singleShot(0, [] {});
    pump();

    expect(window.isVisible(), "rem-show: visible window stays visible");
    Snackbar *onCard = window.findChild<Snackbar *>();
    expect(onCard && onCard->isVisible(), "rem-show: in-card reminder bar still shows");
    if (onCard) {
        auto *label = onCard->findChild<QLabel *>();
        expect(label && label->toolTip() == title, "rem-show: in-card title kept");
        expect(clickSnackbarButton(onCard, reminderDismissLabel()),
               "rem-show: 忽略 still works");
    }
    pump();
    expect(window.isVisible(), "rem-show: dismiss does not hide the window");

    if (g_failed == before)
        pass("visible window reminder banner unchanged (#94)");
}

void testHiddenWindowTrayStandingStatus()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "tray-stand: data dir");
    if (dataDir.isEmpty())
        return;

    const QDate today = QDate::currentDate();
    {
        DataStore store;
        expect(store.load(), "tray-stand: seed load");
        expect(!store.addTodo(QStringLiteral("今天甲"), today).isEmpty(), "tray-stand: today a");
        expect(!store.addTodo(QStringLiteral("今天乙"), today).isEmpty(), "tray-stand: today b");
        expect(!store.addTodo(QStringLiteral("昨天未办"), today.addDays(-1)).isEmpty(),
               "tray-stand: overdue");
        expect(!store.addTodo(QStringLiteral("无日期不计入"), QDate()).isEmpty(),
               "tray-stand: nodate");
        expect(store.flush(), "tray-stand: flush seed");
    }

    MainWindow window;
    window.show();
    pump();
    QTimer::singleShot(0, [] {});
    pump();
    window.hide();
    pump();
    expect(!window.isVisible(), "tray-stand: hidden like tray");

    auto *status = window.findChild<QAction *>(QStringLiteral("trayStatus"));
    expect(status != nullptr, "tray-stand: menu status action");
    if (!status)
        return;
    expect(!status->isEnabled(), "tray-stand: status is not a command");
    expect(status->text() == QStringLiteral("今日未办 2 · 逾期 1"),
           "tray-stand: seed 2 today + 1 overdue");

    TodoRow *todayRow = findRowByText(&window, QStringLiteral("今天甲"));
    expect(todayRow != nullptr, "tray-stand: found today row");
    if (!todayRow)
        return;
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(todayRow, &space);
    pump();
    QTimer::singleShot(0, [] {});
    pump();
    expect(!window.isVisible(), "tray-stand: complete does not raise");
    expect(status->text() == QStringLiteral("今日未办 1 · 逾期 1"),
           "tray-stand: complete today updates menu");

    auto *quickAdd = window.findChild<QAction *>(QStringLiteral("trayQuickAdd"));
    expect(quickAdd != nullptr, "tray-stand: tray 快速添加");
    if (!quickAdd)
        return;
    quickAdd->trigger();
    pump();
    QTimer::singleShot(0, [] {});
    pump();
    QuickAddPopup *popup = findPopup();
    expect(popup != nullptr && popup->isVisible(), "tray-stand: popup opens while hidden");
    if (!popup)
        return;
    popup->setText(QStringLiteral("托盘再加一条"));
    popup->confirm();
    pump();
    QTimer::singleShot(0, [] {});
    pump();
    expect(!window.isVisible(), "tray-stand: still hidden after add");
    expect(status->text() == QStringLiteral("今日未办 2 · 逾期 1"),
           "tray-stand: add today updates menu");
    expect(diskHasText(dataDir, QStringLiteral("托盘再加一条")),
           "tray-stand: new today item on disk");

    window.show();
    pump();
    QTimer::singleShot(0, [] {});
    pump();
    int todayOpen = 0;
    int overdue = 0;
    for (TodoRow *row : window.findChildren<TodoRow *>()) {
        if (!row || row->item().completed)
            continue;
        if (row->item().isOverdue(today))
            ++overdue;
        else if (row->item().dueDate == today)
            ++todayOpen;
    }
    expect(todayOpen == 2 && overdue == 1, "tray-stand: list matches 今日未办 2 · 逾期 1");
    expect(status->text() == formatStandingTodoStatus({todayOpen, overdue}),
           "tray-stand: menu still matches list counts");

    if (g_failed == before)
        pass("hidden window tray standing status matches list after complete/add");
}

TodoRow *findRowByTextAny(QWidget *root, const QString &text)
{
    const auto rows = root->findChildren<TodoRow *>();
    for (TodoRow *row : rows) {
        if (row && row->item().text == text)
            return row;
    }
    return nullptr;
}

bool sectionHasText(GroupSection *section, const QString &text, bool completed)
{
    if (!section)
        return false;
    const auto rows = section->findChildren<TodoRow *>();
    for (TodoRow *row : rows) {
        if (row && row->item().text == text && row->item().completed == completed)
            return true;
    }
    return false;
}

void completeVisibleRow(QWidget *root, const QString &text)
{
    TodoRow *row = findRowByText(root, text);
    if (!row)
        return;
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(row, &space);
    pump();
    QTimer::singleShot(0, [] {});
    pump();
}

void waitHidden(QWidget *widget, int timeoutMs)
{
    if (!widget)
        return;
    QElapsedTimer timer;
    timer.start();
    while (widget->isVisible() && timer.elapsed() < timeoutMs)
        QApplication::processEvents();
}

void testCompleteUndoToastHappyPath()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "complete-undo-ui: data dir");
    if (dataDir.isEmpty())
        return;

    const QString title = QStringLiteral("误点完成");
    expect(seedOpenTodo(dataDir, title, kPomodoroDefaultMinutes),
           "complete-undo-ui: seed open todo");

    MainWindow window;
    window.show();
    pump();
    QTimer::singleShot(0, [] {});
    pump();

    GroupSection *today = findSection(&window, GroupId::Today);
    GroupSection *done = findSection(&window, GroupId::PastCompleted);
    expect(today && sectionHasText(today, title, false),
           "complete-undo-ui: starts in Today");
    expect(!(done && sectionHasText(done, title, true)),
           "complete-undo-ui: not in 已完成 yet");

    completeVisibleRow(&window, title);
    TodoRow *doneRow = findRowByTextAny(&window, title);
    expect(doneRow && doneRow->item().completed, "complete-undo-ui: marked complete");
    done = findSection(&window, GroupId::PastCompleted);
    today = findSection(&window, GroupId::Today);
    expect(done && sectionHasText(done, title, true),
           "complete-undo-ui: folded into 已完成");
    expect(!(today && sectionHasText(today, title, false)),
           "complete-undo-ui: left the open section");
    expect(done && !done->isExpanded(), "complete-undo-ui: 已完成 stays collapsed");

    Snackbar *bar = window.findChild<Snackbar *>();
    expect(bar && bar->isVisible(), "complete-undo-ui: toast visible");
    expect(clickSnackbarButton(bar, completeUndoActionLabel()),
           "complete-undo-ui: 撤销 is clickable");
    pump();
    QTimer::singleShot(0, [] {});
    pump();

    today = findSection(&window, GroupId::Today);
    done = findSection(&window, GroupId::PastCompleted);
    TodoRow *openRow = findRowByText(&window, title);
    expect(openRow && !openRow->item().completed,
           "complete-undo-ui: one undo restores incomplete");
    expect(today && sectionHasText(today, title, false),
           "complete-undo-ui: returns to Today");
    expect(!(done && sectionHasText(done, title, true)),
           "complete-undo-ui: left 已完成");
    expect(diskHasText(dataDir, title), "complete-undo-ui: incomplete on disk");

    if (g_failed == before)
        pass("window: complete toast undo restores open section (#92)");
}

void testCompleteUndoToastLatestOnlyAndTimeout()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "complete-undo-latest: data dir");
    if (dataDir.isEmpty())
        return;

    const QString first = QStringLiteral("先完成的");
    const QString second = QStringLiteral("后完成的");
    {
        DataStore store;
        expect(store.load(), "complete-undo-latest: seed load");
        expect(!store.addTodo(first, QDate::currentDate()).isEmpty(),
               "complete-undo-latest: seed first");
        expect(!store.addTodo(second, QDate::currentDate()).isEmpty(),
               "complete-undo-latest: seed second");
        expect(store.flush(), "complete-undo-latest: flush");
    }

    MainWindow window;
    window.show();
    pump();
    QTimer::singleShot(0, [] {});
    pump();

    completeVisibleRow(&window, first);
    completeVisibleRow(&window, second);

    Snackbar *bar = window.findChild<Snackbar *>();
    expect(bar && bar->isVisible(), "complete-undo-latest: toast for latest");
    expect(clickSnackbarButton(bar, completeUndoActionLabel()),
           "complete-undo-latest: undo latest");
    pump();
    QTimer::singleShot(0, [] {});
    pump();

    TodoRow *firstRow = findRowByTextAny(&window, first);
    TodoRow *secondRow = findRowByTextAny(&window, second);
    expect(firstRow && firstRow->item().completed,
           "complete-undo-latest: earlier complete stays done");
    expect(secondRow && !secondRow->item().completed,
           "complete-undo-latest: only latest is undone");
    expect(sectionHasText(findSection(&window, GroupId::PastCompleted), first, true),
           "complete-undo-latest: first stays in 已完成");
    expect(sectionHasText(findSection(&window, GroupId::Today), second, false),
           "complete-undo-latest: second returns to Today");
    expect(!diskHasText(dataDir, first), "complete-undo-latest: first still completed on disk");
    expect(diskHasText(dataDir, second), "complete-undo-latest: second incomplete on disk");

    completeVisibleRow(&window, second);
    bar = window.findChild<Snackbar *>();
    expect(bar && bar->isVisible(), "complete-undo-timeout: toast after re-complete");
    if (bar)
        bar->dismiss();
    waitHidden(bar, 400);
    expect(!(bar && bar->isVisible()), "complete-undo-timeout: toast gone");
    expect(!clickSnackbarButton(bar, completeUndoActionLabel()),
           "complete-undo-timeout: 撤销 no longer available");
    TodoRow *timedOut = findRowByTextAny(&window, second);
    expect(timedOut && timedOut->item().completed,
           "complete-undo-timeout: item stays complete");
    expect(!diskHasText(dataDir, second),
           "complete-undo-timeout: completed remains on disk");

    if (g_failed == before)
        pass("window: only latest complete toggle is undoable; timeout drops undo (#92)");
}

void testAutostartWithTrayStartsHidden()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "start-hide: data dir");
    if (dataDir.isEmpty())
        return;
    expect(writeAutoStartSetting(dataDir, true), "start-hide: seed autostart on");

    TrayOverride tray(1);
    MainWindow window;
    expect(MainWindowTestAccess::startHiddenFlag(window),
           "start-hide: ctor snapshots autostart+tray");
    expect(!window.isVisible(), "start-hide: ctor does not auto-show");
    window.presentOnStartup();
    expect(!window.isVisible(), "start-hide: presentOnStartup stays hidden");
    expect(MainWindowTestAccess::trayInstalled(window),
           "start-hide: tray icon installed while hidden");

    pump();
    expect(!window.isVisible(), "start-hide: timers do not raise the window");

    auto *showAction = window.findChild<QAction *>(QStringLiteral("trayShow"));
    expect(showAction != nullptr, "start-hide: tray 显示 action");
    if (showAction)
        showAction->trigger();
    pump();
    expect(window.isVisible(), "start-hide: tray 显示 raises the window");

    auto *hideAction = window.findChild<QAction *>(QStringLiteral("trayHide"));
    expect(hideAction != nullptr, "start-hide: tray 隐藏 action");
    if (hideAction)
        hideAction->trigger();
    pump();
    expect(!window.isVisible(), "start-hide: tray 隐藏 returns to tray");

    MainWindowTestAccess::toggleByHotkey(window);
    pump();
    expect(window.isVisible(), "start-hide: hotkey show still works");

    if (g_failed == before)
        pass("autostart on + tray: cold start hidden; tray/hotkey show (#97)");
}

void testAutostartWithoutTrayStaysVisible()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "start-show: data dir");
    if (dataDir.isEmpty())
        return;
    expect(writeAutoStartSetting(dataDir, true), "start-show: seed autostart on");

    TrayOverride tray(0);
    MainWindow window;
    expect(!MainWindowTestAccess::startHiddenFlag(window),
           "start-show: no-tray does not snapshot hidden start");
    window.presentOnStartup();
    expect(window.isVisible(), "start-show: no tray stays visible (must be operable)");
    expect(!MainWindowTestAccess::trayInstalled(window),
           "start-show: no fake tray icon");

    pump();
    expect(window.isVisible(), "start-show: stays visible after timers");

    if (g_failed == before)
        pass("autostart on + no tray: cold start visible (#97)");
}

void testTrayWithoutAutostartStaysVisible()
{
    const int before = g_failed;
    const QString dataDir = makeDataDir();
    expect(!dataDir.isEmpty(), "start-off: data dir");
    if (dataDir.isEmpty())
        return;
    expect(writeAutoStartSetting(dataDir, false), "start-off: seed autostart off");

    TrayOverride tray(1);
    MainWindow window;
    expect(!MainWindowTestAccess::startHiddenFlag(window),
           "start-off: autostart-off does not snapshot hidden start");
    window.presentOnStartup();
    expect(window.isVisible(), "start-off: tray + autostart-off still shows");

    pump();
    expect(window.isVisible(), "start-off: stays visible after timers");

    if (g_failed == before)
        pass("autostart off + tray: cold start visible (#97)");
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    WinUtils::setTrayAvailableOverrideForTest(-1);
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    app.setApplicationName(QStringLiteral("Sticky Todo"));
    app.setStyle(QStringLiteral("Fusion"));
    Theme::applyApplicationChrome();

    testAutostartWithTrayStartsHidden();
    testAutostartWithoutTrayStaysVisible();
    testTrayWithoutAutostartStaysVisible();
    testHiddenWindowTrayAddAndCancel();
    testCompletedSectionFoldedInWindow();
    testHiddenWindowReminderDoesNotRaise();
    testVisibleWindowReminderStillShowsBanner();
    testHiddenWindowPomodoroDoesNotRaise();
    testVisibleWindowPomodoroStillShowsToast();
    testHiddenWindowTrayStandingStatus();
    testCompleteUndoToastHappyPath();
    testCompleteUndoToastLatestOnlyAndTimeout();

    if (g_failed != 0) {
        std::fprintf(stderr, "%d quick-add window assertion(s) failed\n", g_failed);
        return 1;
    }
    std::fprintf(stdout, "ALL GREEN\n");
    return 0;
}
