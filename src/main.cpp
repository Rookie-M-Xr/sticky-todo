#include "mainwindow.h"
#include "theme.h"
#include "winutils.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    WinUtils::applyCommandLineDataDir();

    // TR #15: Low IL cannot silently raise to Medium. Log-only diagnostics;
    // never exit as if a Medium child took over. The note continues here
    // (degraded: typically no tray) and shows an in-app banner.
    WinUtils::handoffIfLowIntegrity();

    if (!WinUtils::acquireSingleInstance()) {
        WinUtils::notifyExistingInstance();
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    app.setApplicationName(QStringLiteral("Sticky Todo"));
    app.setApplicationDisplayName(QStringLiteral("Sticky Todo"));
    app.setOrganizationName(QStringLiteral("StickyTodo"));
    app.setApplicationVersion(QStringLiteral("0.1.1"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    app.setStyle(QStringLiteral("Fusion"));
    app.setFont(Theme::uiFont(13));
    Theme::applyApplicationChrome();

    WinUtils::setAppUserModelId();
    WinUtils::clearDownloadedFileMarks();

    MainWindow window;
    window.show();
    WinUtils::activateWindow(&window);
    return app.exec();
}
