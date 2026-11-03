#include "models.h"
#include "theme.h"
#include "widgets.h"

#include <QApplication>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QWidget>

#include <cstdio>
#include <functional>

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

QVector<QPair<QString, std::function<void()>>> snoozeActions()
{
    QVector<QPair<QString, std::function<void()>>> actions;
    actions.append({QStringLiteral("延后 5"), [] {}});
    actions.append({QStringLiteral("延后 10"), [] {}});
    actions.append({QStringLiteral("延后 15"), [] {}});
    return actions;
}

QList<QPushButton *> visibleActionButtons(Snackbar *bar)
{
    QList<QPushButton *> out;
    if (!bar)
        return out;
    const auto buttons = bar->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button && button->isVisible())
            out.append(button);
    }
    return out;
}

void fireOnCard(Snackbar *bar, const QString &title, int width)
{
    bar->showActions(reminderFireLabel(title), snoozeActions(), 0, reminderSnoozeMenuLabel());
    bar->setGeometry(18, 400, width, bar->height());
    QApplication::processEvents();
}

void testMediumTitleReadable()
{
    const int before = g_failed;
    QWidget card;
    card.resize(368, 508);
    card.show();
    Snackbar bar(&card);
    const QString title = QStringLiteral("去一楼会议室拿文件了");
    expect(title.size() >= 10, "snack-mid: fixture ≥10 chars");
    fireOnCard(&bar, title, 332);

    expect(bar.height() <= 56, "snack-mid: height stays compact");
    expect(bar.width() == 332, "snack-mid: width stays inside card");
    expect(bar.height() < card.height() / 4, "snack-mid: does not blow the window");

    auto *label = bar.findChild<QLabel *>();
    expect(label != nullptr, "snack-mid: label");
    if (label) {
        expect(label->toolTip() == title, "snack-mid: tooltip is full title");
        expect(!label->text().contains(QStringLiteral("提醒：")), "snack-mid: no 提醒： prefix");
        expect(!label->text().contains(QStringLiteral("…")), "snack-mid: 10-char title not crushed");
        expect(label->text() == title, "snack-mid: full title visible");
    }

    const QList<QPushButton *> buttons = visibleActionButtons(&bar);
    expect(buttons.size() == 1, "snack-mid: snooze collapsed to one control");
    if (buttons.size() == 1)
        expect(buttons.front()->text() == reminderSnoozeMenuLabel(),
               "snack-mid: overflow label 延后 ▾");

    if (g_failed == before)
        pass("snackbar medium title readable (#77)");
}

void testLongTitleHover()
{
    const int before = g_failed;
    QWidget card;
    card.resize(316, 400);
    card.show();
    Snackbar bar(&card);
    const QString title = QStringLiteral("去").repeated(80);
    fireOnCard(&bar, title, 280);

    expect(bar.width() == 280, "snack-long: min-width card still hosts bar");
    expect(bar.height() <= 56, "snack-long: height stays compact");

    auto *label = bar.findChild<QLabel *>();
    expect(label != nullptr, "snack-long: label");
    if (label) {
        expect(label->toolTip() == title, "snack-long: hover/tooltip is full text");
        expect(label->text().startsWith(QStringLiteral("去")), "snack-long: meaningful start");
        expect(label->text().contains(QStringLiteral("…")) || label->text().size() < title.size(),
               "snack-long: overflow elided");
        expect(!label->text().contains(QStringLiteral("提醒：")), "snack-long: no prefix");
    }
    expect(visibleActionButtons(&bar).size() == 1, "snack-long: still one snooze control");

    if (g_failed == before)
        pass("snackbar long title start + tooltip (#77)");
}

void testEmptyFallback()
{
    const int before = g_failed;
    QWidget card;
    card.resize(368, 508);
    card.show();
    Snackbar bar(&card);
    fireOnCard(&bar, QStringLiteral("   "), 332);
    auto *label = bar.findChild<QLabel *>();
    expect(label && label->toolTip() == QStringLiteral("定时提醒"),
           "snack-empty: fallback label");
    if (g_failed == before)
        pass("snackbar empty fallback");
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    Theme::applyApplicationChrome();

    testMediumTitleReadable();
    testLongTitleHover();
    testEmptyFallback();

    if (g_failed != 0) {
        std::fprintf(stderr, "%d snackbar assertion(s) failed\n", g_failed);
        return 1;
    }
    std::fprintf(stdout, "ALL GREEN\n");
    return 0;
}
