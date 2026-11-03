#include "models.h"
#include "theme.h"
#include "widgets.h"

#include <QApplication>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QStringList>
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
    for (int minutes : kReminderSnoozePresets)
        actions.append({reminderSnoozePresetLabel(minutes), [] {}});
    return actions;
}

QVector<QPair<QString, std::function<void()>>> dismissAction()
{
    QVector<QPair<QString, std::function<void()>>> actions;
    actions.append({reminderDismissLabel(), [] {}});
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
    bar->showActions(reminderFireLabel(title), snoozeActions(), 0, reminderSnoozeMenuLabel(),
                     dismissAction(), reminderCustomSnoozePlaceholder(), [](int) {});
    bar->setGeometry(18, 400, width, bar->height());
    QApplication::processEvents();
}

QStringList visibleButtonTexts(Snackbar *bar)
{
    QStringList texts;
    for (QPushButton *button : visibleActionButtons(bar))
        texts.append(button->text());
    return texts;
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

    const QStringList texts = visibleButtonTexts(&bar);
    expect(texts.size() == 2, "snack-mid: dismiss + snooze overflow");
    expect(texts.contains(reminderDismissLabel()), "snack-mid: 忽略 visible");
    expect(texts.contains(reminderSnoozeMenuLabel()), "snack-mid: overflow label 延后 ▾");
    expect(!texts.contains(QStringLiteral("延后 5")),
           "snack-mid: presets stay inside menu");

    if (g_failed == before)
        pass("snackbar medium title readable (#77 + #68 dismiss)");
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
    expect(visibleActionButtons(&bar).size() == 2, "snack-long: dismiss + one snooze control");
    expect(visibleButtonTexts(&bar).contains(reminderDismissLabel()),
           "snack-long: 忽略 still visible");

    if (g_failed == before)
        pass("snackbar long title start + tooltip (#77 + #68)");
}

void testDismissClick()
{
    const int before = g_failed;
    QWidget card;
    card.resize(368, 508);
    card.show();
    Snackbar bar(&card);
    bool ignored = false;
    QVector<QPair<QString, std::function<void()>>> leading;
    leading.append({reminderDismissLabel(), [&ignored] { ignored = true; }});
    bar.showActions(reminderFireLabel(QStringLiteral("喝水")), snoozeActions(), 0,
                    reminderSnoozeMenuLabel(), leading, reminderCustomSnoozePlaceholder(),
                    [](int) {});
    bar.setGeometry(18, 400, 332, bar.height());
    QApplication::processEvents();

    bool clicked = false;
    for (QPushButton *button : visibleActionButtons(&bar)) {
        if (button && button->text() == reminderDismissLabel()) {
            button->click();
            clicked = true;
            break;
        }
    }
    expect(clicked, "snack-dismiss: found 忽略");
    expect(ignored, "snack-dismiss: click runs dismiss callback");
    if (g_failed == before)
        pass("snackbar dismiss this occurrence click (#68)");
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
    testDismissClick();
    testEmptyFallback();

    if (g_failed != 0) {
        std::fprintf(stderr, "%d snackbar assertion(s) failed\n", g_failed);
        return 1;
    }
    std::fprintf(stdout, "ALL GREEN\n");
    return 0;
}
