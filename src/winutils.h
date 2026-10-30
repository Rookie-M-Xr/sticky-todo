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

// Probe: mkpath + real write of a throwaway file, then delete.
bool directoryLooksWritable(const QString &dir);
// Windows: clear FILE_ATTRIBUTE_READONLY on dir + immediate files.
// Linux: chmod u+w on dir when needed.
bool tryClearDirectoryReadOnly(const QString &dir);
// mkpath + probe; if fail, tryClearDirectoryReadOnly then probe again.
bool ensureDirectoryWritable(const QString &dir);
// %LOCALAPPDATA%\StickyTodo\data (Win) or $XDG_DATA_HOME/StickyTodo/data (Linux).
QString stickyTodoLocalDataDir();
// Copy known JSON (+ .bak) from srcDir into dstDir when dest file is missing.
int migrateDataFiles(const QString &srcDir, const QString &dstDir);

// True when the process token is below Medium IL (Windows MIC). Linux: false.
bool isProcessLowIntegrity();
// TR #15: Low cannot silently raise to Medium. Logs a skip notice and always
// returns false so this process continues in-process (degraded). Never exits
// as if a Medium child took over.
bool handoffIfLowIntegrity();

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
