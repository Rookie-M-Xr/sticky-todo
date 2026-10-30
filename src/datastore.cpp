#include "datastore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <utility>
#include <functional>

namespace {

constexpr int kSchemaVersion = 1;

QDateTime parseTimestamp(const QString &text)
{
    if (text.trimmed().isEmpty())
        return QDateTime();
    QDateTime value = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!value.isValid())
        value = QDateTime::fromString(text, Qt::ISODate);
    return value;
}

QString nowText()
{
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}

} // namespace

DataStore::DataStore(QObject *parent)
    : QObject(parent)
{
    m_todoSaveTimer.setSingleShot(true);
    m_settingsSaveTimer.setSingleShot(true);
    connect(&m_todoSaveTimer, &QTimer::timeout, this, &DataStore::saveTodosNow);
    connect(&m_settingsSaveTimer, &QTimer::timeout, this, &DataStore::saveSettingsNow);
}

QString DataStore::resolveDataDir() const
{
    const QString overrideDir = qEnvironmentVariable("STICKY_TODO_DATA_DIR").trimmed();
    if (!overrideDir.isEmpty())
        return QDir::cleanPath(overrideDir);

    // 绿色便携版：数据跟随 exe 放置，绝不固定写入 C 盘用户目录。
    // config 目录和 exe 同级，可随整个程序目录一起复制/备份/删除。
    QString base = QCoreApplication::applicationDirPath();
    if (base.isEmpty())
        base = QDir::currentPath();
    return QDir(base).filePath(QStringLiteral("config"));
}

QString DataStore::todosPath() const
{
    return QDir(m_dataDir).filePath(QStringLiteral("todos.json"));
}

QString DataStore::settingsPath() const
{
    return QDir(m_dataDir).filePath(QStringLiteral("settings.json"));
}

QString DataStore::archivePath() const
{
    return QDir(m_dataDir).filePath(QStringLiteral("todos-archive.json"));
}

bool DataStore::load()
{
    m_dataDir = resolveDataDir();
    if (!QDir().mkpath(m_dataDir)) {
        const QString message = QStringLiteral("无法创建数据目录：%1").arg(QDir::toNativeSeparators(m_dataDir));
        addNotice(message);
        emit errorOccurred(message);
        return false;
    }

    m_firstRun = !QFileInfo::exists(settingsPath());
    m_loaded = true;
    loadTodos();
    loadSettings();
    // R0: auto-purge completed items older than retentionDays once per load.
    applyRetentionOnLoad();
    return true;
}

bool DataStore::flush()
{
    m_todoSaveTimer.stop();
    m_settingsSaveTimer.stop();
    bool ok = true;
    if (m_todosDirty) {
        saveTodosNow();
        ok = ok && !m_todosDirty;
    }
    if (m_settingsDirty) {
        saveSettingsNow();
        ok = ok && !m_settingsDirty;
    }
    return ok;
}

QStringList DataStore::takeNotices()
{
    const QStringList notices = m_notices;
    m_notices.clear();
    return notices;
}

const TodoItem *DataStore::findTodo(const QString &id) const
{
    for (const TodoItem &item : m_todos) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

QString DataStore::addTodo(const QString &text, QDate dueDate)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return QString();
    const QDateTime now = QDateTime::currentDateTime();
    TodoItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.text = trimmed.left(4000);
    item.createdAt = now;
    item.updatedAt = now;
    item.dueDate = dueDate.isValid() ? dueDate : QDate();
    m_todos.append(item);
    scheduleTodosSave(0);
    emit todosChanged();
    return item.id;
}

bool DataStore::updateText(const QString &id, const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;
    for (TodoItem &item : m_todos) {
        if (item.id != id)
            continue;
        const QString next = trimmed.left(4000);
        if (item.text == next)
            return true;
        item.text = next;
        item.updatedAt = QDateTime::currentDateTime();
        scheduleTodosSave(120);
        emit todosChanged();
        return true;
    }
    return false;
}

bool DataStore::updateDue(const QString &id, QDate dueDate)
{
    for (TodoItem &item : m_todos) {
        if (item.id != id)
            continue;
        const QDate next = dueDate.isValid() ? dueDate : QDate();
        if (item.dueDate == next)
            return true;
        item.dueDate = next;
        item.updatedAt = QDateTime::currentDateTime();
        scheduleTodosSave(0);
        emit todosChanged();
        return true;
    }
    return false;
}

bool DataStore::setCompleted(const QString &id, bool completed)
{
    return setCompleted(id, completed, nullptr);
}

bool DataStore::setCompleted(const QString &id, bool completed, QDateTime *completedAt)
{
    for (TodoItem &item : m_todos) {
        if (item.id != id)
            continue;
        if (item.completed == completed) {
            if (completedAt)
                *completedAt = item.completedAt;
            return true;
        }
        const QDateTime now = QDateTime::currentDateTime();
        item.completed = completed;
        item.completedAt = completed ? now : QDateTime();
        item.updatedAt = now;
        if (completedAt)
            *completedAt = item.completedAt;
        scheduleTodosSave(0);
        emit todosChanged();
        return true;
    }
    return false;
}

bool DataStore::removeTodo(const QString &id, TodoItem *removed, int *removedIndex)
{
    for (int i = 0; i < m_todos.size(); ++i) {
        if (m_todos.at(i).id != id)
            continue;
        if (removed)
            *removed = m_todos.at(i);
        if (removedIndex)
            *removedIndex = i;
        m_todos.removeAt(i);
        scheduleTodosSave(0);
        emit todosChanged();
        return true;
    }
    return false;
}

bool DataStore::restoreTodo(const TodoItem &item, int index)
{
    if (item.id.isEmpty() || item.text.trimmed().isEmpty())
        return false;
    for (const TodoItem &existing : m_todos) {
        if (existing.id == item.id)
            return false;
    }
    if (index >= 0 && index <= m_todos.size())
        m_todos.insert(index, item);
    else
        m_todos.append(item);
    scheduleTodosSave(0);
    emit todosChanged();
    return true;
}

void DataStore::setRetentionDays(int days)
{
    const int next = qBound(1, days, 3650);
    if (m_settings.retentionDays == next)
        return;
    m_settings.retentionDays = next;
    scheduleSettingsSave(0);
    emit settingsChanged();
}

bool DataStore::isCompletedOlderThan(const TodoItem &item, int days, const QDateTime &now)
{
    if (!item.completed || days < 0)
        return false;
    QDateTime stamp = item.completedAt;
    if (!stamp.isValid())
        stamp = item.updatedAt;
    if (!stamp.isValid())
        return false;
    return stamp.addDays(days) <= now;
}

int DataStore::removeCompletedMatching(const std::function<bool(const TodoItem &)> &pred)
{
    int removed = 0;
    for (int i = m_todos.size() - 1; i >= 0; --i) {
        const TodoItem &item = m_todos.at(i);
        if (!item.completed)
            continue;
        if (!pred(item))
            continue;
        m_todos.removeAt(i);
        ++removed;
    }
    if (removed > 0) {
        scheduleTodosSave(0);
        emit todosChanged();
    }
    return removed;
}

int DataStore::purgeCompletedOlderThan(int days)
{
    if (days < 0)
        return 0;
    const QDateTime now = QDateTime::currentDateTime();
    return removeCompletedMatching([&](const TodoItem &item) {
        return isCompletedOlderThan(item, days, now);
    });
}

int DataStore::purgeAllCompleted()
{
    return removeCompletedMatching([](const TodoItem &) { return true; });
}

int DataStore::archiveThenRemoveMatching(const std::function<bool(const TodoItem &)> &pred)
{
    QVector<TodoItem> matched;
    matched.reserve(m_todos.size());
    for (const TodoItem &item : std::as_const(m_todos)) {
        if (item.completed && pred(item))
            matched.append(item);
    }
    if (matched.isEmpty())
        return 0;

    QJsonArray archived;
    const QString path = archivePath();
    if (QFileInfo::exists(path)) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonValue arr = doc.object().value(QStringLiteral("archived"));
                if (arr.isArray())
                    archived = arr.toArray();
            }
        }
    }

    const QString stamped = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    for (const TodoItem &item : std::as_const(matched)) {
        QJsonObject entry;
        entry.insert(QStringLiteral("archivedAt"), stamped);
        entry.insert(QStringLiteral("item"), item.toJson());
        archived.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    root.insert(QStringLiteral("archived"), archived);
    QString error;
    if (!writeJson(path, root, &error)) {
        const QString message = QStringLiteral("归档写入失败：%1").arg(error);
        addNotice(message);
        emit errorOccurred(message);
        return 0;
    }

    const QSet<QString> ids = [&] {
        QSet<QString> s;
        for (const TodoItem &item : std::as_const(matched))
            s.insert(item.id);
        return s;
    }();
    for (int i = m_todos.size() - 1; i >= 0; --i) {
        if (ids.contains(m_todos.at(i).id))
            m_todos.removeAt(i);
    }
    scheduleTodosSave(0);
    emit todosChanged();
    return matched.size();
}

int DataStore::archiveCompletedOlderThan(int days)
{
    if (days < 0)
        return 0;
    const QDateTime now = QDateTime::currentDateTime();
    return archiveThenRemoveMatching([&](const TodoItem &item) {
        return isCompletedOlderThan(item, days, now);
    });
}

int DataStore::archiveAllCompleted()
{
    return archiveThenRemoveMatching([](const TodoItem &) { return true; });
}

void DataStore::applyRetentionOnLoad()
{
    const int days = m_settings.retentionDays;
    if (days < 1)
        return;
    const int removed = purgeCompletedOlderThan(days);
    if (removed > 0)
        addNotice(QStringLiteral("已按保留策略清理 %1 条超期完成项（>%2 天）").arg(removed).arg(days));
}

void DataStore::setDraft(const QString &draft)
{
    const QString next = draft.left(1000);
    if (m_settings.draft == next)
        return;
    m_settings.draft = next;
    scheduleSettingsSave(450);
}

void DataStore::setDraftDue(QDate date)
{
    if (m_settings.draftDue == date)
        return;
    m_settings.draftDue = date;
    scheduleSettingsSave(250);
}

bool DataStore::groupExpanded(GroupId id) const
{
    const auto it = m_settings.groupExpanded.constFind(groupKey(id));
    return it == m_settings.groupExpanded.constEnd() ? true : it.value();
}

void DataStore::setGroupExpanded(GroupId id, bool expanded)
{
    const QString key = groupKey(id);
    if (groupExpanded(id) == expanded && m_settings.groupExpanded.contains(key))
        return;
    m_settings.groupExpanded.insert(key, expanded);
    scheduleSettingsSave(0);
}

void DataStore::setAlwaysOnTop(bool on)
{
    if (m_settings.alwaysOnTop == on)
        return;
    m_settings.alwaysOnTop = on;
    scheduleSettingsSave(0);
    emit settingsChanged();
}

void DataStore::setAutoHide(bool on)
{
    if (m_settings.autoHide == on)
        return;
    m_settings.autoHide = on;
    scheduleSettingsSave(0);
    emit settingsChanged();
}

void DataStore::setAutoStartPreference(bool on)
{
    if (m_settings.autoStart == on)
        return;
    m_settings.autoStart = on;
    scheduleSettingsSave(0);
    emit settingsChanged();
}

void DataStore::setDockSide(int side)
{
    const int next = (side >= 0 && side <= 2) ? side : 0;
    if (m_settings.dockSide == next)
        return;
    m_settings.dockSide = next;
    scheduleSettingsSave(0);
    emit settingsChanged();
}

void DataStore::setCollapsed(bool collapsed)
{
    if (m_settings.collapsed == collapsed)
        return;
    m_settings.collapsed = collapsed;
    scheduleSettingsSave(0);
    emit settingsChanged();
}

void DataStore::setPinned(bool pinned)
{
    if (m_settings.pinned == pinned)
        return;
    m_settings.pinned = pinned;
    scheduleSettingsSave(0);
    emit settingsChanged();
}

void DataStore::setWindowState(const QRect &rect, const QString &screenName, int dockSide,
                               bool collapsed, bool pinned)
{
    if (!validateWindowRect(rect))
        return;
    const int side = (dockSide >= 0 && dockSide <= 2) ? dockSide : 0;
    if (m_settings.windowRect == rect && m_settings.screenName == screenName
        && m_settings.dockSide == side && m_settings.collapsed == collapsed
        && m_settings.pinned == pinned) {
        return;
    }
    m_settings.windowRect = rect;
    m_settings.screenName = screenName;
    m_settings.dockSide = side;
    m_settings.collapsed = collapsed;
    m_settings.pinned = pinned;
    scheduleSettingsSave(250);
}

void DataStore::scheduleTodosSave(int delayMs)
{
    m_todosDirty = true;
    if (delayMs <= 0) {
        m_todoSaveTimer.stop();
        saveTodosNow();
        return;
    }
    m_todoSaveTimer.start(delayMs);
}

void DataStore::scheduleSettingsSave(int delayMs)
{
    m_settingsDirty = true;
    if (delayMs <= 0) {
        m_settingsSaveTimer.stop();
        saveSettingsNow();
        return;
    }
    m_settingsSaveTimer.start(delayMs);
}

void DataStore::saveTodosNow()
{
    m_todoSaveTimer.stop();
    if (!m_todosDirty)
        return;
    QJsonArray array;
    for (const TodoItem &item : std::as_const(m_todos))
        array.append(item.toJson());
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    root.insert(QStringLiteral("todos"), array);
    QString error;
    if (writeJson(todosPath(), root, &error)) {
        m_todosDirty = false;
        m_lastError.clear();
    } else {
        const QString message = QStringLiteral("待办保存失败：%1").arg(error);
        m_lastError = message;
        addNotice(message);
        emit errorOccurred(message);
    }
}

void DataStore::saveSettingsNow()
{
    m_settingsSaveTimer.stop();
    if (!m_settingsDirty)
        return;

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    root.insert(QStringLiteral("alwaysOnTop"), m_settings.alwaysOnTop);
    root.insert(QStringLiteral("autoHide"), m_settings.autoHide);
    root.insert(QStringLiteral("autoStart"), m_settings.autoStart);
    root.insert(QStringLiteral("dockSide"), m_settings.dockSide);
    root.insert(QStringLiteral("collapsed"), m_settings.collapsed);
    root.insert(QStringLiteral("pinned"), m_settings.pinned);
    root.insert(QStringLiteral("retentionDays"), m_settings.retentionDays);
    root.insert(QStringLiteral("draft"), m_settings.draft);
    root.insert(QStringLiteral("draftDue"),
                m_settings.draftDue.isValid() ? m_settings.draftDue.toString(Qt::ISODate) : QString());

    if (m_settings.windowRect.isValid()) {
        QJsonObject rect;
        rect.insert(QStringLiteral("x"), m_settings.windowRect.x());
        rect.insert(QStringLiteral("y"), m_settings.windowRect.y());
        rect.insert(QStringLiteral("w"), m_settings.windowRect.width());
        rect.insert(QStringLiteral("h"), m_settings.windowRect.height());
        root.insert(QStringLiteral("window"), rect);
    }
    root.insert(QStringLiteral("screen"), m_settings.screenName);

    QJsonObject groups;
    for (auto it = m_settings.groupExpanded.constBegin(); it != m_settings.groupExpanded.constEnd(); ++it)
        groups.insert(it.key(), it.value());
    root.insert(QStringLiteral("groups"), groups);

    QString error;
    if (writeJson(settingsPath(), root, &error)) {
        m_settingsDirty = false;
        m_lastError.clear();
    } else {
        const QString message = QStringLiteral("设置保存失败：%1").arg(error);
        m_lastError = message;
        addNotice(message);
        emit errorOccurred(message);
    }
}

bool DataStore::writeJson(const QString &path, const QJsonObject &root, QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("无法创建目录 %1").arg(QDir::toNativeSeparators(info.absolutePath()));
        return false;
    }

    const QString backup = path + QStringLiteral(".bak");
    if (QFileInfo::exists(path)) {
        QFile::remove(backup);
        if (!QFile::copy(path, backup)) {
            qWarning() << "Sticky Todo: could not refresh backup for" << path;
        }
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

void DataStore::loadTodos()
{
    const QString path = todosPath();
    if (!QFileInfo::exists(path)) {
        m_todos.clear();
        return;
    }

    auto readTodoFile = [](const QString &filePath, QVector<TodoItem> *items,
                           int *invalidCount, QString *why) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (why)
                *why = file.errorString();
            return false;
        }
        const QByteArray bytes = file.readAll();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (why)
                *why = parseError.errorString();
            return false;
        }
        if (!doc.isObject()) {
            if (why)
                *why = QStringLiteral("根节点不是对象");
            return false;
        }
        const QJsonObject root = doc.object();
        const QJsonValue arrayValue = root.value(QStringLiteral("todos"));
        if (!arrayValue.isArray()) {
            if (why)
                *why = QStringLiteral("todos 字段缺失或类型错误");
            return false;
        }
        const int schema = root.value(QStringLiteral("schemaVersion")).toInt(kSchemaVersion);
        items->clear();
        int invalid = 0;
        const QJsonArray array = arrayValue.toArray();
        items->reserve(array.size());
        for (const QJsonValue &value : array) {
            if (!value.isObject()) {
                ++invalid;
                continue;
            }
            bool ok = false;
            const TodoItem item = TodoItem::fromJson(value.toObject(), &ok);
            if (ok)
                items->append(item);
            else
                ++invalid;
        }
        if (invalidCount)
            *invalidCount = invalid;
        if (schema > kSchemaVersion && why)
            *why = QStringLiteral("数据版本 %1 高于当前支持的 %2").arg(schema).arg(kSchemaVersion);
        return true;
    };

    QVector<TodoItem> items;
    int invalidCount = 0;
    QString problem;
    if (readTodoFile(path, &items, &invalidCount, &problem)) {
        m_todos = items;
        if (invalidCount > 0) {
            addNotice(QStringLiteral("有 %1 条待办格式异常，已安全跳过并保留原文件。").arg(invalidCount));
        }
        return;
    }

    addNotice(QStringLiteral("待办数据读取失败（%1），正在尝试备份。").arg(problem));
    preserveCorruptFile(path);
    QString backupWarning;
    if (reloadTodoBackup(&backupWarning)) {
        addNotice(backupWarning);
        scheduleTodosSave(0);
    } else {
        m_todos.clear();
        scheduleTodosSave(0);
    }
}

void DataStore::loadSettings()
{
    const QString path = settingsPath();
    if (!QFileInfo::exists(path)) {
        m_settings = AppSettings();
        m_settings.draftDue = QDate::currentDate();
        scheduleSettingsSave(0);
        return;
    }

    auto readSettingsFile = [](const QString &filePath, AppSettings *settings, QString *why) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (why)
                *why = file.errorString();
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (why)
                *why = parseError.errorString();
            return false;
        }
        if (!doc.isObject()) {
            if (why)
                *why = QStringLiteral("根节点不是对象");
            return false;
        }
        const QJsonObject root = doc.object();
        settings->alwaysOnTop = root.value(QStringLiteral("alwaysOnTop")).toBool(true);
        settings->autoHide = root.value(QStringLiteral("autoHide")).toBool(true);
        settings->autoStart = root.value(QStringLiteral("autoStart")).toBool(true);
        settings->dockSide = root.value(QStringLiteral("dockSide")).toInt(0);
        settings->dockSide = qBound(0, settings->dockSide, 2);
        settings->collapsed = root.value(QStringLiteral("collapsed")).toBool(false);
        settings->pinned = root.value(QStringLiteral("pinned")).toBool(false);
        settings->retentionDays = qBound(1, root.value(QStringLiteral("retentionDays")).toInt(30), 3650);
        settings->draft = root.value(QStringLiteral("draft")).toString().left(1000);
        const QString dueText = root.value(QStringLiteral("draftDue")).toString();
        settings->draftDue = QDate::fromString(dueText, Qt::ISODate);
        if (!settings->draftDue.isValid())
            settings->draftDue = QDate::currentDate();
        settings->screenName = root.value(QStringLiteral("screen")).toString();

        const QJsonValue windowValue = root.value(QStringLiteral("window"));
        if (windowValue.isObject()) {
            const QJsonObject window = windowValue.toObject();
            const QRect rect(window.value(QStringLiteral("x")).toInt(),
                             window.value(QStringLiteral("y")).toInt(),
                             window.value(QStringLiteral("w")).toInt(),
                             window.value(QStringLiteral("h")).toInt());
            if (rect.isValid() && rect.width() > 0 && rect.height() > 0)
                settings->windowRect = rect;
        }

        settings->groupExpanded.clear();
        const QJsonValue groupsValue = root.value(QStringLiteral("groups"));
        if (groupsValue.isObject()) {
            const QJsonObject groups = groupsValue.toObject();
            for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
                settings->groupExpanded.insert(it.key(), it.value().toBool(true));
        }
        return true;
    };

    AppSettings parsed;
    QString problem;
    if (readSettingsFile(path, &parsed, &problem)) {
        m_settings = parsed;
        return;
    }

    addNotice(QStringLiteral("设置读取失败（%1），正在尝试备份。").arg(problem));
    preserveCorruptFile(path);
    QString backupWarning;
    if (reloadSettingsBackup(&backupWarning)) {
        addNotice(backupWarning);
        scheduleSettingsSave(0);
    } else {
        m_settings = AppSettings();
        m_settings.draftDue = QDate::currentDate();
        scheduleSettingsSave(0);
    }
}

void DataStore::preserveCorruptFile(const QString &path)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QFileInfo info(path);
    QString target = info.dir().filePath(info.completeBaseName() + QStringLiteral(".corrupt-") + stamp
                                        + QStringLiteral(".") + info.suffix());
    int suffix = 1;
    while (QFileInfo::exists(target)) {
        target = info.dir().filePath(info.completeBaseName() + QStringLiteral(".corrupt-") + stamp
                                     + QStringLiteral("-") + QString::number(suffix++) + QStringLiteral(".")
                                     + info.suffix());
    }
    if (QFile::rename(path, target)) {
        addNotice(QStringLiteral("已将无法读取的文件保留为 %1").arg(QDir::toNativeSeparators(target)));
        return;
    }
    if (QFile::copy(path, target)) {
        addNotice(QStringLiteral("已将无法读取的文件复制为 %1；原文件暂未覆盖。")
                      .arg(QDir::toNativeSeparators(target)));
        return;
    }
    addNotice(QStringLiteral("无法备份损坏的数据文件，请手动检查 %1").arg(QDir::toNativeSeparators(path)));
}

bool DataStore::reloadTodoBackup(QString *warning)
{
    const QString backup = todosPath() + QStringLiteral(".bak");
    if (!QFileInfo::exists(backup))
        return false;

    QFile file(backup);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonValue arrayValue = doc.object().value(QStringLiteral("todos"));
    if (!arrayValue.isArray())
        return false;

    QVector<TodoItem> items;
    for (const QJsonValue &value : arrayValue.toArray()) {
        if (!value.isObject())
            continue;
        bool ok = false;
        const TodoItem item = TodoItem::fromJson(value.toObject(), &ok);
        if (ok)
            items.append(item);
    }
    m_todos = items;
    if (warning)
        *warning = QStringLiteral("已从上一份备份恢复 %1 条待办。").arg(items.size());
    return true;
}

bool DataStore::reloadSettingsBackup(QString *warning)
{
    const QString backup = settingsPath() + QStringLiteral(".bak");
    if (!QFileInfo::exists(backup))
        return false;

    AppSettings fallback;
    QFile file(backup);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    fallback.alwaysOnTop = root.value(QStringLiteral("alwaysOnTop")).toBool(true);
    fallback.autoHide = root.value(QStringLiteral("autoHide")).toBool(true);
    fallback.autoStart = root.value(QStringLiteral("autoStart")).toBool(true);
    fallback.dockSide = qBound(0, root.value(QStringLiteral("dockSide")).toInt(0), 2);
    fallback.collapsed = root.value(QStringLiteral("collapsed")).toBool(false);
    fallback.pinned = root.value(QStringLiteral("pinned")).toBool(false);
    fallback.retentionDays = qBound(1, root.value(QStringLiteral("retentionDays")).toInt(30), 3650);
    fallback.draft = root.value(QStringLiteral("draft")).toString().left(1000);
    fallback.draftDue = QDate::fromString(root.value(QStringLiteral("draftDue")).toString(), Qt::ISODate);
    if (!fallback.draftDue.isValid())
        fallback.draftDue = QDate::currentDate();
    fallback.screenName = root.value(QStringLiteral("screen")).toString();
    const QJsonValue windowValue = root.value(QStringLiteral("window"));
    if (windowValue.isObject()) {
        const QJsonObject window = windowValue.toObject();
        const QRect rect(window.value(QStringLiteral("x")).toInt(), window.value(QStringLiteral("y")).toInt(),
                         window.value(QStringLiteral("w")).toInt(), window.value(QStringLiteral("h")).toInt());
        if (rect.isValid())
            fallback.windowRect = rect;
    }
    const QJsonValue groupsValue = root.value(QStringLiteral("groups"));
    if (groupsValue.isObject()) {
        const QJsonObject groups = groupsValue.toObject();
        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
            fallback.groupExpanded.insert(it.key(), it.value().toBool(true));
    }
    m_settings = fallback;
    if (warning)
        *warning = QStringLiteral("已从上一份备份恢复窗口与设置。");
    return true;
}

void DataStore::addNotice(const QString &text)
{
    if (text.isEmpty())
        return;
    m_notices.append(text);
    qWarning().noquote() << "Sticky Todo:" << text;
}

bool DataStore::validateWindowRect(const QRect &rect) const
{
    return rect.isValid() && rect.width() >= 240 && rect.height() >= 300
        && rect.width() <= 2000 && rect.height() <= 1600;
}
