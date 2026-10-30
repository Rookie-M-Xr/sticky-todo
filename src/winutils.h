#pragma once

#include <QImage>
#include <QString>

class QWidget;

namespace WinUtils {

bool acquireSingleInstance();
bool notifyExistingInstance();
unsigned int activationMessage();

void applyCommandLineDataDir();

bool setAutoStart(bool enabled, QString *errorMessage);
bool autoStartEnabled(QString *command = nullptr);

void setTopMost(QWidget *widget, bool on);
void activateWindow(QWidget *widget);
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

} // namespace WinUtils
