// Portable Linux stubs for WinUtils (box / Linux packaging only).
// Keeps the Windows winutils.cpp untouched; build_linux.sh compiles this instead.

#include "winutils.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QLockFile>
#include <QStringList>
#include <QWidget>
#include <QWindow>

#include <memory>
#include <vector>

namespace {

std::unique_ptr<QLockFile> g_instanceLock;

void applyDataDirFromArgs(const QStringList &args)
{
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--data-dir") && !args.at(i + 1).isEmpty()) {
            qputenv("STICKY_TODO_DATA_DIR", args.at(i + 1).toUtf8());
            break;
        }
    }
}

} // namespace

namespace WinUtils {

bool acquireSingleInstance()
{
    if (g_instanceLock)
        return true;
    const QString path = QDir::temp().filePath(QStringLiteral("sticky-todo-single-instance.lock"));
    auto lock = std::make_unique<QLockFile>(path);
    lock->setStaleLockTime(0);
    if (!lock->tryLock(100))
        return false;
    g_instanceLock = std::move(lock);
    return true;
}

bool notifyExistingInstance()
{
    return false;
}

unsigned int activationMessage()
{
    return 0;
}

void applyCommandLineDataDir()
{
    // Called before QApplication exists; read /proc/self/cmdline.
    QFile cmdline(QStringLiteral("/proc/self/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = cmdline.readAll();
    QStringList args;
    int start = 0;
    for (int i = 0; i < raw.size(); ++i) {
        if (raw.at(i) == '\0') {
            if (i > start)
                args.append(QString::fromLocal8Bit(raw.constData() + start, i - start));
            start = i + 1;
        }
    }
    if (start < raw.size())
        args.append(QString::fromLocal8Bit(raw.constData() + start, raw.size() - start));
    applyDataDirFromArgs(args);
}

bool setAutoStart(bool enabled, QString *errorMessage, const QString &dataDir)
{
    Q_UNUSED(enabled);
    Q_UNUSED(dataDir);
    if (errorMessage)
        *errorMessage = QStringLiteral("Linux 构建暂不支持开机自启");
    return false;
}

bool autoStartEnabled(QString *command)
{
    if (command)
        command->clear();
    return false;
}

void setTopMost(QWidget *widget, bool on)
{
    if (!widget)
        return;
    const bool wasVisible = widget->isVisible();
    widget->setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (wasVisible)
        widget->show();
}

void activateWindow(QWidget *widget)
{
    if (!widget)
        return;
    widget->show();
    widget->raise();
    widget->activateWindow();
    if (QWindow *window = widget->windowHandle())
        window->requestActivate();
}

void setAppUserModelId()
{
}

void prepareRoundedPopup(QWidget *popup)
{
    Q_UNUSED(popup);
}

void suppressSystemFrame(QWidget *window)
{
    Q_UNUSED(window);
}

bool trayAvailable()
{
    return false;
}

unsigned int trayCallbackMessage()
{
    return 0;
}

bool installTrayIcon(QWidget *owner, const QImage &image, const QString &tip)
{
    Q_UNUSED(owner);
    Q_UNUSED(image);
    Q_UNUSED(tip);
    return false;
}

void updateTrayTip(QWidget *owner, const QString &tip)
{
    Q_UNUSED(owner);
    Q_UNUSED(tip);
}

void showTrayBalloon(QWidget *owner, const QString &title, const QString &message)
{
    Q_UNUSED(owner);
    Q_UNUSED(title);
    Q_UNUSED(message);
}

void removeTrayIcon(QWidget *owner)
{
    Q_UNUSED(owner);
}

bool promoteTrayIcon()
{
    return false;
}

QString lastTrayError()
{
    return QStringLiteral("tray not implemented on Linux stub");
}

void clearDownloadedFileMarks()
{
}

QString windowsErrorText(unsigned long code)
{
    return QStringLiteral("error %1").arg(code);
}

} // namespace WinUtils
