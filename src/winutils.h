#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

class QWidget;

namespace WinUtils {

bool acquireSingleInstance();
bool notifyExistingInstance();
unsigned int activationMessage();

void applyCommandLineDataDir();

bool setAutoStart(bool enabled, QString *errorMessage, const QString &dataDir = QString());
bool autoStartEnabled(QString *command = nullptr);

void setTopMost(QWidget *widget, bool on);
void activateWindow(QWidget *widget);
bool isOsForeground(QWidget *widget);
void setAppUserModelId();
void prepareRoundedPopup(QWidget *popup);
void suppressSystemFrame(QWidget *window);

bool trayAvailable();
unsigned int trayCallbackMessage();
bool installTrayIcon(QWidget *owner, const QImage &image, const QString &tip);
void updateTrayTip(QWidget *owner, const QString &tip);
void showTrayBalloon(QWidget *owner, const QString &title, const QString &message);
void removeTrayIcon(QWidget *owner);
bool promoteTrayIcon();
QString lastTrayError();
void clearDownloadedFileMarks();

QString windowsErrorText(unsigned long code);

// Global toggle hotkey (show/focus or hide). Sequence text uses Qt-ish form,
// e.g. "Ctrl+Alt+T". Empty clears. Win: RegisterHotKey; Linux: X11 grab (Wayland no).
bool globalHotkeySupported(QString *reason = nullptr);
QString normalizeGlobalHotkey(const QString &text);
bool setGlobalHotkey(QWidget *owner, const QString &normalized, QString *error = nullptr);
void clearGlobalHotkey();
bool isGlobalHotkeyEvent(const QByteArray &eventType, void *message);

} // namespace WinUtils
