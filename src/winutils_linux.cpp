// Portable Linux stubs for WinUtils (box / Linux packaging only).
// Keeps the Windows winutils.cpp untouched; build_linux.sh compiles this instead.

#include "winutils.h"

#include <QByteArray>
#include <QGuiApplication>
#include <qguiapplication_platform.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QIODevice>
#include <QLockFile>
#include <QStringList>
#include <QWidget>
#include <QWindow>

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
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


bool directoryLooksWritable(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return false;
    if (!QDir().mkpath(dir))
        return false;
    const QString probe = QDir(dir).filePath(QStringLiteral(".sticky-todo-write-probe"));
    QSaveFile file(probe);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray marker = QByteArrayLiteral("sticky-todo-write-probe");
    if (file.write(marker) != marker.size())
        return false;
    if (!file.commit())
        return false;
    QFile::remove(probe);
    return true;
}

bool tryClearDirectoryReadOnly(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return false;
    bool touched = false;
    const QByteArray path = QFile::encodeName(dir);
    struct stat st;
    if (::stat(path.constData(), &st) == 0) {
        const mode_t next = st.st_mode | S_IWUSR;
        if (next != st.st_mode && ::chmod(path.constData(), next) == 0)
            touched = true;
    }
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        const QByteArray fp = QFile::encodeName(fi.absoluteFilePath());
        if (::stat(fp.constData(), &st) != 0)
            continue;
        const mode_t next = st.st_mode | S_IWUSR;
        if (next != st.st_mode && ::chmod(fp.constData(), next) == 0)
            touched = true;
    }
    return touched;
}

bool ensureDirectoryWritable(const QString &dir)
{
    if (directoryLooksWritable(dir))
        return true;
    tryClearDirectoryReadOnly(dir);
    return directoryLooksWritable(dir);
}

QString stickyTodoLocalDataDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        return QDir::home().filePath(QStringLiteral(".local/share/StickyTodo/data"));
    return QDir(base).filePath(QStringLiteral("StickyTodo/data"));
}

int migrateDataFiles(const QString &srcDir, const QString &dstDir)
{
    if (srcDir.isEmpty() || dstDir.isEmpty())
        return 0;
    if (QDir::cleanPath(srcDir) == QDir::cleanPath(dstDir))
        return 0;
    if (!QDir().mkpath(dstDir))
        return 0;

    static const char *const kNames[] = {
        "todos.json",
        "todos.json.bak",
        "settings.json",
        "settings.json.bak",
        "todos-archive.json",
        "daily-reports.json",
        "weekly-reports.json",
        "monthly-reports.json",
    };
    int copied = 0;
    for (const char *name : kNames) {
        const QString src = QDir(srcDir).filePath(QString::fromUtf8(name));
        const QString dst = QDir(dstDir).filePath(QString::fromUtf8(name));
        if (!QFileInfo::exists(src) || QFileInfo::exists(dst))
            continue;
        if (QFile::copy(src, dst))
            ++copied;
    }
    return copied;
}

bool isProcessLowIntegrity()
{
    return false;
}

bool handoffIfLowIntegrity()
{
    return false;
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


bool isOsForeground(QWidget *widget)
{
    if (!widget)
        return false;
    // X11: isActiveWindow is generally accurate. Also accept Qt focusWindow match.
    // Wayland: best-effort only (compositor may not expose true OS foreground).
    if (widget->isActiveWindow())
        return true;
    QWindow *fw = QGuiApplication::focusWindow();
    QWindow *ww = widget->windowHandle();
    return fw && ww && (fw == ww);
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

bool globalHotkeySupported(QString *reason)
{
    const QString platform = QGuiApplication::platformName();
    if (platform.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
        if (reason)
            *reason = QStringLiteral("Wayland 下系统级全局热键受限，当前构建不支持");
        return false;
    }
    if (!platform.contains(QLatin1String("xcb"), Qt::CaseInsensitive)
        && platform != QLatin1String("x11")) {
        if (reason)
            *reason = QStringLiteral("当前平台（%1）不支持全局热键").arg(platform);
        return false;
    }
    return true;
}

namespace {

constexpr int kGrabKeycodeUnset = -1;

struct XlibApi {
    void *lib = nullptr;
    using XGrabKeyFn = int (*)(void *display, int keycode, unsigned int modifiers, unsigned long grab_window,
                               int owner_events, int pointer_mode, int keyboard_mode);
    using XUngrabKeyFn = int (*)(void *display, int keycode, unsigned int modifiers, unsigned long grab_window);
    using XKeysymToKeycodeFn = unsigned char (*)(void *display, unsigned long keysym);
    using XFlushFn = int (*)(void *display);
    using XDefaultRootWindowFn = unsigned long (*)(void *display);
    XGrabKeyFn XGrabKey = nullptr;
    XUngrabKeyFn XUngrabKey = nullptr;
    XKeysymToKeycodeFn XKeysymToKeycode = nullptr;
    XFlushFn XFlush = nullptr;
    XDefaultRootWindowFn XDefaultRootWindow = nullptr;
};

XlibApi g_xlib;
void *g_display = nullptr;
unsigned long g_root = 0;
int g_keycode = kGrabKeycodeUnset;
unsigned int g_mods = 0;
QString g_hotkeyText;
bool g_grabbed = false;

// X11 masks (from X.h)
constexpr unsigned int kShiftMask = 1u << 0;
constexpr unsigned int kLockMask = 1u << 1;
constexpr unsigned int kControlMask = 1u << 2;
constexpr unsigned int kMod1Mask = 1u << 3; // Alt
constexpr unsigned int kMod2Mask = 1u << 4; // NumLock often
constexpr unsigned int kMod4Mask = 1u << 6; // Super

// XCB
constexpr unsigned char kXcbKeyPress = 2;

struct XcbKeyPressEvent {
    unsigned char response_type;
    unsigned char detail; // keycode
    unsigned short sequence;
    unsigned int time;
    unsigned int root;
    unsigned int event;
    unsigned int child;
    short root_x;
    short root_y;
    short event_x;
    short event_y;
    unsigned short state;
    unsigned char same_screen;
    unsigned char pad0;
};

bool loadXlib()
{
    if (g_xlib.lib)
        return g_xlib.XGrabKey != nullptr;
    g_xlib.lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!g_xlib.lib)
        return false;
    g_xlib.XGrabKey = reinterpret_cast<XlibApi::XGrabKeyFn>(dlsym(g_xlib.lib, "XGrabKey"));
    g_xlib.XUngrabKey = reinterpret_cast<XlibApi::XUngrabKeyFn>(dlsym(g_xlib.lib, "XUngrabKey"));
    g_xlib.XKeysymToKeycode = reinterpret_cast<XlibApi::XKeysymToKeycodeFn>(dlsym(g_xlib.lib, "XKeysymToKeycode"));
    g_xlib.XFlush = reinterpret_cast<XlibApi::XFlushFn>(dlsym(g_xlib.lib, "XFlush"));
    g_xlib.XDefaultRootWindow = reinterpret_cast<XlibApi::XDefaultRootWindowFn>(dlsym(g_xlib.lib, "XDefaultRootWindow"));
    return g_xlib.XGrabKey && g_xlib.XUngrabKey && g_xlib.XKeysymToKeycode && g_xlib.XFlush
        && g_xlib.XDefaultRootWindow;
}

QStringList splitHotkeyTokens(const QString &text)
{
    QStringList parts;
    for (const QString &raw : text.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
        const QString t = raw.trimmed();
        if (!t.isEmpty())
            parts.append(t);
    }
    return parts;
}

bool mapHotkeyToken(const QString &token, unsigned int *mods, unsigned long *keysym, QString *canonPart)
{
    const QString lower = token.toLower();
    if (lower == QLatin1String("ctrl") || lower == QLatin1String("control")) {
        *mods |= kControlMask;
        if (canonPart)
            *canonPart = QStringLiteral("Ctrl");
        return true;
    }
    if (lower == QLatin1String("alt") || lower == QLatin1String("option")) {
        *mods |= kMod1Mask;
        if (canonPart)
            *canonPart = QStringLiteral("Alt");
        return true;
    }
    if (lower == QLatin1String("shift")) {
        *mods |= kShiftMask;
        if (canonPart)
            *canonPart = QStringLiteral("Shift");
        return true;
    }
    if (lower == QLatin1String("win") || lower == QLatin1String("meta")
        || lower == QLatin1String("super")) {
        *mods |= kMod4Mask;
        if (canonPart)
            *canonPart = QStringLiteral("Win");
        return true;
    }
    if (token.size() == 1) {
        const QChar ch = token.at(0).toLower();
        if (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) {
            // XK_a = 0x0061
            *keysym = 0x0061u + static_cast<unsigned long>(ch.unicode() - QLatin1Char('a').unicode());
            if (canonPart)
                *canonPart = QString(ch.toUpper());
            return true;
        }
        if (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) {
            *keysym = 0x0030u + static_cast<unsigned long>(ch.unicode() - QLatin1Char('0').unicode());
            if (canonPart)
                *canonPart = QString(ch);
            return true;
        }
    }
    if (lower.size() >= 2 && lower.startsWith(QLatin1Char('f'))) {
        bool ok = false;
        const int n = lower.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 12) {
            // XK_F1 = 0xffbe
            *keysym = 0xffbeu + static_cast<unsigned long>(n - 1);
            if (canonPart)
                *canonPart = QStringLiteral("F%1").arg(n);
            return true;
        }
    }
    return false;
}

bool parseHotkey(const QString &text, unsigned int *mods, unsigned long *keysym, QString *normalized)
{
    *mods = 0;
    *keysym = 0;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (normalized)
            normalized->clear();
        return true;
    }
    const QStringList parts = splitHotkeyTokens(trimmed);
    if (parts.isEmpty())
        return false;
    QString canonKey;
    for (const QString &part : parts) {
        QString canon;
        unsigned long before = *keysym;
        if (!mapHotkeyToken(part, mods, keysym, &canon))
            return false;
        if (*keysym != before && *keysym != 0) {
            if (!canonKey.isEmpty())
                return false;
            canonKey = canon;
        }
    }
    if (canonKey.isEmpty() || *mods == 0)
        return false;
    QStringList ordered;
    if (*mods & kControlMask)
        ordered.append(QStringLiteral("Ctrl"));
    if (*mods & kMod1Mask)
        ordered.append(QStringLiteral("Alt"));
    if (*mods & kShiftMask)
        ordered.append(QStringLiteral("Shift"));
    if (*mods & kMod4Mask)
        ordered.append(QStringLiteral("Win"));
    ordered.append(canonKey);
    if (normalized)
        *normalized = ordered.join(QLatin1Char('+'));
    return true;
}

void ungrabAll()
{
    if (!g_grabbed || !g_display || g_keycode == kGrabKeycodeUnset || !g_xlib.XUngrabKey)
        return;
    const unsigned int ignoreMasks[] = {0, kLockMask, kMod2Mask, kLockMask | kMod2Mask};
    for (unsigned int extra : ignoreMasks)
        g_xlib.XUngrabKey(g_display, g_keycode, g_mods | extra, g_root);
    if (g_xlib.XFlush)
        g_xlib.XFlush(g_display);
    g_grabbed = false;
    g_keycode = kGrabKeycodeUnset;
    g_mods = 0;
    g_hotkeyText.clear();
}

} // namespace

QString normalizeGlobalHotkey(const QString &text)
{
    unsigned int mods = 0;
    unsigned long keysym = 0;
    QString normalized;
    if (!parseHotkey(text, &mods, &keysym, &normalized))
        return QString();
    return normalized;
}

bool setGlobalHotkey(QWidget *owner, const QString &normalized, QString *error)
{
    Q_UNUSED(owner);
    if (error)
        error->clear();
    clearGlobalHotkey();
    const QString text = normalized.trimmed();
    if (text.isEmpty())
        return true;

    QString why;
    if (!globalHotkeySupported(&why)) {
        if (error)
            *error = why;
        return false;
    }
    if (!loadXlib()) {
        if (error)
            *error = QStringLiteral("无法加载 libX11，全局热键不可用");
        return false;
    }

    auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !x11->display()) {
        if (error)
            *error = QStringLiteral("无 X11 Display，全局热键不可用");
        return false;
    }
    g_display = x11->display();
    g_root = g_xlib.XDefaultRootWindow(g_display);

    unsigned int mods = 0;
    unsigned long keysym = 0;
    QString canon;
    if (!parseHotkey(text, &mods, &keysym, &canon) || canon.isEmpty()) {
        if (error)
            *error = QStringLiteral("快捷键格式无效，请使用如 Ctrl+Alt+T");
        return false;
    }
    const unsigned char keycode = g_xlib.XKeysymToKeycode(g_display, keysym);
    if (!keycode) {
        if (error)
            *error = QStringLiteral("无法映射按键到键码");
        return false;
    }

    // GrabAsync = 1. Conflicts surface as async X errors; we still mark grabbed.
    constexpr int GrabModeAsync = 1;
    const unsigned int ignoreMasks[] = {0, kLockMask, kMod2Mask, kLockMask | kMod2Mask};
    for (unsigned int extra : ignoreMasks) {
        g_xlib.XGrabKey(g_display, static_cast<int>(keycode), mods | extra, g_root,
                        1 /* owner_events */, GrabModeAsync, GrabModeAsync);
    }
    if (g_xlib.XFlush)
        g_xlib.XFlush(g_display);
    g_keycode = static_cast<int>(keycode);
    g_mods = mods;
    g_hotkeyText = canon;
    g_grabbed = true;
    return true;
}

void clearGlobalHotkey()
{
    ungrabAll();
    g_display = nullptr;
    g_root = 0;
}

bool isGlobalHotkeyEvent(const QByteArray &eventType, void *message)
{
    if (!g_grabbed || !message || g_keycode == kGrabKeycodeUnset)
        return false;
    if (eventType != "xcb_generic_event_t")
        return false;
    auto *generic = static_cast<const unsigned char *>(message);
    // response_type is first byte; strip 0x80 send-event bit
    const unsigned char type = static_cast<unsigned char>(generic[0] & ~0x80);
    if (type != kXcbKeyPress)
        return false;
    const auto *kp = reinterpret_cast<const XcbKeyPressEvent *>(message);
    if (static_cast<int>(kp->detail) != g_keycode)
        return false;
    const unsigned int relevant = kShiftMask | kControlMask | kMod1Mask | kMod4Mask;
    return (kp->state & relevant) == (g_mods & relevant);
}


} // namespace WinUtils
