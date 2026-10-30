#include "settingspage.h"

#include "datastore.h"
#include "theme.h"
#include "widgets.h"

#include <QButtonGroup>
#include <QDir>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

class Separator : public QWidget
{
public:
    explicit Separator(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(1);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setPen(QPen(QColor(211, 196, 160, 95), 1.0));
        painter.drawLine(0, 0, width(), 0);
    }
};

class SettingsCard : public QWidget
{
public:
    explicit SettingsCard(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAutoFillBackground(false);
        m_body = new QVBoxLayout(this);
        m_body->setContentsMargins(14, 5, 14, 5);
        m_body->setSpacing(0);
    }

    QVBoxLayout *body() const { return m_body; }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(209, 194, 158, 100), 1.0));
        painter.setBrush(QColor(255, 255, 255, 54));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    }

private:
    QVBoxLayout *m_body = nullptr;
};

class SettingRow : public QWidget
{
public:
    SettingRow(const QString &title, const QString &subtitle, QWidget *control,
               QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(8);

        auto *texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(1);

        auto *titleLabel = new QLabel(title, this);
        titleLabel->setFont(Theme::uiFont(12, QFont::DemiBold));
        titleLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Ink.name()));
        titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        texts->addWidget(titleLabel);

        m_subtitle = new QLabel(subtitle, this);
        m_subtitle->setFont(Theme::uiFont(10));
        m_subtitle->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Muted.name()));
        m_subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_subtitle->setTextFormat(Qt::PlainText);
        m_subtitle->setVisible(!subtitle.isEmpty());
        texts->addWidget(m_subtitle);

        root->addLayout(texts, 1);
        if (control) {
            control->setParent(this);
            root->addWidget(control, 0, Qt::AlignVCenter);
        }
        setMinimumHeight(subtitle.isEmpty() ? 43 : 50);
    }

    QLabel *subtitleLabel() const { return m_subtitle; }

private:
    QLabel *m_subtitle = nullptr;
};

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = Theme::uiFont(10, QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    label->setFont(font);
    label->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Muted.name()));
    label->setContentsMargins(2, 6, 0, 0);
    return label;
}

QPushButton *dockButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFont(Theme::uiFont(10, QFont::DemiBold));
    button->setFixedHeight(24);
    button->setMinimumWidth(42);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { border: 1px solid rgba(169,154,120,0.58); border-radius: 9px; "
        "background: rgba(255,255,255,0.16); color: #6f6857; padding: 0 7px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.5); }"
        "QPushButton:checked { background: #6f7c51; color: #fffdf2; border-color: #6f7c51; }"));
    return button;
}

} // namespace

SettingsPage::SettingsPage(DataStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(false);
    buildUi();
    refresh();
    connect(m_store, &DataStore::settingsChanged, this, &SettingsPage::refresh);
}

void SettingsPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"));
    m_scroll->verticalScrollBar()->setSingleStep(30);

    m_content = new QWidget(m_scroll);
    m_content->setAttribute(Qt::WA_NoSystemBackground);
    m_content->setAttribute(Qt::WA_TranslucentBackground, false);
    m_content->setAutoFillBackground(false);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(16, 2, 12, 14);
    m_contentLayout->setSpacing(7);
    m_contentLayout->setAlignment(Qt::AlignTop);

    auto *intro = new QLabel(QStringLiteral("把便签调成顺手的样子。"), m_content);
    intro->setFont(Theme::uiFont(13, QFont::DemiBold));
    intro->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Ink.name()));
    m_contentLayout->addWidget(intro);

    auto *introSub = new QLabel(QStringLiteral("所有修改都会立即保存在本机。"), m_content);
    introSub->setFont(Theme::uiFont(10));
    introSub->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Muted.name()));
    m_contentLayout->addWidget(introSub);
    m_contentLayout->addSpacing(4);

    m_contentLayout->addWidget(sectionLabel(QStringLiteral("外观与行为"), m_content));
    m_contentLayout->addWidget(buildGeneralCard());
    m_contentLayout->addWidget(sectionLabel(QStringLiteral("窗口"), m_content));
    m_contentLayout->addWidget(buildWindowCard());
    m_contentLayout->addWidget(sectionLabel(QStringLiteral("数据"), m_content));
    m_contentLayout->addWidget(buildDataCard());
    m_contentLayout->addWidget(sectionLabel(QStringLiteral("关于"), m_content));
    m_contentLayout->addWidget(buildAboutCard());

    auto *footer = new QLabel(QStringLiteral("无账户 · 无同步 · 无遥测"), m_content);
    footer->setAlignment(Qt::AlignHCenter);
    footer->setFont(Theme::uiFont(9));
    footer->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Faint.name()));
    footer->setContentsMargins(0, 5, 0, 0);
    m_contentLayout->addWidget(footer);
    m_contentLayout->addStretch(1);

    m_scroll->setWidget(m_content);
    root->addWidget(m_scroll);
}

QWidget *SettingsPage::buildGeneralCard()
{
    auto *card = new SettingsCard;
    auto *body = card->body();

    m_topSwitch = new SwitchButton(card);
    m_topSwitch->setToolTip(QStringLiteral("让便签保持在其他窗口前面"));
    auto *topRow = new SettingRow(QStringLiteral("窗口置顶"),
                                  QStringLiteral("让便签一直待在其他窗口前面"),
                                  m_topSwitch, card);
    body->addWidget(topRow);
    connect(m_topSwitch, &QAbstractButton::clicked, this, [this](bool on) {
        if (!m_syncing)
            m_store->setAlwaysOnTop(on);
    });

    body->addWidget(new Separator(card));

    m_autoHideSwitch = new SwitchButton(card);
    auto *hideRow = new SettingRow(QStringLiteral("贴边自动收起"),
                                   QStringLiteral("拖到屏幕边缘后自动收成窄标签"),
                                   m_autoHideSwitch, card);
    body->addWidget(hideRow);
    connect(m_autoHideSwitch, &QAbstractButton::clicked, this, [this](bool on) {
        if (!m_syncing)
            m_store->setAutoHide(on);
    });

    body->addWidget(new Separator(card));

    m_autoStartSwitch = new SwitchButton(card);
    auto *startRow = new SettingRow(QStringLiteral("开机自动启动"),
                                    QStringLiteral("登录 Windows 后自动运行，仅当前用户"),
                                    m_autoStartSwitch, card);
    body->addWidget(startRow);
    connect(m_autoStartSwitch, &QAbstractButton::clicked, this, [this](bool on) {
        if (!m_syncing)
            emit autoStartRequested(on);
    });
    return card;
}

QWidget *SettingsPage::buildWindowCard()
{
    auto *card = new SettingsCard;
    auto *body = card->body();

    auto *dockControl = new QWidget(card);
    auto *dockLayout = new QHBoxLayout(dockControl);
    dockLayout->setContentsMargins(0, 0, 0, 0);
    dockLayout->setSpacing(4);

    m_dockNone = dockButton(QStringLiteral("悬浮"), dockControl);
    m_dockLeft = dockButton(QStringLiteral("贴左"), dockControl);
    m_dockRight = dockButton(QStringLiteral("贴右"), dockControl);
    auto *group = new QButtonGroup(dockControl);
    group->setExclusive(true);
    group->addButton(m_dockNone, 0);
    group->addButton(m_dockLeft, 1);
    group->addButton(m_dockRight, 2);
    dockLayout->addWidget(m_dockNone);
    dockLayout->addWidget(m_dockLeft);
    dockLayout->addWidget(m_dockRight);

    auto *dockRow = new SettingRow(QStringLiteral("贴靠位置"),
                                   QStringLiteral("吸附到屏幕左侧或右侧"),
                                   dockControl, card);
    body->addWidget(dockRow);
    connect(m_dockNone, &QPushButton::clicked, this, [this] { emit dockRequested(0); });
    connect(m_dockLeft, &QPushButton::clicked, this, [this] { emit dockRequested(1); });
    connect(m_dockRight, &QPushButton::clicked, this, [this] { emit dockRequested(2); });

    body->addWidget(new Separator(card));

    m_collapseAction = new TextAction(QStringLiteral("现在收起"), card);
    m_collapseAction->setColors(Theme::AccentDark, Theme::Accent);
    m_collapseAction->setFont(Theme::uiFont(11, QFont::DemiBold));
    m_collapseAction->setToolTip(QStringLiteral("收成屏幕边缘的一条窄标签"));
    auto *collapseRow = new SettingRow(QStringLiteral("收起便签"),
                                       QStringLiteral("贴边后仅保留一条窄标签"),
                                       m_collapseAction, card);
    body->addWidget(collapseRow);
    connect(m_collapseAction, &QAbstractButton::clicked, this, &SettingsPage::collapseRequested);

    body->addWidget(new Separator(card));

    auto *resetAction = new TextAction(QStringLiteral("重置位置"), card);
    resetAction->setColors(Theme::InkSoft, Theme::Ink);
    resetAction->setFont(Theme::uiFont(11));
    auto *resetRow = new SettingRow(QStringLiteral("窗口位置"),
                                    QStringLiteral("窗口跑到屏幕外时可以找回"),
                                    resetAction, card);
    body->addWidget(resetRow);
    connect(resetAction, &QAbstractButton::clicked, this, &SettingsPage::resetWindowRequested);
    return card;
}

QWidget *SettingsPage::buildDataCard()
{
    auto *card = new SettingsCard;
    auto *body = card->body();

    auto *privacyRow = new SettingRow(QStringLiteral("本地存储"),
                                      QStringLiteral("数据只保存在这台电脑上，不会上传"),
                                      nullptr, card);
    body->addWidget(privacyRow);
    body->addWidget(new Separator(card));

    auto *openAction = new TextAction(QStringLiteral("打开"), card);
    openAction->setColors(Theme::AccentDark, Theme::Accent);
    openAction->setFont(Theme::uiFont(11, QFont::DemiBold));
    openAction->setToolTip(QStringLiteral("在文件资源管理器中打开数据目录"));
    auto *pathRow = new SettingRow(QStringLiteral("数据目录"), QString(), openAction, card);
    m_pathLabel = pathRow->subtitleLabel();
    m_pathLabel->setVisible(true);
    m_pathLabel->setFont(Theme::uiFont(9));
    m_pathLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Muted.name()));
    m_pathLabel->setTextFormat(Qt::PlainText);
    body->addWidget(pathRow);
    connect(openAction, &QAbstractButton::clicked, this, &SettingsPage::openDataFolderRequested);

    body->addWidget(new Separator(card));

    auto *storageRow = new SettingRow(QStringLiteral("保存方式"),
                                      QStringLiteral("JSON 文件 · 原子写入 · 自动备份"),
                                      nullptr, card);
    body->addWidget(storageRow);
    return card;
}

QWidget *SettingsPage::buildAboutCard()
{
    auto *card = new SettingsCard;
    auto *body = card->body();

    auto *content = new QWidget(card);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 7, 0, 7);
    layout->setSpacing(3);

    auto *titleLine = new QHBoxLayout;
    titleLine->setContentsMargins(0, 0, 0, 0);
    titleLine->setSpacing(7);
    auto *title = new QLabel(QStringLiteral("Sticky Todo"), content);
    title->setFont(Theme::titleFont(14));
    title->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Ink.name()));
    auto *badge = new QLabel(QStringLiteral("v1.0"), content);
    badge->setFont(Theme::uiFont(9, QFont::DemiBold));
    badge->setStyleSheet(QStringLiteral(
        "QLabel { color: #6f7c51; background: rgba(111,124,81,0.12); border-radius: 6px; padding: 1px 5px; }"));
    titleLine->addWidget(title);
    titleLine->addWidget(badge, 0, Qt::AlignVCenter);
    titleLine->addStretch(1);
    layout->addLayout(titleLine);

    auto *desc = new QLabel(QStringLiteral("一张安静待在桌面上的待办便签。"), content);
    desc->setFont(Theme::uiFont(10));
    desc->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Muted.name()));
    layout->addWidget(desc);

    auto *shortcuts = new QLabel(QStringLiteral("快捷键  Ctrl+N 新建 · Ctrl+F 搜索 · Esc 关闭浮层"), content);
    shortcuts->setFont(Theme::uiFont(9));
    shortcuts->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Faint.name()));
    shortcuts->setWordWrap(true);
    layout->addWidget(shortcuts);

    auto *uninstall = new QLabel(QStringLiteral("绿色版：退出后删除程序目录即可；自启项可用 uninstall.ps1 清理。"), content);
    uninstall->setFont(Theme::uiFont(9));
    uninstall->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Faint.name()));
    uninstall->setWordWrap(true);
    layout->addWidget(uninstall);

    body->addWidget(content);
    return card;
}

void SettingsPage::refresh()
{
    if (!m_store)
        return;
    m_syncing = true;
    const AppSettings &settings = m_store->settings();
    m_topSwitch->setChecked(settings.alwaysOnTop);
    m_autoHideSwitch->setChecked(settings.autoHide);
    m_autoStartSwitch->setChecked(settings.autoStart);
    m_dockNone->setChecked(settings.dockSide == 0);
    m_dockLeft->setChecked(settings.dockSide == 1);
    m_dockRight->setChecked(settings.dockSide == 2);

    const QString nativePath = QDir::toNativeSeparators(m_store->dataDir());
    const QFontMetrics metrics(m_pathLabel->font());
    m_pathLabel->setText(metrics.elidedText(nativePath, Qt::ElideMiddle, 208));
    m_pathLabel->setToolTip(nativePath);
    m_collapseAction->setText(settings.collapsed ? QStringLiteral("展开便签")
                                                 : QStringLiteral("现在收起"));
    m_syncing = false;
}
