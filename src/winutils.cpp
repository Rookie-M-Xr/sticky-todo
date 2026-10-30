#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include "winutils.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QStringList>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QIODevice>
#include <QImage>
#include <QWidget>

#include <cstring>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winreg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <exdisp.h>
#include <shldisp.h>
#include <shlobj.h>
#include <servprov.h>
#include <shlguid.h>
#include <taskschd.h>
#include <wbemidl.h>

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Sticky Todo";
constexpr UINT kTrayId = 1;
constexpr UINT kTrayCallback = WM_APP + 0x4B1;
constexpr wchar_t kTrayClass[] = L"StickyTodoTrayWindow";
// Stable identity so Windows can remember the icon across launches.
const GUID kTrayGuid = {
    0xE7A1C4B2, 0x6F3D, 0x4A91, {0x9C, 0x2E, 0x8B, 0x5D, 0x0F, 0x1A, 0x3C, 0x47}};
HANDLE g_instanceMutex = nullptr;
HICON g_trayIcon = nullptr;
HWND g_trayHwnd = nullptr;
HWND g_forwardHwnd = nullptr;
UINT g_taskbarCreated = 0;
bool g_trayAdded = false;
bool g_trayUseGuid = true;
QString g_trayTip;
QString g_trayError;

void reinstallCachedTrayIcon();

void copyLimited(wchar_t *dest, int capacity, const QString &text)
{
    if (!dest || capacity <= 0)
        return;
    const std::wstring wide = text.toStdWString();
    const int count = qMin(capacity - 1, static_cast<int>(wide.size()));
    if (count > 0)
        memcpy(dest, wide.data(), static_cast<size_t>(count) * sizeof(wchar_t));
    dest[qMax(0, count)] = L'\0';
}

int trayPixelSize(HWND hwnd)
{
    UINT dpi = 96;
    const HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (tray)
        dpi = GetDpiForWindow(tray);
    else if (hwnd)
        dpi = GetDpiForWindow(hwnd);
    int px = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    if (px < 16)
        px = GetSystemMetrics(SM_CXSMICON);
    if (px < 16)
        px = 16;
    return px;
}

HICON iconFromModule(int px)
{
    HICON icon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
        px, px, LR_DEFAULTCOLOR));
    if (icon)
        return icon;

    const QString path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    HICON large = nullptr;
    HICON small = nullptr;
    if (ExtractIconExW(reinterpret_cast<LPCWSTR>(path.utf16()), 0, &large, &small, 1) > 0) {
        if (small) {
            if (large)
                DestroyIcon(large);
            return small;
        }
        return large;
    }
    return nullptr;
}

HICON iconForTray(HWND hwnd, const QImage &source)
{
    const int px = trayPixelSize(hwnd);
    QImage image = source;
    if (image.isNull()) {
        image = QImage(px, px, QImage::Format_ARGB32);
        image.fill(qRgba(244, 214, 122, 255));
    }
    image = image.scaled(px, px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_ARGB32);
    image.setDevicePixelRatio(1.0);
    return image.toHICON();
}

void fillTrayData(NOTIFYICONDATAW *data, HWND hwnd, HICON icon, const QString &tip, DWORD flags)
{
    memset(data, 0, sizeof(*data));
    data->cbSize = sizeof(*data);
    data->hWnd = hwnd;
    data->uID = kTrayId;
    data->uFlags = flags;
    data->uCallbackMessage = kTrayCallback;
    data->hIcon = icon;
    if (g_trayUseGuid) {
        data->guidItem = kTrayGuid;
        data->uFlags |= NIF_GUID;
    }
    if (flags & NIF_TIP)
        copyLimited(data->szTip, 128, tip);
}

LRESULT CALLBACK trayWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (g_taskbarCreated && message == g_taskbarCreated) {
        reinstallCachedTrayIcon();
        return 0;
    }
    if (message == kTrayCallback && g_forwardHwnd && IsWindow(g_forwardHwnd)) {
        PostMessageW(g_forwardHwnd, message, wParam, lParam);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND ensureTrayWindow()
{
    if (g_trayHwnd && IsWindow(g_trayHwnd))
        return g_trayHwnd;

    if (!g_taskbarCreated)
        g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = trayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kTrayClass;
    RegisterClassExW(&wc);

    // Own the icon from a normal hidden window. The visible note is a frameless
    // tool window, and that HWND is a poor owner for Shell_NotifyIcon.
    g_trayHwnd = CreateWindowExW(
        0, kTrayClass, L"StickyTodoTray", WS_OVERLAPPED,
        0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    return g_trayHwnd;
}

bool notifyAddOrModify(NOTIFYICONDATAW *data)
{
    SetLastError(0);
    if (Shell_NotifyIconW(NIM_ADD, data))
        return true;
    const DWORD addError = GetLastError();
    if (Shell_NotifyIconW(NIM_MODIFY, data))
        return true;
    g_trayError = WinUtils::windowsErrorText(addError ? addError : GetLastError());
    return false;
}

void applyTrayVersion(NOTIFYICONDATAW *data)
{
    data->uVersion = NOTIFYICON_VERSION_4;
    data->uFlags = g_trayUseGuid ? NIF_GUID : 0;
    Shell_NotifyIconW(NIM_SETVERSION, data);
    data->uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP | (g_trayUseGuid ? NIF_GUID : 0);
    Shell_NotifyIconW(NIM_MODIFY, data);
}

void reinstallCachedTrayIcon()
{
    if (!g_trayHwnd || !g_trayIcon)
        return;
    NOTIFYICONDATAW data;
    fillTrayData(&data, g_trayHwnd, g_trayIcon, g_trayTip, NIF_MESSAGE | NIF_ICON | NIF_TIP);
    if (!notifyAddOrModify(&data))
        return;
    applyTrayVersion(&data);
    g_trayAdded = true;
    SetWindowTextW(g_trayHwnd, L"StickyTodoTray OK");
}

bool isEphemeralDataDir(const QString &dataDir)
{
    const QString cleaned = QDir::cleanPath(dataDir);
    if (cleaned.isEmpty())
        return true;
    const QString tempRoot = QDir::cleanPath(QDir::tempPath());
    if (cleaned.compare(tempRoot, Qt::CaseInsensitive) == 0)
        return true;
    const QString prefix = tempRoot + QLatin1Char('/');
    if (cleaned.startsWith(prefix, Qt::CaseInsensitive))
        return true;
    const QString native = QDir::toNativeSeparators(cleaned);
    const QString nativeTemp = QDir::toNativeSeparators(tempRoot) + QDir::separator();
    if (native.startsWith(nativeTemp, Qt::CaseInsensitive))
        return true;
    return false;
}

QString startupCommand(const QString &dataDir)
{
    const QString path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QString command = QStringLiteral("\"%1\"").arg(path);
    QString dir = dataDir.trimmed();
    if (dir.isEmpty())
        dir = qEnvironmentVariable("STICKY_TODO_DATA_DIR").trimmed();
    if (!dir.isEmpty())
        command += QStringLiteral(" --data-dir \"%1\"").arg(QDir::toNativeSeparators(dir));
    return command;
}

void applyDataDirFromCommandLine()
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return;
    for (int i = 1; i + 1 < argc; ++i) {
        if (wcscmp(argv[i], L"--data-dir") == 0 && argv[i + 1][0] != L'\0') {
            SetEnvironmentVariableW(L"STICKY_TODO_DATA_DIR", argv[i + 1]);
            break;
        }
    }
    LocalFree(argv);
}


bool isProcessLowIntegrity()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    DWORD length = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &length);
    if (length == 0) {
        CloseHandle(token);
        return false;
    }
    std::vector<unsigned char> buffer(length);
    if (!GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), length, &length)) {
        CloseHandle(token);
        return false;
    }
    CloseHandle(token);

    auto *til = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buffer.data());
    if (!til || !til->Label.Sid || !IsValidSid(til->Label.Sid))
        return false;
    const DWORD rid = *GetSidSubAuthority(
        til->Label.Sid,
        static_cast<DWORD>(*GetSidSubAuthorityCount(til->Label.Sid) - 1));
    return rid < SECURITY_MANDATORY_MEDIUM_RID;
}

QString moduleDirectory()
{
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    return QFileInfo(QString::fromWCharArray(path)).absolutePath();
}

bool probeWritableFile(const QString &candidatePath)
{
    // Low IL cannot write medium %TEMP%; probe with a real write rather than
    // trusting QDir::temp() / GetTempPath().
    QSaveFile probe(candidatePath);
    if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray marker = QByteArrayLiteral("sticky-todo-handoff-probe");
    if (probe.write(marker) != marker.size())
        return false;
    if (!probe.commit())
        return false;
    QFile::remove(candidatePath);
    return true;
}

QString chooseWritableHandoffScriptPath(const QString &exeDir, const QString &localApp)
{
    QStringList candidates;
    if (!exeDir.isEmpty())
        candidates << QDir(exeDir).filePath(QStringLiteral("StickyTodo-handoff.cmd"));
    if (!localApp.isEmpty()) {
        const QString lowTemp = QDir(localApp).filePath(QStringLiteral("Temp"));
        QDir().mkpath(lowTemp);
        candidates << QDir(lowTemp).filePath(QStringLiteral("StickyTodo-handoff.cmd"));
    }
    wchar_t tempBuf[MAX_PATH];
    const DWORD n = GetTempPathW(MAX_PATH, tempBuf);
    if (n > 0 && n < MAX_PATH) {
        const QString sysTemp = QString::fromWCharArray(tempBuf);
        candidates << QDir(sysTemp).filePath(QStringLiteral("StickyTodo-handoff.cmd"));
    }

    QStringList seen;
    for (const QString &raw : candidates) {
        const QString path = QDir::cleanPath(raw);
        if (path.isEmpty())
            continue;
        bool dup = false;
        for (const QString &s : seen) {
            if (s.compare(path, Qt::CaseInsensitive) == 0) {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;
        seen << path;
        if (probeWritableFile(path))
            return path;
    }
    return {};
}

bool writeHandoffScript(const QString &scriptPath, const QString &srcDir, const QString &dataDir)
{
    const QString src = QDir::toNativeSeparators(srcDir);
    const QString data = QDir::toNativeSeparators(dataDir);
    // Medium cmd (started via explorer's Shell.Application) copies package →
    // LocalAppData runtime then relaunches with the same --data-dir. A Low
    // process cannot raise its own IL; explorer-brokered ShellExecute yields Medium.
    QString body;
    body += QStringLiteral("@echo off\r\n");
    body += QStringLiteral("setlocal\r\n");
    body += QStringLiteral("set \"SRC=%1\"\r\n").arg(src);
    body += QStringLiteral("set \"DATA=%1\"\r\n").arg(data);
    body += QStringLiteral("set \"RUNTIME=%LOCALAPPDATA%\\StickyTodo\\runtime\"\r\n");
    body += QStringLiteral("mkdir \"%RUNTIME%\\platforms\" 2>nul\r\n");
    body += QStringLiteral("copy /Y \"%SRC%\\StickyTodo.exe\" \"%RUNTIME%\\StickyTodo.exe\" >nul\r\n");
    body += QStringLiteral("copy /Y \"%SRC%\\Qt6Core.dll\" \"%RUNTIME%\\\" >nul\r\n");
    body += QStringLiteral("copy /Y \"%SRC%\\Qt6Gui.dll\" \"%RUNTIME%\\\" >nul\r\n");
    body += QStringLiteral("copy /Y \"%SRC%\\Qt6Widgets.dll\" \"%RUNTIME%\\\" >nul\r\n");
    body += QStringLiteral("copy /Y \"%SRC%\\libgcc_s_seh-1.dll\" \"%RUNTIME%\\\" >nul\r\n");
    body += QStringLiteral("copy /Y \"%SRC%\\libstdc++-6.dll\" \"%RUNTIME%\\\" >nul\r\n");
    body += QStringLiteral("copy /Y \"%SRC%\\libwinpthread-1.dll\" \"%RUNTIME%\\\" >nul\r\n");
    body += QStringLiteral(
        "copy /Y \"%SRC%\\platforms\\qwindows.dll\" \"%RUNTIME%\\platforms\\\" >nul\r\n");
    body += QStringLiteral(
        "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"Get-ChildItem -LiteralPath $env:LOCALAPPDATA\\StickyTodo\\runtime -Recurse -File "
        "| Unblock-File\" >nul 2>&1\r\n");
    body += QStringLiteral("if not exist \"%RUNTIME%\\StickyTodo.exe\" exit /b 1\r\n");
    body += QStringLiteral(
        "start \"\" /D \"%RUNTIME%\" \"%RUNTIME%\\StickyTodo.exe\" --data-dir \"%DATA%\"\r\n");

    QSaveFile file(scriptPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = body.toLocal8Bit();
    if (file.write(bytes) != bytes.size())
        return false;
    return file.commit();
}


void appendHandoffLog(const QString &exeDir, const QString &line)
{
    if (exeDir.isEmpty())
        return;
    const QString path = QDir(exeDir).filePath(QStringLiteral("StickyTodo-handoff.log"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    f.write(line.toUtf8());
    f.write("\n");
}

bool launchHandoffViaWmi(const QString &scriptPath, const QString &workingDir, const QString &exeDir)
{
    // TR #15: WMI Win32_Process.Create is served out-of-proc and often starts the
    // command at the interactive user's Medium IL when Low COM brokers are blocked.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninit = false;
    if (hr == S_OK || hr == S_FALSE) {
        shouldUninit = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        appendHandoffLog(exeDir, QStringLiteral("wmi: CoInitializeEx hr=0x%1")
                                       .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        return false;
    }

    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    // RPC_E_TOO_LATE is fine if already set.
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        appendHandoffLog(exeDir, QStringLiteral("wmi: CoInitializeSecurity hr=0x%1")
                                       .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        // continue — some hosts already configured security
    }

    IWbemLocator *locator = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                          reinterpret_cast<void **>(&locator));
    if (FAILED(hr) || !locator) {
        appendHandoffLog(exeDir, QStringLiteral("wmi: WbemLocator hr=0x%1")
                                       .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IWbemServices *services = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\CIMV2");
    hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    SysFreeString(ns);
    locator->Release();
    if (FAILED(hr) || !services) {
        appendHandoffLog(exeDir, QStringLiteral("wmi: ConnectServer hr=0x%1")
                                       .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    hr = CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                           EOAC_NONE);
    if (FAILED(hr)) {
        appendHandoffLog(exeDir, QStringLiteral("wmi: ProxyBlanket hr=0x%1")
                                       .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        services->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IWbemClassObject *processClass = nullptr;
    BSTR className = SysAllocString(L"Win32_Process");
    hr = services->GetObject(className, 0, nullptr, &processClass, nullptr);
    SysFreeString(className);
    if (FAILED(hr) || !processClass) {
        appendHandoffLog(exeDir, QStringLiteral("wmi: GetObject hr=0x%1")
                                       .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        services->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IWbemClassObject *inParamsDef = nullptr;
    BSTR createName = SysAllocString(L"Create");
    hr = processClass->GetMethod(createName, 0, &inParamsDef, nullptr);
    processClass->Release();
    if (FAILED(hr) || !inParamsDef) {
        appendHandoffLog(exeDir, QStringLiteral("wmi: GetMethod hr=0x%1")
                                       .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        SysFreeString(createName);
        services->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IWbemClassObject *inParams = nullptr;
    hr = inParamsDef->SpawnInstance(0, &inParams);
    inParamsDef->Release();
    if (FAILED(hr) || !inParams) {
        appendHandoffLog(exeDir, QStringLiteral("wmi: SpawnInstance hr=0x%1")
                                       .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        SysFreeString(createName);
        services->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    const QString cmdLine = QStringLiteral("cmd.exe /c \"\"%1\"\"")
                                .arg(QDir::toNativeSeparators(scriptPath));
    const QString work = QDir::toNativeSeparators(workingDir);
    VARIANT vCmd;
    VARIANT vDir;
    VariantInit(&vCmd);
    VariantInit(&vDir);
    vCmd.vt = VT_BSTR;
    vCmd.bstrVal = SysAllocString(cmdLine.toStdWString().c_str());
    vDir.vt = VT_BSTR;
    vDir.bstrVal = SysAllocString(work.toStdWString().c_str());
    inParams->Put(L"CommandLine", 0, &vCmd, 0);
    inParams->Put(L"CurrentDirectory", 0, &vDir, 0);
    VariantClear(&vCmd);
    VariantClear(&vDir);

    IWbemClassObject *outParams = nullptr;
    BSTR processPath = SysAllocString(L"Win32_Process");
    hr = services->ExecMethod(processPath, createName, 0, nullptr, inParams, &outParams, nullptr);
    SysFreeString(processPath);
    SysFreeString(createName);
    inParams->Release();
    services->Release();

    bool ok = false;
    if (SUCCEEDED(hr) && outParams) {
        VARIANT vRet;
        VariantInit(&vRet);
        if (SUCCEEDED(outParams->Get(L"ReturnValue", 0, &vRet, nullptr, nullptr))) {
            if (vRet.vt == VT_I4)
                ok = (vRet.lVal == 0);
            else if (vRet.vt == VT_UI4)
                ok = (vRet.ulVal == 0);
            appendHandoffLog(exeDir, QStringLiteral("wmi: ReturnValue=%1 hr=0x%2")
                                           .arg(vRet.vt == VT_I4 ? vRet.lVal : int(vRet.ulVal))
                                           .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
            VariantClear(&vRet);
        }
        outParams->Release();
    } else {
        appendHandoffLog(exeDir, QStringLiteral("wmi: ExecMethod hr=0x%1")
                                       .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
    }

    if (shouldUninit)
        CoUninitialize();
    return ok;
}

bool launchHandoffViaTaskScheduler(const QString &scriptPath)
{
    // TR #15: Task Scheduler service starts the action at the interactive user's
    // Medium IL. Works when in-proc / Explorer ShellExecute stays Low.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninit = false;
    if (hr == S_OK || hr == S_FALSE) {
        shouldUninit = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    ITaskService *service = nullptr;
    hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITaskService, reinterpret_cast<void **>(&service));
    if (FAILED(hr) || !service) {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    VARIANT vEmpty;
    VariantInit(&vEmpty);
    hr = service->Connect(vEmpty, vEmpty, vEmpty, vEmpty);
    if (FAILED(hr)) {
        service->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    BSTR rootPath = SysAllocString(L"\\");
    ITaskFolder *folder = nullptr;
    hr = service->GetFolder(rootPath, &folder);
    SysFreeString(rootPath);
    if (FAILED(hr) || !folder) {
        service->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    const wchar_t kTaskName[] = L"StickyTodoHandoff";
    BSTR taskName = SysAllocString(kTaskName);
    folder->DeleteTask(taskName, 0); // ignore missing

    ITaskDefinition *task = nullptr;
    hr = service->NewTask(0, &task);
    if (FAILED(hr) || !task) {
        SysFreeString(taskName);
        folder->Release();
        service->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IPrincipal *principal = nullptr;
    if (SUCCEEDED(task->get_Principal(&principal)) && principal) {
        principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        principal->put_RunLevel(TASK_RUNLEVEL_LUA);
        principal->Release();
    }

    ITaskSettings *settings = nullptr;
    if (SUCCEEDED(task->get_Settings(&settings)) && settings) {
        settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        settings->put_AllowDemandStart(VARIANT_TRUE);
        settings->put_StartWhenAvailable(VARIANT_TRUE);
        settings->Release();
    }

    IActionCollection *actions = nullptr;
    hr = task->get_Actions(&actions);
    if (FAILED(hr) || !actions) {
        task->Release();
        SysFreeString(taskName);
        folder->Release();
        service->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IAction *action = nullptr;
    hr = actions->Create(TASK_ACTION_EXEC, &action);
    actions->Release();
    if (FAILED(hr) || !action) {
        task->Release();
        SysFreeString(taskName);
        folder->Release();
        service->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IExecAction *execAction = nullptr;
    hr = action->QueryInterface(IID_IExecAction, reinterpret_cast<void **>(&execAction));
    action->Release();
    if (FAILED(hr) || !execAction) {
        task->Release();
        SysFreeString(taskName);
        folder->Release();
        service->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    BSTR cmdPath = SysAllocString(L"cmd.exe");
    const std::wstring args =
        L"/c \"" + QDir::toNativeSeparators(scriptPath).toStdWString() + L"\"";
    BSTR cmdArgs = SysAllocString(args.c_str());
    execAction->put_Path(cmdPath);
    execAction->put_Arguments(cmdArgs);
    SysFreeString(cmdPath);
    SysFreeString(cmdArgs);
    execAction->Release();

    VARIANT vUser;
    VARIANT vPassword;
    VARIANT vSddl;
    VariantInit(&vUser);
    VariantInit(&vPassword);
    VariantInit(&vSddl);
    IRegisteredTask *registered = nullptr;
    hr = folder->RegisterTaskDefinition(taskName, task, TASK_CREATE_OR_UPDATE, vUser,
                                        vPassword, TASK_LOGON_INTERACTIVE_TOKEN, vSddl,
                                        &registered);
    task->Release();
    if (FAILED(hr) || !registered) {
        SysFreeString(taskName);
        folder->Release();
        service->Release();
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    VARIANT vParams;
    VariantInit(&vParams);
    IRunningTask *running = nullptr;
    hr = registered->Run(vParams, &running);
    if (running)
        running->Release();
    registered->Release();
    // Keep the task definition for overwrite next time; do not DeleteTask after
    // Run — that can tear down the just-started action on some builds.
    SysFreeString(taskName);
    folder->Release();
    service->Release();
    if (shouldUninit)
        CoUninitialize();
    return SUCCEEDED(hr);
}

bool launchHandoffViaShellApplication(const QString &scriptPath, const QString &workingDir)
{
    // TR #15: CoCreateInstance(CLSID_Shell) from a Low process stays Low (in-proc
    // or same-IL). Borrow IShellDispatch2 from the desktop explorer (Medium) via
    // ShellWindows, then ShellExecute cmd /c handoff.cmd so the child is Medium.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninit = false;
    if (hr == S_OK || hr == S_FALSE) {
        shouldUninit = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        // Already initialized on another apartment model; proceed without uninit.
    } else {
        return false;
    }

    IShellWindows *windows = nullptr;
    hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER,
                          IID_IShellWindows, reinterpret_cast<void **>(&windows));
    if (FAILED(hr) || !windows) {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    VARIANT vEmpty;
    VariantInit(&vEmpty);
    long desktopHwnd = 0;
    IDispatch *desktopDisp = nullptr;
    hr = windows->FindWindowSW(&vEmpty, &vEmpty, SWC_DESKTOP, &desktopHwnd,
                               SWFO_NEEDDISPATCH, &desktopDisp);
    windows->Release();
    if (hr != S_OK || !desktopDisp) {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IServiceProvider *provider = nullptr;
    hr = desktopDisp->QueryInterface(IID_IServiceProvider,
                                     reinterpret_cast<void **>(&provider));
    desktopDisp->Release();
    if (FAILED(hr) || !provider) {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IShellBrowser *browser = nullptr;
    hr = provider->QueryService(SID_STopLevelBrowser, IID_IShellBrowser,
                                reinterpret_cast<void **>(&browser));
    provider->Release();
    if (FAILED(hr) || !browser) {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IShellView *view = nullptr;
    hr = browser->QueryActiveShellView(&view);
    browser->Release();
    if (FAILED(hr) || !view) {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IDispatch *background = nullptr;
    hr = view->GetItemObject(SVGIO_BACKGROUND, IID_IDispatch,
                             reinterpret_cast<void **>(&background));
    view->Release();
    if (FAILED(hr) || !background) {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IShellFolderViewDual *folderView = nullptr;
    hr = background->QueryInterface(IID_IShellFolderViewDual,
                                    reinterpret_cast<void **>(&folderView));
    background->Release();
    if (FAILED(hr) || !folderView) {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IDispatch *appDisp = nullptr;
    hr = folderView->get_Application(&appDisp);
    folderView->Release();
    if (FAILED(hr) || !appDisp) {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IShellDispatch2 *shell = nullptr;
    hr = appDisp->QueryInterface(IID_IShellDispatch2,
                                 reinterpret_cast<void **>(&shell));
    appDisp->Release();
    if (FAILED(hr) || !shell) {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    const std::wstring scriptNative =
        QDir::toNativeSeparators(scriptPath).toStdWString();
    const std::wstring dirNative =
        QDir::toNativeSeparators(workingDir).toStdWString();
    const std::wstring args = L"/c \"" + scriptNative + L"\"";

    BSTR bstrFile = SysAllocString(L"cmd.exe");
    VARIANT vArgs;
    VARIANT vDir;
    VARIANT vOp;
    VARIANT vShow;
    VariantInit(&vArgs);
    VariantInit(&vDir);
    VariantInit(&vOp);
    VariantInit(&vShow);

    vArgs.vt = VT_BSTR;
    vArgs.bstrVal = SysAllocString(args.c_str());
    vDir.vt = VT_BSTR;
    vDir.bstrVal = SysAllocString(dirNative.c_str());
    vOp.vt = VT_BSTR;
    vOp.bstrVal = SysAllocString(L"open");
    vShow.vt = VT_I4;
    vShow.lVal = SW_HIDE;

    bool ok = false;
    if (bstrFile && vArgs.bstrVal && vDir.bstrVal && vOp.bstrVal) {
        hr = shell->ShellExecute(bstrFile, vArgs, vDir, vOp, vShow);
        ok = SUCCEEDED(hr);
    }

    VariantClear(&vArgs);
    VariantClear(&vDir);
    VariantClear(&vOp);
    VariantClear(&vShow);
    if (bstrFile)
        SysFreeString(bstrFile);
    shell->Release();
    if (shouldUninit)
        CoUninitialize();
    return ok;
}

LSTATUS queryAutoStart(QString *command)
{
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key);
    if (status != ERROR_SUCCESS)
        return status;

    DWORD type = 0;
    DWORD bytes = 0;
    status = RegQueryValueExW(key, kRunValue, nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS) {
        RegCloseKey(key);
        return status;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        RegCloseKey(key);
        return ERROR_INVALID_DATA;
    }

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
    DWORD read = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    status = RegQueryValueExW(key, kRunValue, nullptr, &type,
                              reinterpret_cast<BYTE *>(buffer.data()), &read);
    RegCloseKey(key);
    if (status == ERROR_SUCCESS && command)
        *command = QString::fromWCharArray(buffer.data());
    return status;
}

} // namespace

namespace WinUtils {

bool acquireSingleInstance()
{
    if (g_instanceMutex)
        return true;
    g_instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\StickyTodo.SingleInstance.v1");
    if (!g_instanceMutex) {
        // A low-integrity copy cannot open the mutex owned by the normal
        // process. Treating that as "I am the only instance" opened a second note.
        return GetLastError() != ERROR_ACCESS_DENIED;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
        return false;
    }
    return true;
}

bool notifyExistingInstance()
{
    const UINT message = activationMessage();
    if (!message)
        return false;
    return PostMessageW(HWND_BROADCAST, message, 0, 0) != FALSE;
}

unsigned int activationMessage()
{
    static const UINT message = RegisterWindowMessageW(L"StickyTodo.Activate.v1");
    return message;
}

bool setAutoStart(bool enabled, QString *errorMessage, const QString &dataDir)
{
    if (errorMessage)
        errorMessage->clear();

    if (!enabled) {
        QString existing;
        const LSTATUS query = queryAutoStart(&existing);
        if (query == ERROR_FILE_NOT_FOUND)
            return true;
        if (query != ERROR_SUCCESS) {
            if (errorMessage)
                *errorMessage = windowsErrorText(static_cast<unsigned long>(query));
            return false;
        }

        HKEY key = nullptr;
        LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
        if (status == ERROR_FILE_NOT_FOUND)
            return true;
        if (status != ERROR_SUCCESS) {
            if (errorMessage)
                *errorMessage = windowsErrorText(static_cast<unsigned long>(status));
            return false;
        }
        status = RegDeleteValueW(key, kRunValue);
        RegCloseKey(key);
        if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
            return true;
        if (errorMessage)
            *errorMessage = windowsErrorText(static_cast<unsigned long>(status));
        return false;
    }

    // Never bake a Temp/smoke --data-dir into HKCU Run: that is how a one-off
    // test launch made subsequent boots read/write an empty (or deleted) folder
    // while the real todos stayed in the portable config next to the package.
    if (isEphemeralDataDir(dataDir.isEmpty()
                               ? qEnvironmentVariable("STICKY_TODO_DATA_DIR")
                               : dataDir)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "数据目录位于临时文件夹，未写入开机启动项，以免启动后读写空配置");
        }
        return false;
    }

    const QString desired = startupCommand(dataDir);
    QString existing;
    if (queryAutoStart(&existing) == ERROR_SUCCESS && existing == desired)
        return true;

    HKEY key = nullptr;
    DWORD disposition = 0;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                     KEY_SET_VALUE, nullptr, &key, &disposition);
    if (status != ERROR_SUCCESS) {
        if (errorMessage)
            *errorMessage = windowsErrorText(static_cast<unsigned long>(status));
        return false;
    }

    const std::wstring data = desired.toStdWString();
    status = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                            reinterpret_cast<const BYTE *>(data.c_str()),
                            static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        if (errorMessage)
            *errorMessage = windowsErrorText(static_cast<unsigned long>(status));
        return false;
    }
    return true;
}

bool autoStartEnabled(QString *command)
{
    QString value;
    const LSTATUS status = queryAutoStart(&value);
    if (command)
        *command = value;
    return status == ERROR_SUCCESS;
}

void setTopMost(QWidget *widget, bool on)
{
    if (!widget)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd)
        return;
    SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    // SetWindowPos can bring the system border back.
    suppressSystemFrame(widget);
}

void activateWindow(QWidget *widget)
{
    if (!widget)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd)
        return;
    if (!IsWindowVisible(hwnd))
        ShowWindow(hwnd, SW_SHOW);
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
}


bool isOsForeground(QWidget *widget)
{
    if (!widget)
        return false;
    // Ensure native handle exists before querying.
    widget->winId();
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd)
        return false;
    const HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    // Compare top-level roots: Qt winId is usually already top-level for MainWindow,
    // but GA_ROOT covers embedded / reparented edge cases without relying on isActiveWindow.
    HWND ours = GetAncestor(hwnd, GA_ROOT);
    if (!ours)
        ours = hwnd;
    HWND fgRoot = GetAncestor(fg, GA_ROOT);
    if (!fgRoot)
        fgRoot = fg;
    return fg == hwnd || fg == ours || fgRoot == ours;
}

void setAppUserModelId()
{
    SetCurrentProcessExplicitAppUserModelID(L"StickyTodo.Desktop.1");
}

void prepareRoundedPopup(QWidget *popup)
{
    if (!popup)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(popup->winId());
    if (!hwnd)
        return;
    // Windows 11 draws its own square frame and a second set of rounded
    // corners around a styled QMenu. Turn that frame off so only the menu's
    // own rounded border remains.
    const DWORD disabled = 1; // DWMNCRP_DISABLED
    DwmSetWindowAttribute(hwnd, 2, &disabled, sizeof(disabled)); // DWMWA_NCRENDERING_POLICY
    const DWORD doNotRound = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(hwnd, 33, &doNotRound, sizeof(doNotRound)); // DWMWA_WINDOW_CORNER_PREFERENCE
    const COLORREF noBorder = 0xFFFFFFFE; // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(hwnd, 34, &noBorder, sizeof(noBorder)); // DWMWA_BORDER_COLOR
}

void suppressSystemFrame(QWidget *window)
{
    if (!window)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
        return;

    // The paper card is inset so the shadow can sit in the margin. Windows 11
    // otherwise fills that margin with its own rounded plate (and a border),
    // which reads as a second window stacked behind the note.
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    const DWORD backdrop = 1; // DWMSBT_NONE
    DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));
    const DWORD doNotRound = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(hwnd, 33, &doNotRound, sizeof(doNotRound));
    const COLORREF none = 0xFFFFFFFE; // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(hwnd, 34, &none, sizeof(none));
    DwmSetWindowAttribute(hwnd, 35, &none, sizeof(none));
}

bool directoryLooksWritable(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return false;
    if (!QDir().mkpath(dir))
        return false;
    const QString probe = QDir(dir).filePath(QStringLiteral(".sticky-todo-write-probe"));
    return probeWritableFile(probe);
}

bool tryClearDirectoryReadOnly(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return false;
    bool touched = false;
    const std::wstring wdir = QDir::toNativeSeparators(dir).toStdWString();
    const DWORD attrs = GetFileAttributesW(wdir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
        if (SetFileAttributesW(wdir.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY))
            touched = true;
    }
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        const std::wstring wfile = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
        const DWORD a = GetFileAttributesW(wfile.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) {
            if (SetFileAttributesW(wfile.c_str(), a & ~FILE_ATTRIBUTE_READONLY))
                touched = true;
        }
    }
    return touched || attrs != INVALID_FILE_ATTRIBUTES;
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
    const QString localApp = qEnvironmentVariable("LOCALAPPDATA").trimmed();
    if (!localApp.isEmpty())
        return QDir(localApp).filePath(QStringLiteral("StickyTodo/data"));
    return {};
}

int migrateDataFiles(const QString &srcDir, const QString &dstDir)
{
    if (srcDir.isEmpty() || dstDir.isEmpty())
        return 0;
    if (QDir::cleanPath(srcDir).compare(QDir::cleanPath(dstDir), Qt::CaseInsensitive) == 0)
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
void applyCommandLineDataDir()
{
    applyDataDirFromCommandLine();
}


bool handoffIfLowIntegrity()
{
    if (!isProcessLowIntegrity())
        return false;

    const QString exeDir = moduleDirectory();
    if (exeDir.isEmpty())
        return false;

    const QString localApp = qEnvironmentVariable("LOCALAPPDATA").trimmed();
    if (localApp.isEmpty())
        return false;

    const QString runtimeDir = QDir(localApp).filePath(QStringLiteral("StickyTodo/runtime"));
    if (QDir::cleanPath(exeDir).compare(QDir::cleanPath(runtimeDir), Qt::CaseInsensitive) == 0)
        return false; // already the medium runtime — do not loop

    QString dataDir = qEnvironmentVariable("STICKY_TODO_DATA_DIR").trimmed();
    if (dataDir.isEmpty())
        dataDir = QDir(exeDir).filePath(QStringLiteral("config"));

    // TR #16: if package config is RO / unwritable, handoff must not pin
    // --data-dir there — use LocalAppData\StickyTodo\data and migrate JSON.
    if (!ensureDirectoryWritable(dataDir)) {
        const QString fallback = stickyTodoLocalDataDir();
        if (ensureDirectoryWritable(fallback)) {
            migrateDataFiles(dataDir, fallback);
            dataDir = fallback;
        }
    }

    // Prefer exe-sibling, then %LOCALAPPDATA%\Temp, then GetTempPath — Low
    // cannot write medium TEMP, so each candidate is probed with a real write.
    const QString scriptPath = chooseWritableHandoffScriptPath(exeDir, localApp);
    if (scriptPath.isEmpty())
        return false;
    if (!writeHandoffScript(scriptPath, exeDir, dataDir))
        return false;

    // TR #15: Low→Medium brokers (MIC may block some). Order: WMI, Task Scheduler,
    // Explorer Shell. Log failures beside the exe for 小测.
    appendHandoffLog(exeDir, QStringLiteral("handoff: begin script=%1")
                                   .arg(QDir::toNativeSeparators(scriptPath)));
    if (launchHandoffViaWmi(scriptPath, exeDir, exeDir)) {
        appendHandoffLog(exeDir, QStringLiteral("handoff: WMI ok"));
        return true;
    }
    if (launchHandoffViaTaskScheduler(scriptPath)) {
        appendHandoffLog(exeDir, QStringLiteral("handoff: TaskScheduler ok"));
        return true;
    }
    appendHandoffLog(exeDir, QStringLiteral("handoff: TaskScheduler failed"));
    if (launchHandoffViaShellApplication(scriptPath, exeDir)) {
        appendHandoffLog(exeDir, QStringLiteral("handoff: ExplorerShell ok"));
        return true;
    }
    appendHandoffLog(exeDir, QStringLiteral("handoff: all brokers failed"));
    return false;
}


bool trayAvailable()
{
    return FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
}

unsigned int trayCallbackMessage()
{
    return kTrayCallback;
}

bool installTrayIcon(QWidget *owner, const QImage &image, const QString &tip)
{
    g_trayError.clear();
    if (!owner)
        return false;
    g_forwardHwnd = reinterpret_cast<HWND>(owner->winId());
    if (!g_forwardHwnd) {
        g_trayError = QStringLiteral("窗口句柄尚未创建");
        return false;
    }

    const HWND hwnd = ensureTrayWindow();
    if (!hwnd) {
        g_trayError = QStringLiteral("无法创建托盘窗口");
        return false;
    }

    if (g_trayAdded && g_trayIcon) {
        g_trayTip = tip;
        NOTIFYICONDATAW data;
        fillTrayData(&data, hwnd, g_trayIcon, tip, NIF_TIP | NIF_SHOWTIP);
        Shell_NotifyIconW(NIM_MODIFY, &data);
        return true;
    }

    const int px = trayPixelSize(hwnd);
    HICON next = iconFromModule(px);
    if (!next)
        next = iconForTray(hwnd, image);
    if (!next) {
        g_trayError = QStringLiteral("无法创建托盘图标");
        SetWindowTextW(hwnd, L"StickyTodoTray FAIL icon");
        return false;
    }

    g_trayTip = tip;
    const bool guidAttempts[] = {true, false};
    bool added = false;
    for (bool useGuid : guidAttempts) {
        g_trayUseGuid = useGuid;
        NOTIFYICONDATAW stale;
        fillTrayData(&stale, hwnd, nullptr, QString(), 0);
        Shell_NotifyIconW(NIM_DELETE, &stale);

        NOTIFYICONDATAW data;
        fillTrayData(&data, hwnd, next, tip, NIF_MESSAGE | NIF_ICON | NIF_TIP);
        if (!notifyAddOrModify(&data))
            continue;
        applyTrayVersion(&data);
        added = true;
        break;
    }
    if (!added) {
        DestroyIcon(next);
        const QString title = QStringLiteral("StickyTodoTray FAIL %1").arg(g_trayError);
        SetWindowTextW(hwnd, reinterpret_cast<LPCWSTR>(title.utf16()));
        return false;
    }

    if (g_trayIcon)
        DestroyIcon(g_trayIcon);
    g_trayIcon = next;
    g_trayAdded = true;
    SetWindowTextW(hwnd, L"StickyTodoTray OK");
    return true;
}

void updateTrayTip(QWidget *owner, const QString &tip)
{
    if (!g_trayIcon || !g_trayHwnd)
        return;
    if (owner)
        g_forwardHwnd = reinterpret_cast<HWND>(owner->winId());
    g_trayTip = tip;
    NOTIFYICONDATAW data;
    fillTrayData(&data, g_trayHwnd, g_trayIcon, tip, NIF_TIP | NIF_SHOWTIP);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void showTrayBalloon(QWidget *owner, const QString &title, const QString &message)
{
    if (!g_trayIcon || !g_trayHwnd)
        return;
    if (owner)
        g_forwardHwnd = reinterpret_cast<HWND>(owner->winId());
    NOTIFYICONDATAW data;
    fillTrayData(&data, g_trayHwnd, g_trayIcon, QString(), NIF_INFO);
    copyLimited(data.szInfoTitle, 64, title);
    copyLimited(data.szInfo, 256, message);
    data.dwInfoFlags = NIIF_INFO;
    data.uTimeout = 4500;
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void removeTrayIcon(QWidget *owner)
{
    Q_UNUSED(owner);
    if (g_trayHwnd) {
        NOTIFYICONDATAW data;
        fillTrayData(&data, g_trayHwnd, nullptr, QString(), 0);
        Shell_NotifyIconW(NIM_DELETE, &data);
    }
    g_trayAdded = false;
    if (g_trayIcon) {
        DestroyIcon(g_trayIcon);
        g_trayIcon = nullptr;
    }
    if (g_trayHwnd) {
        DestroyWindow(g_trayHwnd);
        g_trayHwnd = nullptr;
    }
    g_forwardHwnd = nullptr;
}

bool promoteTrayIcon()
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings", 0,
                      KEY_READ, &root) != ERROR_SUCCESS) {
        return false;
    }

    bool found = false;
    DWORD index = 0;
    wchar_t name[256];
    while (true) {
        DWORD nameLen = 256;
        if (RegEnumKeyExW(root, index++, name, &nameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        HKEY child = nullptr;
        if (RegOpenKeyExW(root, name, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &child) != ERROR_SUCCESS)
            continue;
        DWORD type = 0;
        DWORD bytes = 0;
        const LSTATUS query = RegQueryValueExW(child, L"ExecutablePath", nullptr, &type, nullptr, &bytes);
        if (query == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && bytes > sizeof(wchar_t)
            && bytes < 4096) {
            std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
            DWORD read = bytes;
            if (RegQueryValueExW(child, L"ExecutablePath", nullptr, &type,
                                 reinterpret_cast<BYTE *>(buffer.data()), &read) == ERROR_SUCCESS) {
                const QString path = QDir::toNativeSeparators(QString::fromWCharArray(buffer.data()));
                if (QFileInfo(path).fileName().compare(QStringLiteral("StickyTodo.exe"), Qt::CaseInsensitive) == 0
                    || path.compare(exe, Qt::CaseInsensitive) == 0) {
                    DWORD one = 1;
                    if (RegSetValueExW(child, L"IsPromoted", 0, REG_DWORD,
                                       reinterpret_cast<const BYTE *>(&one), sizeof(one)) == ERROR_SUCCESS)
                        found = true;
                }
            }
        }
        RegCloseKey(child);
    }
    RegCloseKey(root);
    return found;
}

QString lastTrayError()
{
    return g_trayError;
}

void clearDownloadedFileMarks()
{
    const auto clearDir = [](const QString &dirPath) {
        const QDir dir(dirPath);
        if (!dir.exists())
            return;
        const QStringList files = dir.entryList(QDir::Files);
        for (const QString &name : files) {
            const QString stream = dir.filePath(name) + QStringLiteral(":Zone.Identifier");
            DeleteFileW(reinterpret_cast<LPCWSTR>(stream.utf16()));
        }
    };
    const QString base = QCoreApplication::applicationDirPath();
    clearDir(base);
    clearDir(QDir(base).filePath(QStringLiteral("platforms")));
}

QString windowsErrorText(unsigned long code)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text;
    if (length && buffer) {
        text = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
        LocalFree(buffer);
    }
    if (text.isEmpty())
        text = QStringLiteral("未知错误");
    return QStringLiteral("%1（错误 %2）").arg(text).arg(code);
}

bool globalHotkeySupported(QString *reason)
{
    Q_UNUSED(reason);
    return true;
}

namespace {

constexpr int kHotkeyId = 1;
HWND g_hotkeyHwnd = nullptr;
QString g_hotkeyText;
UINT g_hotkeyMods = 0;
UINT g_hotkeyVk = 0;

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

bool mapHotkeyToken(const QString &token, UINT *mods, UINT *vk, QString *canonPart)
{
    const QString lower = token.toLower();
    if (lower == QLatin1String("ctrl") || lower == QLatin1String("control")) {
        *mods |= MOD_CONTROL;
        if (canonPart)
            *canonPart = QStringLiteral("Ctrl");
        return true;
    }
    if (lower == QLatin1String("alt") || lower == QLatin1String("option")) {
        *mods |= MOD_ALT;
        if (canonPart)
            *canonPart = QStringLiteral("Alt");
        return true;
    }
    if (lower == QLatin1String("shift")) {
        *mods |= MOD_SHIFT;
        if (canonPart)
            *canonPart = QStringLiteral("Shift");
        return true;
    }
    if (lower == QLatin1String("win") || lower == QLatin1String("meta")
        || lower == QLatin1String("super")) {
        *mods |= MOD_WIN;
        if (canonPart)
            *canonPart = QStringLiteral("Win");
        return true;
    }
    if (token.size() == 1) {
        const QChar ch = token.at(0).toUpper();
        if ((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
            || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))) {
            *vk = static_cast<UINT>(ch.unicode());
            if (canonPart)
                *canonPart = QString(ch);
            return true;
        }
    }
    if (lower.size() >= 2 && lower.startsWith(QLatin1Char('f'))) {
        bool ok = false;
        const int n = lower.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 12) {
            *vk = static_cast<UINT>(VK_F1 + (n - 1));
            if (canonPart)
                *canonPart = QStringLiteral("F%1").arg(n);
            return true;
        }
    }
    return false;
}

bool parseHotkey(const QString &text, UINT *mods, UINT *vk, QString *normalized)
{
    *mods = 0;
    *vk = 0;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (normalized)
            normalized->clear();
        return true;
    }
    const QStringList parts = splitHotkeyTokens(trimmed);
    if (parts.isEmpty())
        return false;
    QStringList canonMods;
    QString canonKey;
    for (const QString &part : parts) {
        QString canon;
        UINT beforeVk = *vk;
        if (!mapHotkeyToken(part, mods, vk, &canon))
            return false;
        if (*vk != beforeVk && *vk != 0) {
            if (!canonKey.isEmpty())
                return false; // more than one key
            canonKey = canon;
        } else {
            if (!canonMods.contains(canon))
                canonMods.append(canon);
        }
    }
    if (canonKey.isEmpty() || *mods == 0)
        return false; // require at least one modifier + one key
    // Stable order: Ctrl, Alt, Shift, Win, Key
    QStringList ordered;
    if (*mods & MOD_CONTROL)
        ordered.append(QStringLiteral("Ctrl"));
    if (*mods & MOD_ALT)
        ordered.append(QStringLiteral("Alt"));
    if (*mods & MOD_SHIFT)
        ordered.append(QStringLiteral("Shift"));
    if (*mods & MOD_WIN)
        ordered.append(QStringLiteral("Win"));
    ordered.append(canonKey);
    if (normalized)
        *normalized = ordered.join(QLatin1Char('+'));
    return true;
}

HWND hwndOf(QWidget *owner)
{
    if (!owner)
        return nullptr;
    return reinterpret_cast<HWND>(owner->winId());
}

} // namespace

QString normalizeGlobalHotkey(const QString &text)
{
    UINT mods = 0;
    UINT vk = 0;
    QString normalized;
    if (!parseHotkey(text, &mods, &vk, &normalized))
        return QString();
    return normalized;
}

bool setGlobalHotkey(QWidget *owner, const QString &normalized, QString *error)
{
    if (error)
        error->clear();
    clearGlobalHotkey();
    const QString text = normalized.trimmed();
    if (text.isEmpty())
        return true;
    UINT mods = 0;
    UINT vk = 0;
    QString canon;
    if (!parseHotkey(text, &mods, &vk, &canon) || canon.isEmpty()) {
        if (error)
            *error = QStringLiteral("快捷键格式无效，请使用如 Ctrl+Alt+T");
        return false;
    }
    if (!owner) {
        if (error)
            *error = QStringLiteral("内部错误：无窗口句柄");
        return false;
    }
    owner->winId(); // ensure native handle
    HWND hwnd = hwndOf(owner);
    if (!hwnd) {
        if (error)
            *error = QStringLiteral("窗口尚未创建，无法注册热键");
        return false;
    }
    // MOD_NOREPEAT avoids key-repeat spam while held.
    const UINT flags = mods | MOD_NOREPEAT;
    if (!RegisterHotKey(hwnd, kHotkeyId, flags, vk)) {
        if (error)
            *error = QStringLiteral("注册失败：%1").arg(windowsErrorText(GetLastError()));
        return false;
    }
    g_hotkeyHwnd = hwnd;
    g_hotkeyText = canon;
    g_hotkeyMods = mods;
    g_hotkeyVk = vk;
    return true;
}

void clearGlobalHotkey()
{
    if (g_hotkeyHwnd) {
        UnregisterHotKey(g_hotkeyHwnd, kHotkeyId);
        g_hotkeyHwnd = nullptr;
    }
    g_hotkeyText.clear();
    g_hotkeyMods = 0;
    g_hotkeyVk = 0;
}

bool isGlobalHotkeyEvent(const QByteArray &eventType, void *message)
{
    if (!g_hotkeyHwnd || !message)
        return false;
    if (eventType != "windows_generic_MSG")
        return false;
    auto *msg = static_cast<MSG *>(message);
    return msg->message == WM_HOTKEY && msg->wParam == static_cast<WPARAM>(kHotkeyId);
}


} // namespace WinUtils
