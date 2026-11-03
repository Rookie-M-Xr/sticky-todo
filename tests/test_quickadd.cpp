#include "models.h"
#include "theme.h"
#include "widgets.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QWidget>

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

void testConfirmEmitsTrimmedAndHides()
{
    const int before = g_failed;
    QuickAddPopup popup;
    QString accepted;
    int cancels = 0;
    QObject::connect(&popup, &QuickAddPopup::accepted, [&](const QString &text) {
        accepted = text;
    });
    QObject::connect(&popup, &QuickAddPopup::cancelled, [&] { ++cancels; });

    popup.openNear(QPoint(80, 80));
    QApplication::processEvents();
    expect(popup.isVisible(), "confirm: popup visible");

    popup.setText(QStringLiteral("  买牛奶  "));
    popup.confirm();
    QApplication::processEvents();

    expect(accepted == QStringLiteral("买牛奶"), "confirm: trimmed text");
    expect(cancels == 0, "confirm: no cancel");
    expect(!popup.isVisible(), "confirm: popup hides");

    if (g_failed == before)
        pass("quick-add confirm emits trimmed text");
}

void testEmptyConfirmWritesNothing()
{
    const int before = g_failed;
    QuickAddPopup popup;
    bool accepted = false;
    QObject::connect(&popup, &QuickAddPopup::accepted, [&](const QString &) { accepted = true; });

    popup.openNear(QPoint(80, 80));
    QApplication::processEvents();
    popup.setText(QStringLiteral("   "));
    popup.confirm();
    QApplication::processEvents();

    expect(!accepted, "empty: no accepted signal");
    expect(popup.isVisible(), "empty: stays open");

    if (g_failed == before)
        pass("quick-add empty confirm writes nothing");
}

void testCancelDoesNotAccept()
{
    const int before = g_failed;
    QuickAddPopup popup;
    bool accepted = false;
    int cancels = 0;
    QObject::connect(&popup, &QuickAddPopup::accepted, [&](const QString &) { accepted = true; });
    QObject::connect(&popup, &QuickAddPopup::cancelled, [&] { ++cancels; });

    popup.openNear(QPoint(80, 80));
    QApplication::processEvents();
    popup.setText(QStringLiteral("不该写入"));
    popup.cancel();
    QApplication::processEvents();

    expect(!accepted, "cancel: no accepted signal");
    expect(cancels == 1, "cancel: cancelled once");
    expect(!popup.isVisible(), "cancel: popup hides");

    if (g_failed == before)
        pass("quick-add cancel does not accept");
}

void testEscapeCancels()
{
    const int before = g_failed;
    QuickAddPopup popup;
    bool accepted = false;
    int cancels = 0;
    QObject::connect(&popup, &QuickAddPopup::accepted, [&](const QString &) { accepted = true; });
    QObject::connect(&popup, &QuickAddPopup::cancelled, [&] { ++cancels; });

    popup.openNear(QPoint(80, 80));
    QApplication::processEvents();
    popup.setText(QStringLiteral("esc"));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&popup, &escape);
    QApplication::processEvents();

    expect(!accepted, "escape: no accepted");
    expect(cancels == 1, "escape: cancelled");
    expect(!popup.isVisible(), "escape: hides");

    if (g_failed == before)
        pass("quick-add Escape cancels");
}

void testDoesNotRaiseHiddenHost()
{
    const int before = g_failed;
    QWidget host;
    host.setObjectName(QStringLiteral("mainHost"));
    host.resize(240, 180);
    host.show();
    QApplication::processEvents();
    host.hide();
    expect(!host.isVisible(), "host: hidden before popup");

    QuickAddPopup popup;
    popup.openNear(QPoint(40, 40));
    QApplication::processEvents();
    expect(popup.isVisible(), "host: popup visible");
    expect(!host.isVisible(), "host: still hidden while typing");

    popup.setText(QStringLiteral("托盘添加"));
    popup.confirm();
    QApplication::processEvents();
    expect(!host.isVisible(), "host: still hidden after confirm");

    if (g_failed == before)
        pass("quick-add does not raise a hidden host");
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    Theme::applyApplicationChrome();

    testConfirmEmitsTrimmedAndHides();
    testEmptyConfirmWritesNothing();
    testCancelDoesNotAccept();
    testEscapeCancels();
    testDoesNotRaiseHiddenHost();

    if (g_failed != 0) {
        std::fprintf(stderr, "%d quick-add assertion(s) failed\n", g_failed);
        return 1;
    }
    std::fprintf(stdout, "ALL GREEN\n");
    return 0;
}
