#include "mainwindow.h"
#include "theme.h"
#include "winutils.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    WinUtils::applyCommandLineDataDir();

    if (!WinUtils::acquireSingleInstance()) {
        WinUtils::notifyExistingInstance();
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    app.setApplicationName(QStringLiteral("Sticky Todo"));
    app.setApplicationDisplayName(QStringLiteral("Sticky Todo"));
    app.setOrganizationName(QStringLiteral("StickyTodo"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    app.setStyle(QStringLiteral("Fusion"));
    app.setFont(Theme::uiFont(13));
    app.setStyleSheet(Theme::styleSheet());

    WinUtils::setAppUserModelId();
    WinUtils::clearDownloadedFileMarks();

    MainWindow window;
    window.show();
    WinUtils::activateWindow(&window);
    return app.exec();
}
