#include "mainwindow.h"

#include "calendarpopup.h"
#include "datastore.h"
#include "icons.h"
#include "settingspage.h"
#include "dailyreportpage.h"
#include "theme.h"
#include "todoview.h"
#include "widgets.h"
#include "winutils.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <limits>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Qt 6.11 gives a direct child of a translucent window its own HWND. An inset
// card therefore shows up a moment later as a second, smaller window. Keep the
// paper flush with the frame so the two HWNDs occupy the same rectangle.
constexpr int kShadow = 0;
constexpr int kDefaultWidth = 368;
constexpr int kDefaultHeight = 508;
constexpr int kMinWidth = 316;
constexpr int kMinHeight = 400;
constexpr int kTabWidth = 34;
constexpr int kTabHeight = 232;

class ResizeGrip : public QWidget
{
public:
    explicit ResizeGrip(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(18, 18);
        setCursor(Qt::SizeFDiagCursor);
        setToolTip(QStringLiteral("拖动调整便签大小"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(139, 128, 102, underMouse() ? 180 : 105), 1.2));
        for (int i = 0; i < 3; ++i)
            painter.drawLine(width() - 4 - i * 4, height() - 5,
                             width() - 5, height() - 4 - i * 4);
    }
};

class ComposerBar : public QWidget
{
public:
    explicit ComposerBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_NativeWindow, false);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // 圆角外的方形四角必须铺不透明纸色。清成透明会打穿无边框窗口，
        // 拖动时输入条和四角方框都会透出后面网页的颜色。
        painter.setPen(Qt::NoPen);
        painter.setBrush(Theme::PaperBottom);
        painter.drawRect(rect());
        const QRectF box = QRectF(rect()).adjusted(0.6, 0.6, -0.6, -0.6);
        painter.setPen(QPen(QColor(196, 180, 140, 160), 1.0));
        painter.setBrush(QColor(255, 254, 247));
        painter.drawRoundedRect(box, 12, 12);
    }
};

void placeSnackbarOnCard(QWidget *card, QWidget *composerBar, QWidget *snackbar)
{
    if (!card || !snackbar)
        return;
    const int barHeight = 42;
    const int inset = 18;
    int bottom = card->height();
    if (composerBar) {
        QWidget *wrap = composerBar->parentWidget();
        if (wrap && wrap->isVisible() && wrap->parentWidget() == card)
            bottom = wrap->y();
        else if (composerBar->isVisible())
            bottom = composerBar->mapTo(card, QPoint(0, 0)).y();
    }
    const int y = qMax(14, bottom - barHeight - 10);
    snackbar->setGeometry(inset, y, qMax(1, card->width() - inset * 2), barHeight);
}

class DockTab : public QWidget
{
public:
    explicit DockTab(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_NativeWindow, false);
        setAutoFillBackground(false);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("待办便签 · 点击展开，拖动可移出屏幕边缘"));
    }

    void setData(int side, int pending, bool pinned)
    {
        m_side = side;
        m_pending = pending;
        m_pinned = pinned;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QRectF box = QRectF(rect()).adjusted(0.7, 0.7, -0.7, -0.7);
        QLinearGradient gradient(box.topLeft(), box.bottomLeft());
        gradient.setColorAt(0.0, QColor(255, 251, 233, 250));
        gradient.setColorAt(1.0, QColor(248, 235, 193, 250));
        painter.setPen(QPen(QColor(218, 201, 158, 230), 1.0));
        painter.setBrush(gradient);
        painter.drawRoundedRect(box, 12, 12);

        if (m_side == 2) {
            QLinearGradient shade(box.topRight(), box.topLeft());
            shade.setColorAt(0.0, QColor(89, 73, 40, 26));
            shade.setColorAt(1.0, QColor(89, 73, 40, 0));
            painter.fillRect(box.adjusted(box.width() - 10, 0, -1, 0), shade);
        } else {
            QLinearGradient shade(box.topLeft(), box.topRight());
            shade.setColorAt(0.0, QColor(89, 73, 40, 26));
            shade.setColorAt(1.0, QColor(89, 73, 40, 0));
            painter.fillRect(QRectF(box.left() + 1, box.top(), 10, box.height()), shade);
        }

        const qreal badgeY = 19;
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_pending > 0 ? Theme::Accent : QColor(184, 174, 147));
        painter.drawEllipse(QRectF((width() - 22) / 2.0, badgeY, 22, 22));
        QFont countFont = Theme::uiFont(9, QFont::Bold);
        painter.setFont(countFont);
        painter.setPen(QColor(255, 253, 241));
        painter.drawText(QRectF(0, badgeY, width(), 22), Qt::AlignCenter,
                         QString::number(qMin(m_pending, 99)));

        painter.setPen(Theme::InkSoft);
        painter.setFont(Theme::uiFont(12, QFont::DemiBold));
        painter.drawText(QRectF(0, 53, width(), 18), Qt::AlignCenter, QStringLiteral("待"));
        painter.drawText(QRectF(0, 73, width(), 18), Qt::AlignCenter, QStringLiteral("办"));

        painter.setPen(QPen(QColor(151, 137, 105, 80), 1.0));
        painter.drawLine(QPointF(width() / 2.0 - 6, height() - 35),
                         QPointF(width() / 2.0 + 6, height() - 35));
        if (m_pinned)
            drawAppIcon(painter, QRectF(width() / 2.0 - 6, height() - 27, 12, 12),
                        AppIcon::Pin, Theme::Accent, 1.3);

        painter.setPen(QPen(QColor(151, 137, 105, 70), 1.0));
        for (int i = 0; i < 3; ++i)
            painter.drawLine(QPointF(width() / 2.0 - 4, height() - 16 + i * 3),
                             QPointF(width() / 2.0 + 4, height() - 16 + i * 3));
    }

private:
    int m_side = 2;
    int m_pending = 0;
    bool m_pinned = false;
};

QString elidedOneLine(const QString &text, int width, const QFont &font)
{
    return QFontMetrics(font).elidedText(text, Qt::ElideRight, width);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    // A tool window deliberately has no taskbar button. The tray icon is the
    // single entry point for restoring the note, so the header's minimize
    // action hides the window instead of minimizing it to the taskbar.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    QPalette windowPalette = palette();
    windowPalette.setColor(QPalette::Window, Qt::transparent);
    setPalette(windowPalette);
    setMouseTracking(true);
    setWindowTitle(QStringLiteral("Sticky Todo"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    m_store = new DataStore(this);
    const bool loaded = m_store->load();
    qApp->installEventFilter(this);
    m_currentDate = QDate::currentDate();
    buildUi();
    buildTray();
    installShortcuts();
    restoreWindowState();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(80);
    m_collapseTimer = new QTimer(this);
    m_collapseTimer->setSingleShot(true);
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(320);
    m_dateTimer = new QTimer(this);
    m_dateTimer->setSingleShot(true);

    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshTodos);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::runSearch);
    connect(m_hoverTimer, &QTimer::timeout, this, [this] {
        if (m_collapsed && !m_suppressHover && !m_animating && m_tab
            && m_tab->underMouse())
            expandFromTab(false);
    });
    connect(m_collapseTimer, &QTimer::timeout, this, [this] {
        if (!canAutoCollapse() || m_animating || m_collapsed || m_pinned)
            return;
        // underMouse() 在列表重建后会卡住。收起与否只看光标是否还在窗口里。
        if (!m_forceDockCollapse && pointerInsideWindow()) {
            m_collapseTimer->start(450);
            return;
        }
        collapseNow(true);
    });
    connect(m_dateTimer, &QTimer::timeout, this, [this] {
        refreshDateIfNeeded();
        scheduleMidnightRefresh();
    });

    connect(m_store, &DataStore::todosChanged, this, [this] {
        if (!m_refreshTimer->isActive())
            m_refreshTimer->start(0);
    });
    connect(m_store, &DataStore::settingsChanged, this, [this] {
        const AppSettings &s = m_store->settings();
        if ((m_dailyVisible && !s.showDailyReport) || (m_weeklyVisible && !s.showWeeklyReport)
            || (m_monthlyVisible && !s.showMonthlyReport)) {
            closeOverlayPage();
        }
        updateReportButtons();
        updateHeader();
        updateTrayState();
        const bool top = s.alwaysOnTop;
        if (top != m_lastTopMost)
            applyTopMost();
        if (s.autoHide && isDocked() && !m_collapsed && !m_pinned)
            startCollapseCountdown(600);
    });
    connect(m_store, &DataStore::errorOccurred, this, [this](const QString &message) {
        if (m_collapsed)
            expandFromTab(false);
        if (!isVisible())
            show();
        showToast(message, QString(), {}, 9000);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_store)
            m_store->flush();
    });

    connect(m_searchBox, &SearchBox::textChanged, this, &MainWindow::handleSearchChanged);
    connect(m_searchBox, &SearchBox::returnPressed, this, [this] {
        m_todosView->setFocus();
    });
    connect(m_todosView, &TodoScrollView::completionRequested, this,
            &MainWindow::handleCompletion);
    connect(m_todosView, &TodoScrollView::textEdited, this, &MainWindow::handleTextEdited);
    connect(m_todosView, &TodoScrollView::deleteRequested, this, &MainWindow::handleDelete);
    connect(m_todosView, &TodoScrollView::dateRequested, this,
            [this](const QString &id, const QPoint &pos) { openCalendarForTodo(id, pos); });
    connect(m_todosView, &TodoScrollView::groupExpansionChanged, this,
            &MainWindow::handleGroupExpansion);
    connect(m_todosView, &TodoScrollView::clearSearch, this, [this] {
        m_searchBox->clear();
        m_refreshTimer->start(0);
    });
    connect(m_todosView, &TodoScrollView::composeRequested, this, &MainWindow::focusComposer);

    connect(m_settingsPage, &SettingsPage::autoStartRequested, this,
            [this](bool on) { applyAutoStart(on, false); });
    connect(m_settingsPage, &SettingsPage::hotkeyChangeRequested, this,
            [this](const QString &normalized) {
                m_store->setGlobalHotkey(normalized);
                applyGlobalHotkey(false);
            });
    connect(m_settingsPage, &SettingsPage::dockRequested, this, &MainWindow::handleDockRequest);
    connect(m_settingsPage, &SettingsPage::collapseRequested, this,
            &MainWindow::handleCollapseAction);
    connect(m_settingsPage, &SettingsPage::resetWindowRequested, this,
            &MainWindow::resetWindowPosition);
    connect(m_settingsPage, &SettingsPage::openDataFolderRequested, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_store->dataDir()));
    });
    connect(m_settingsPage, &SettingsPage::messageRequested, this, [this](const QString &text) {
        showToast(text);
    });
    connect(m_dailyPage, &DailyReportPage::messageRequested, this, [this](const QString &text) {
        showToast(text);
    });
    connect(m_weeklyPage, &DailyReportPage::messageRequested, this, [this](const QString &text) {
        showToast(text);
    });
    connect(m_monthlyPage, &DailyReportPage::messageRequested, this, [this](const QString &text) {
        showToast(text);
    });

    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationActive)
                    refreshDateIfNeeded();
            });
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *) {
        QTimer::singleShot(0, this, &MainWindow::ensureVisible);
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) {
        QTimer::singleShot(0, this, &MainWindow::ensureVisible);
    });
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen *) {
        QTimer::singleShot(0, this, &MainWindow::ensureVisible);
    });
    for (QScreen *screen : QGuiApplication::screens()) {
        connect(screen, &QScreen::availableGeometryChanged, this, [this](const QRect &) {
            if (!m_dragActive && !m_resizing)
                ensureVisible();
        });
        connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this](qreal) {
            if (!m_dragActive && !m_resizing)
                ensureVisible();
        });
    }

    m_refreshTimer->start(0);
    updateHeader();
    updateTrayState();
    scheduleMidnightRefresh();
    qApp->installNativeEventFilter(this);
    m_startupNoticesPending = true;
    updateReportButtons();

    if (!loaded) {
        showToast(QStringLiteral("数据目录不可用，修改可能无法保存。"), QString(), {}, 9000);
    }
    QTimer::singleShot(0, this, [this] {
        const AppSettings &settings = m_store->settings();
        applyAutoStart(settings.autoStart, true);
        applyTopMost();
        applyGlobalHotkey(true);
    });
}

MainWindow::~MainWindow()
{
    WinUtils::clearGlobalHotkey();
    if (qApp)
        qApp->removeEventFilter(this);
    qApp->removeNativeEventFilter(this);
}

void MainWindow::buildUi()
{
    // Qt 6.11 keeps horizontal and vertical size constraints separately and
    // resizes a top-level layout from QLayoutPrivate::doResize. The paper card
    // used to be in that layout while also being placed by hand for the shadow
    // margin. Rebuilding a row (for example after ticking a todo) invalidated
    // the layout and snapped the card to (0, 0), so a docked note left the edge.
    setMinimumSize(1, 1);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    m_card = new PaperCard(this);
    m_card->setMouseTracking(true);
    m_card->setMinimumSize(1, 1);
    m_card->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    auto *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    cardLayout->setSizeConstraints(QLayout::SetNoConstraint, QLayout::SetNoConstraint);
#else
    cardLayout->setSizeConstraint(QLayout::SetNoConstraint);
#endif

    m_header = new QWidget(m_card);
    m_header->setAttribute(Qt::WA_NoSystemBackground);
    m_header->setAttribute(Qt::WA_TranslucentBackground, false);
    m_header->setAutoFillBackground(false);
    auto *headerLayout = new QVBoxLayout(m_header);
    headerLayout->setContentsMargins(14, 12, 12, 0);
    headerLayout->setSpacing(0);

    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(1);

    m_backButton = new IconButton(AppIcon::ArrowLeft, 26, 16, m_header);
    m_backButton->setToolTip(QStringLiteral("返回待办"));
    m_backButton->hide();
    topRow->addWidget(m_backButton, 0, Qt::AlignVCenter);

    m_titleBlock = new QWidget(m_header);
    m_titleBlock->setAttribute(Qt::WA_NoSystemBackground);
    m_titleBlock->setAttribute(Qt::WA_TranslucentBackground, false);
    m_titleBlock->setAutoFillBackground(false);
    m_titleBlock->setCursor(Qt::OpenHandCursor);
    m_titleBlock->installEventFilter(this);
    auto *titleLayout = new QVBoxLayout(m_titleBlock);
    titleLayout->setContentsMargins(4, 0, 4, 0);
    titleLayout->setSpacing(0);
    m_titleLabel = new QLabel(QStringLiteral("便签待办"), m_titleBlock);
    m_titleLabel->setFont(Theme::titleFont(20));
    m_titleLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Ink.name()));
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_subtitleLabel = new QLabel(m_titleBlock);
    m_subtitleLabel->setFont(Theme::uiFont(10));
    m_subtitleLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Muted.name()));
    m_subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addWidget(m_subtitleLabel);
    topRow->addWidget(m_titleBlock, 1);

    m_pinButton = new IconButton(AppIcon::Pin, 26, 15, m_header);
    m_pinButton->setToolTip(QStringLiteral("贴边并固定展开"));
    m_pinButton->hide();
    topRow->addWidget(m_pinButton, 0, Qt::AlignVCenter);

    m_dailyButton = new IconButton(AppIcon::Calendar, 26, 16, m_header);
    m_dailyButton->setToolTip(QStringLiteral("日报"));
    topRow->addWidget(m_dailyButton, 0, Qt::AlignVCenter);

    m_weeklyButton = new IconButton(AppIcon::Week, 26, 16, m_header);
    m_weeklyButton->setToolTip(QStringLiteral("周报"));
    topRow->addWidget(m_weeklyButton, 0, Qt::AlignVCenter);

    m_monthlyButton = new IconButton(AppIcon::Month, 26, 16, m_header);
    m_monthlyButton->setToolTip(QStringLiteral("月报"));
    topRow->addWidget(m_monthlyButton, 0, Qt::AlignVCenter);

    m_settingsButton = new IconButton(AppIcon::Settings, 26, 17, m_header);
    m_settingsButton->setToolTip(QStringLiteral("设置"));
    topRow->addWidget(m_settingsButton, 0, Qt::AlignVCenter);

    m_hideButton = new IconButton(AppIcon::Minimize, 26, 16, m_header);
    m_hideButton->setToolTip(QStringLiteral("隐藏到托盘（程序继续运行）"));
    topRow->addWidget(m_hideButton, 0, Qt::AlignVCenter);

    m_closeButton = new IconButton(AppIcon::Close, 26, 16, m_header);
    m_closeButton->setIconColor(Theme::InkSoft);
    m_closeButton->setHoverColor(QColor(188, 103, 79, 34));
    m_closeButton->setToolTip(QStringLiteral("隐藏到托盘（程序继续运行）"));
    topRow->addWidget(m_closeButton, 0, Qt::AlignVCenter);

    headerLayout->addLayout(topRow);
    headerLayout->addSpacing(8);

    m_searchBox = new SearchBox(m_header);
    m_searchBox->setPlaceholderText(QStringLiteral("搜索待办…"));
    headerLayout->addWidget(m_searchBox);
    headerLayout->addSpacing(8);
    cardLayout->addWidget(m_header);

    m_stack = new QStackedWidget(m_card);
    m_stack->setAttribute(Qt::WA_NoSystemBackground);
    m_stack->setAttribute(Qt::WA_TranslucentBackground, false);
    m_stack->setAutoFillBackground(false);
    m_stack->setFrameShape(QFrame::NoFrame);
    m_stack->setStyleSheet(QStringLiteral(
        "QStackedWidget { background: transparent; border: none; }"
        "QStackedWidget > QWidget { background: transparent; }"));
    m_todosView = new TodoScrollView(m_store, m_stack);
    m_settingsPage = new SettingsPage(m_store, m_stack);
    m_dailyPage = new DailyReportPage(m_store, ReportKind::Daily, m_stack);
    m_weeklyPage = new DailyReportPage(m_store, ReportKind::Weekly, m_stack);
    m_monthlyPage = new DailyReportPage(m_store, ReportKind::Monthly, m_stack);
    m_stack->addWidget(m_todosView);
    m_stack->addWidget(m_settingsPage);
    m_stack->addWidget(m_dailyPage);
    m_stack->addWidget(m_weeklyPage);
    m_stack->addWidget(m_monthlyPage);
    cardLayout->addWidget(m_stack, 1);

    auto *composerWrap = new QWidget(m_card);
    composerWrap->setAttribute(Qt::WA_NoSystemBackground);
    composerWrap->setAttribute(Qt::WA_TranslucentBackground, false);
    composerWrap->setAutoFillBackground(false);
    auto *composerWrapLayout = new QVBoxLayout(composerWrap);
    composerWrapLayout->setContentsMargins(14, 4, 14, 12);
    composerWrapLayout->setSpacing(0);
    m_composerBar = new ComposerBar(composerWrap);
    auto *composerLayout = new QHBoxLayout(m_composerBar);
    composerLayout->setContentsMargins(5, 5, 8, 5);
    composerLayout->setSpacing(4);
    m_addButton = new CircleAddButton(m_composerBar);
    composerLayout->addWidget(m_addButton, 0, Qt::AlignVCenter);
    m_composer = new QLineEdit(m_composerBar);
    m_composer->setFrame(false);
    m_composer->setFont(Theme::uiFont(13));
    m_composer->setPlaceholderText(QStringLiteral("写下一件要做的事…"));
    m_composer->setMaxLength(4000);
    m_composer->setClearButtonEnabled(false);
    m_composer->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; padding: 0 2px; "
        "selection-background-color: #dce2c2; selection-color: #3b382e; }"));
    QPalette editPalette = m_composer->palette();
    editPalette.setColor(QPalette::Text, Theme::Ink);
    editPalette.setColor(QPalette::PlaceholderText, QColor(158, 148, 123));
    m_composer->setPalette(editPalette);
    composerLayout->addWidget(m_composer, 1);
    m_composerDate = new DateChip(m_composerBar);
    m_composerDate->setCompact(true);
    composerLayout->addWidget(m_composerDate, 0, Qt::AlignVCenter);
    composerWrapLayout->addWidget(m_composerBar);
    cardLayout->addWidget(composerWrap);

    m_snackbar = new Snackbar(m_card);
    m_resizeGrip = new ResizeGrip(m_card);
    m_resizeGrip->installEventFilter(this);

    m_tab = new DockTab(this);
    m_tab->installEventFilter(this);
    m_tab->hide();

    m_composer->setText(m_store->settings().draft);
    m_composerDate->setDate(m_store->settings().draftDue.isValid()
                                ? m_store->settings().draftDue
                                : QDate::currentDate());

    connect(m_backButton, &QAbstractButton::clicked, this, &MainWindow::closeOverlayPage);
    connect(m_dailyButton, &QAbstractButton::clicked, this, &MainWindow::openDailyPage);
    connect(m_weeklyButton, &QAbstractButton::clicked, this, &MainWindow::openWeeklyPage);
    connect(m_monthlyButton, &QAbstractButton::clicked, this, &MainWindow::openMonthlyPage);
    connect(m_settingsButton, &QAbstractButton::clicked, this, &MainWindow::openSettingsPage);
    connect(m_pinButton, &QAbstractButton::clicked, this, &MainWindow::togglePin);
    connect(m_hideButton, &QAbstractButton::clicked, this, &MainWindow::minimizeToTray);
    connect(m_closeButton, &QAbstractButton::clicked, this, &MainWindow::hideToTray);
    connect(m_addButton, &QAbstractButton::clicked, this, &MainWindow::submitComposer);
    connect(m_composer, &QLineEdit::returnPressed, this, &MainWindow::submitComposer);
    connect(m_composer, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_store->setDraft(text);
        // 草稿被清空且光标已经在外面时，离开事件不会再来一次，这里补上收起。
        if (text.trimmed().isEmpty())
            startCollapseCountdown(450);
    });
    connect(m_composerDate, &QAbstractButton::clicked, this, &MainWindow::openCalendarForComposer);
}

void MainWindow::buildTray()
{
    m_trayMenu = new QMenu(this);
    m_trayMenu->setObjectName(QStringLiteral("trayMenu"));
    m_trayMenu->setFont(Theme::uiFont(11));
    m_trayMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    m_trayMenu->setAttribute(Qt::WA_TranslucentBackground);
    m_trayMenu->setAttribute(Qt::WA_StyledBackground);
    m_trayMenu->installEventFilter(this);
    m_trayShowAction = m_trayMenu->addAction(QStringLiteral("显示便签"));
    m_trayHideAction = m_trayMenu->addAction(QStringLiteral("隐藏便签"));
    m_trayMenu->addSeparator();
    auto *settingsAction = m_trayMenu->addAction(QStringLiteral("设置…"));
    m_trayAutoStartAction = m_trayMenu->addAction(QStringLiteral("开机自动启动"));
    m_trayAutoStartAction->setCheckable(true);
    m_trayMenu->addSeparator();
    auto *quitAction = m_trayMenu->addAction(QStringLiteral("退出 Sticky Todo"));

    connect(m_trayShowAction, &QAction::triggered, this, &MainWindow::showAndActivate);
    connect(m_trayHideAction, &QAction::triggered, this, &MainWindow::hideToTray);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettingsPage);
    connect(m_trayAutoStartAction, &QAction::toggled, this, [this](bool on) {
        applyAutoStart(on, false);
    });
    connect(quitAction, &QAction::triggered, this, &MainWindow::quitApplication);
}

void MainWindow::installShortcuts()
{
    auto *newShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+N")), this);
    newShortcut->setContext(Qt::WindowShortcut);
    connect(newShortcut, &QShortcut::activated, this, &MainWindow::focusComposer);

    auto *findShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this);
    findShortcut->setContext(Qt::WindowShortcut);
    connect(findShortcut, &QShortcut::activated, this, &MainWindow::focusSearch);
}

void MainWindow::restoreWindowState()
{
    const AppSettings &settings = m_store->settings();
    m_dockSide = settings.dockSide;
    m_pinned = settings.pinned;
    m_collapsed = settings.collapsed;
    m_currentDate = QDate::currentDate();

    QScreen *screen = screenByName(settings.screenName);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    const QRect avail = screen->availableGeometry();
    QSize size = settings.windowRect.isValid() ? settings.windowRect.size()
                                               : QSize(kDefaultWidth, kDefaultHeight);
    size.setWidth(qBound(kMinWidth, size.width(), qMax(kMinWidth, avail.width())));
    size.setHeight(qBound(kMinHeight, size.height(), qMax(kMinHeight, avail.height())));

    m_expandedWindowRect = QRect(settings.windowRect.topLeft(), size);
    if (!settings.windowRect.isValid()
        || !avail.adjusted(-20, -20, 20, 20).intersects(m_expandedWindowRect)) {
        m_expandedWindowRect = defaultExpandedRect(screen);
    }

    if (m_collapsed && m_dockSide == 0)
        m_dockSide = 2;
    if (m_collapsed)
        m_pinned = false;

    if (isDocked()) {
        m_expandedWindowRect = dockedRectFor(m_expandedWindowRect.size(), screen, m_dockSide);
    } else {
        m_expandedWindowRect.setWidth(qMin(m_expandedWindowRect.width(), avail.width()));
        m_expandedWindowRect.setHeight(qMin(m_expandedWindowRect.height(), avail.height()));
        m_expandedWindowRect.moveLeft(qBound(avail.left(), m_expandedWindowRect.left(),
                                             avail.right() - m_expandedWindowRect.width() + 1));
        m_expandedWindowRect.moveTop(qBound(avail.top(), m_expandedWindowRect.top(),
                                            avail.bottom() - m_expandedWindowRect.height() + 1));
    }
    m_screenName = screen->name();
    m_lastTopMost = settings.alwaysOnTop;
    m_peekExpanded = false;

    setGeometry(m_collapsed ? collapsedRect() : m_expandedWindowRect);
    applyCardGeometry();
    updatePinButton();
}

QScreen *MainWindow::screenByName(const QString &name) const
{
    if (!name.isEmpty()) {
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen->name() == name)
                return screen;
        }
    }
    return nullptr;
}


QScreen *MainWindow::nearestScreen(const QPoint &global) const
{
    if (QScreen *at = QGuiApplication::screenAt(global))
        return at;

    // When the point sits in a multi-monitor gap (or the dead zone created by
    // uniting unequal-height screens), pick the closest availableGeometry.
    QScreen *best = nullptr;
    qint64 bestDist = std::numeric_limits<qint64>::max();
    for (QScreen *screen : QGuiApplication::screens()) {
        const QRect avail = screen->availableGeometry();
        const int dx = global.x() < avail.left() ? avail.left() - global.x()
                     : global.x() > avail.right() ? global.x() - avail.right() : 0;
        const int dy = global.y() < avail.top() ? avail.top() - global.y()
                     : global.y() > avail.bottom() ? global.y() - avail.bottom() : 0;
        const qint64 dist = qint64(dx) * dx + qint64(dy) * dy;
        if (dist < bestDist) {
            bestDist = dist;
            best = screen;
        }
    }
    return best;
}

QScreen *MainWindow::screenForWindow() const
{
    // Prefer geometry over the cached name so multi-monitor drag/dock
    // follows the window's current screen instead of a stale m_screenName.
    // nearestScreen also covers inter-monitor gaps / unequal-height dead zones
    // where screenAt(center) is null.
    QScreen *screen = nearestScreen(frameGeometry().center());
    if (screen)
        return screen;
    screen = screenByName(m_screenName);
    if (screen)
        return screen;
    return QGuiApplication::primaryScreen();
}

QRect MainWindow::defaultExpandedRect(QScreen *screen) const
{
    if (!screen)
        return QRect(80, 80, kDefaultWidth, kDefaultHeight);
    const QRect avail = screen->availableGeometry();
    const int w = qMin(kDefaultWidth, qMax(kMinWidth, avail.width() - 24));
    const int h = qMin(kDefaultHeight, qMax(kMinHeight, avail.height() - 24));
    return QRect(avail.right() - w - 23, avail.top() + 24, w, h);
}

QRect MainWindow::dockedRectFor(const QSize &size, QScreen *screen, int side) const
{
    if (!screen)
        return QRect(0, 0, size.width(), size.height());
    const QRect avail = screen->availableGeometry();
    QSize target = size;
    target.setWidth(qBound(qMin(kMinWidth, avail.width()), target.width(), avail.width()));
    target.setHeight(qBound(qMin(kMinHeight, avail.height()), target.height(), avail.height()));
    const int x = side == 1 ? avail.left() - kShadow
                            : avail.right() - target.width() + 1 + kShadow;
    int y = m_expandedWindowRect.isValid() ? m_expandedWindowRect.y()
                                          : avail.center().y() - target.height() / 2;
    y = qBound(avail.top(), y, avail.bottom() - target.height() + 1);
    return QRect(x, y, target.width(), target.height());
}

QRect MainWindow::collapsedRect() const
{
    QScreen *screen = screenForWindow();
    if (!screen)
        return QRect(0, 0, kTabWidth, kTabHeight);
    const QRect avail = screen->availableGeometry();
    int y = m_expandedWindowRect.isValid()
                ? m_expandedWindowRect.center().y() - kTabHeight / 2
                : avail.center().y() - kTabHeight / 2;
    y = qBound(avail.top(), y, avail.bottom() - kTabHeight + 1);
    const int x = m_dockSide == 1 ? avail.left() : avail.right() - kTabWidth + 1;
    return QRect(x, y, kTabWidth, kTabHeight);
}

void MainWindow::applyCardGeometry()
{
    if (!m_card || !m_tab)
        return;
    if (m_collapsed) {
        m_card->hide();
        m_tab->setGeometry(rect());
        m_tab->raise();
        m_tab->show();
        return;
    }
    m_tab->hide();
    m_card->setGeometry(kShadow, kShadow, qMax(1, width() - 2 * kShadow),
                        qMax(1, height() - 2 * kShadow));
    m_card->show();
    m_card->raise();
    // Keep the toast inside the card's corner curve. A bar that crosses the
    // radius paints past the paper and looks like another window.
    placeSnackbarOnCard(m_card, m_composerBar, m_snackbar);
    if (m_resizeGrip)
        m_resizeGrip->move(m_card->width() - 24, m_card->height() - 24);
    if (m_snackbar && m_snackbar->isVisible())
        m_snackbar->raise();
    if (m_resizeGrip)
        m_resizeGrip->raise();
}

QSize MainWindow::sizeHint() const
{
    if (m_collapsed)
        return QSize(kTabWidth, kTabHeight);
    if (m_expandedWindowRect.isValid())
        return m_expandedWindowRect.size();
    return QSize(kDefaultWidth, kDefaultHeight);
}

QSize MainWindow::minimumSizeHint() const
{
    return QSize(1, 1);
}

void MainWindow::reapplyDockGeometry()
{
    if (m_restoringDock || m_animating || m_dragActive || m_resizing || m_tabDragActive)
        return;
    if (isDocked() && !m_collapsed && isVisible()) {
        // Geometry-first: do not re-dock onto a stale named screen when the
        // window already sits on another monitor.
        QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
        if (!screen)
            screen = screenForWindow();
        const QSize size = m_expandedWindowRect.isValid()
                               ? m_expandedWindowRect.size()
                               : QSize(kDefaultWidth, kDefaultHeight);
        if (screen) {
            if (m_screenName != screen->name())
                m_screenName = screen->name();
            const QRect expected = dockedRectFor(size, screen, m_dockSide);
            if (geometry() != expected) {
                m_restoringDock = true;
                setGeometry(expected);
                m_restoringDock = false;
            }
        }
    }
    applyCardGeometry();
}

QImage MainWindow::trayImage() const
{
    QImage image(QStringLiteral(":/icons/app.png"));
    if (image.isNull()) {
        image = QImage(32, 32, QImage::Format_ARGB32);
        image.fill(qRgb(244, 214, 122));
    }
    return image;
}

QString MainWindow::trayTipText() const
{
    int pending = 0;
    if (m_store) {
        for (const TodoItem &item : m_store->todos()) {
            if (!item.completed)
                ++pending;
        }
    }
    return QStringLiteral("Sticky Todo · %1 项待办\n单击显示，右键可退出").arg(pending);
}

void MainWindow::ensureTrayIcon()
{
    if (m_quitting)
        return;
    if (!WinUtils::trayAvailable()) {
        if (m_trayAttempts < 20)
            QTimer::singleShot(500, this, &MainWindow::ensureTrayIcon);
        ++m_trayAttempts;
        return;
    }

    // Qt 6.11's QWindowsSystemTrayIcon registers the icon from the constructor,
    // before this tool window is on screen. Shell_NotifyIcon then fails and is
    // not retried, so Windows never lists the icon. Add it from the real window
    // after show, and keep trying until the shell accepts it.
    const bool ok = WinUtils::installTrayIcon(this, trayImage(), trayTipText());
    if (ok)
        m_trayInstalled = true;
    const bool promoted = ok && WinUtils::promoteTrayIcon();
    const QString detail = WinUtils::lastTrayError();
    // 拒绝访问不会因为重试而好转，立刻说明原因，避免用户以为程序没有托盘。
    const bool denied = detail.contains(QStringLiteral("错误 5"));
    if ((!ok || !promoted) && !denied && m_trayAttempts < 15) {
        ++m_trayAttempts;
        QTimer::singleShot(400, this, &MainWindow::ensureTrayIcon);
        return;
    }
    if (!m_trayInstalled) {
        const QString message = denied
            ? QStringLiteral("这个目录被标成了低完整性，Windows 不允许托盘图标。请改用 Start StickyTodo.cmd 打开。")
            : QStringLiteral("托盘图标未能加入任务栏：%1").arg(detail);
        showToast(message, QString(), {}, 8000);
    }
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Punch out anything the style or DWM left in the shadow margin, so the
    // only opaque shape is the paper card itself.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (m_collapsed)
        return;
    const QRectF card(kShadow, kShadow, width() - 2 * kShadow, height() - 2 * kShadow);
    painter.setPen(Qt::NoPen);
    if (m_animating) {
        // 动画期间只画一层柔影，避免每帧重复十几次透明圆角绘制。
        painter.setBrush(QColor(92, 77, 47, 18));
        painter.drawRoundedRect(card.adjusted(-2, -1, 2, 2), 18, 18);
        return;
    }
    for (int i = kShadow; i >= 1; --i) {
        const int alpha = qMax(1, 5 - i / 4);
        painter.setBrush(QColor(92, 77, 47, alpha));
        const qreal grow = i * 0.55;
        painter.drawRoundedRect(card.adjusted(-grow, -grow * 0.45, grow, grow * 0.9),
                               16 + grow * 0.4, 16 + grow * 0.4);
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_animating)
        applyCardGeometry();
}

void MainWindow::saveWindowState()
{
    if (!m_store)
        return;
    QScreen *screen = screenForWindow();
    if (screen)
        m_screenName = screen->name();
    QRect rect = m_expandedWindowRect;
    if (!m_collapsed && !m_animating && !m_resizing && !m_dragActive && isVisible()) {
        rect = geometry();
        if (isDocked() && screen)
            rect = dockedRectFor(rect.size(), screen, m_dockSide);
        m_expandedWindowRect = rect;
    }
    m_store->setWindowState(rect, m_screenName, m_dockSide, m_collapsed, m_pinned);
}

void MainWindow::ensureVisible()
{
    if (m_animating || m_dragActive || m_resizing || !isVisible())
        return;
    QScreen *screen = screenForWindow();
    if (!screen)
        return;
    const QRect avail = screen->availableGeometry();
    if (isDocked()) {
        m_expandedWindowRect = dockedRectFor(m_expandedWindowRect.size(), screen, m_dockSide);
    } else {
        QSize size = m_expandedWindowRect.size();
        size.setWidth(qMin(size.width(), avail.width()));
        size.setHeight(qMin(size.height(), avail.height()));
        m_expandedWindowRect.setSize(size);
        m_expandedWindowRect.moveLeft(qBound(avail.left(), m_expandedWindowRect.left(),
                                             avail.right() - size.width() + 1));
        m_expandedWindowRect.moveTop(qBound(avail.top(), m_expandedWindowRect.top(),
                                            avail.bottom() - size.height() + 1));
    }
    setGeometry(m_collapsed ? collapsedRect() : m_expandedWindowRect);
    applyCardGeometry();
    saveWindowState();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyCardGeometry();
    applyTopMost();
    WinUtils::suppressSystemFrame(this);
    if (m_card)
        WinUtils::suppressSystemFrame(m_card);
    if (m_tab)
        WinUtils::suppressSystemFrame(m_tab);
    if (m_startupNoticesPending) {
        m_startupNoticesPending = false;
        const QStringList notices = m_store->takeNotices();
        if (!notices.isEmpty()) {
            QTimer::singleShot(450, this, [this, notices] {
                showToast(notices.join(QStringLiteral("；")), QString(), {}, 9000);
            });
        } else if (m_store->todos().isEmpty()) {
            QTimer::singleShot(600, this, [this] {
                showToast(QStringLiteral("把要做的事写在下边，按 Enter 保存。"), QString(), {}, 4500);
            });
        }
    }
    if (isDocked() && !m_collapsed && !m_pinned && m_store->settings().autoHide)
        startCollapseCountdown(700);
    updateTrayState();
    // Qt gives the first QLineEdit (the new-item composer) initial focus.
    // Move focus to the list on show so a docked note can auto-collapse; the
    // composer still takes focus when the user clicks it or presses Ctrl+N.
    if (!m_settingsVisible) {
        QTimer::singleShot(0, this, [this] {
            if (m_todosView && !m_settingsVisible && !m_collapsed)
                m_todosView->setFocus(Qt::OtherFocusReason);
        });
    }
    ensureTrayIcon();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_quitting) {
        event->accept();
        return;
    }
    // With tray: X hides like minimize (—). Without tray (Linux stub / #4):
    // hideToTray() already routes to save+quit so the first dismiss exits.
    event->ignore();
    hideToTray();
}

void MainWindow::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    if (m_collapsed) {
        if (!m_suppressHover && m_hoverTimer && !m_hoverTimer->isActive())
            m_hoverTimer->start(320);
        return;
    }
    // After snapping, an Enter can be generated while the note expands under
    // the pointer. Do not let that cancel the deliberate post-drag collapse.
    // 列表重建有时会送出一个虚假的 Enter。光标其实在外面时不能把收起取消掉。
    if (!m_forceDockCollapse && pointerInsideWindow())
        stopCollapseCountdown();
}

void MainWindow::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (m_collapsed) {
        if (m_hoverTimer)
            m_hoverTimer->stop();
        m_suppressHover = false;
        return;
    }
    if (isDocked() && !m_pinned
        && (m_store->settings().autoHide || m_peekExpanded)
        && !m_settingsVisible) {
        startCollapseCountdown(450);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();

    if (watched == m_trayMenu && (type == QEvent::Show || type == QEvent::Resize))
        WinUtils::prepareRoundedPopup(m_trayMenu);

    if (type == QEvent::WindowDeactivate && watched == this) {
        QTimer::singleShot(0, this, [this] {
            if (!pointerInsideWindow())
                startCollapseCountdown(450);
        });
    }

    // A deliberate click/move means the user wants to use the note. Cancel a
    // pending post-drag auto-collapse instead of pulling it away mid-action.
    if (type == QEvent::MouseButtonPress && m_forceDockCollapse) {
        m_forceDockCollapse = false;
        if (m_collapseTimer)
            m_collapseTimer->stop();
    }

    // Mouse moves/releases during a drag or resize can be delivered to any
    // child under the cursor. Handle them at application level so a tab that
    // turns into a floating window mid-drag still tracks the pointer.
    if (m_resizing) {
        if (type == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const QPoint delta = mouse->globalPosition().toPoint() - m_resizeStartGlobal;
            QScreen *screen = screenForWindow();
            const QRect avail = screen ? screen->availableGeometry()
                                       : QRect(0, 0, 1920, 1080);
            QSize target = m_resizeStartSize + QSize(delta.x(), delta.y());
            target.setWidth(qBound(kMinWidth, target.width(), qMax(kMinWidth, avail.width())));
            target.setHeight(qBound(kMinHeight, target.height(), qMax(kMinHeight, avail.height())));
            setGeometry(QRect(m_resizeStartPos, target));
            return true;
        }
        if (type == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                finishResize();
                return true;
            }
        }
    }

    if (type == QEvent::MouseButtonRelease && m_dragActive) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_dragActive = false;
            m_titleBlock->setCursor(Qt::OpenHandCursor);
            if (m_dragMoved)
                finishDrag();
            m_dragMoved = false;
            return true;
        }
    }

    if (type == QEvent::MouseMove && m_dragActive) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (!(mouse->buttons() & Qt::LeftButton))
            return true;
        const QPoint global = mouse->globalPosition().toPoint();
        if (!m_dragMoved) {
            if ((global - m_dragStartGlobal).manhattanLength() < QApplication::startDragDistance())
                return true;
            m_dragMoved = true;
            if (isDocked())
                undockForDrag();
            m_dragOffset = global - frameGeometry().topLeft();
        }

        // Clamp against the union of all screens so the window can cross
        // monitors; a single screenAt() box traps the drag on one display.
        QRect avail;
        for (QScreen *s : QGuiApplication::screens())
            avail = avail.united(s->availableGeometry());
        if (!avail.isValid()) {
            QScreen *screen = QGuiApplication::screenAt(global);
            if (!screen)
                screen = screenForWindow();
            avail = screen ? screen->availableGeometry()
                           : QRect(0, 0, 1920, 1080);
        }
        QPoint target = global - m_dragOffset;
        const int minX = avail.left() - qMax(60, width() - 120);
        const int maxX = avail.right() - 120;
        const int maxY = avail.bottom() - 40;
        target.setX(qBound(minX, target.x(), qMax(minX, maxX)));
        target.setY(qBound(avail.top(), target.y(), qMax(avail.top(), maxY)));
        // Union AABB can include dead space between / below unequal screens.
        // Keep the frame center on a real screen so release snap cannot jump.
        {
            const QPoint center = target + QPoint(width() / 2, height() / 2);
            if (!QGuiApplication::screenAt(center)) {
                if (QScreen *nearestScr = nearestScreen(center)) {
                    const QRect a = nearestScr->availableGeometry();
                    QPoint c = center;
                    c.setX(qBound(a.left(), c.x(), a.right()));
                    c.setY(qBound(a.top(), c.y(), a.bottom()));
                    target = c - QPoint(width() / 2, height() / 2);
                }
            }
        }
        move(target);
        return true;
    }

    if (watched == m_resizeGrip) {
        if (type == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_resizing = true;
                m_dragActive = false;
                m_resizeStartSize = size();
                m_resizeStartGlobal = mouse->globalPosition().toPoint();
                m_resizeStartPos = pos();
                stopCollapseCountdown();
                return true;
            }
        } else if (type == QEvent::MouseMove && m_resizing) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const QPoint delta = mouse->globalPosition().toPoint() - m_resizeStartGlobal;
            QScreen *screen = screenForWindow();
            const QRect avail = screen ? screen->availableGeometry()
                                       : QRect(0, 0, 1920, 1080);
            QSize target = m_resizeStartSize + QSize(delta.x(), delta.y());
            target.setWidth(qBound(kMinWidth, target.width(), qMax(kMinWidth, avail.width())));
            target.setHeight(qBound(kMinHeight, target.height(), qMax(kMinHeight, avail.height())));
            setGeometry(QRect(m_resizeStartPos, target));
            return true;
        } else if (type == QEvent::MouseButtonRelease && m_resizing) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                finishResize();
                return true;
            }
        }
    }

    if (watched == m_titleBlock) {
        if (type == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && !m_collapsed) {
                m_dragActive = true;
                m_dragMoved = false;
                m_dragStartGlobal = mouse->globalPosition().toPoint();
                m_dragOffset = m_dragStartGlobal - frameGeometry().topLeft();
                m_titleBlock->setCursor(Qt::ClosedHandCursor);
                stopCollapseCountdown();
                if (m_composer)
                    m_composer->clearFocus();
                if (m_searchBox)
                    m_searchBox->clearFocus();
                if (m_todosView) {
                    if (auto *editor = qobject_cast<QLineEdit *>(m_todosView->focusWidget()))
                        editor->clearFocus();
                }
                return true;
            }
        } else if (type == QEvent::MouseButtonDblClick) {
            return true;
        }
    }

    if (watched == m_tab) {
        if (type == QEvent::Enter) {
            if (m_collapsed && !m_suppressHover && m_hoverTimer
                && !m_hoverTimer->isActive())
                m_hoverTimer->start(320);
            return true;
        }
        if (type == QEvent::Leave) {
            if (m_hoverTimer)
                m_hoverTimer->stop();
            m_suppressHover = false;
            if (m_peekExpanded && !m_pinned)
                startCollapseCountdown(450);
            return true;
        }
        if (type == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_tabDragActive = true;
                m_tabDragMoved = false;
                m_tabDragOffset = mouse->globalPosition().toPoint() - frameGeometry().topLeft();
                m_dragStartGlobal = mouse->globalPosition().toPoint();
                stopCollapseCountdown();
                if (m_hoverTimer)
                    m_hoverTimer->stop();
                return true;
            }
        } else if (type == QEvent::MouseMove && m_tabDragActive) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const QPoint global = mouse->globalPosition().toPoint();
            if ((global - m_dragStartGlobal).manhattanLength() < 6)
                return true;

            m_tabDragMoved = true;
            const QSize restored = m_expandedWindowRect.size().isValid()
                                       ? m_expandedWindowRect.size()
                                       : QSize(kDefaultWidth, kDefaultHeight);
            m_collapsed = false;
            m_peekExpanded = false;
            m_pinned = false;
            m_dockSide = 0;
            m_animating = false;

            QScreen *screen = QGuiApplication::screenAt(global);
            if (!screen)
                screen = screenForWindow();
            const QRect avail = screen ? screen->availableGeometry()
                                       : QRect(0, 0, 1920, 1080);
            QPoint topLeft(global.x() - 62, global.y() - 28);
            topLeft.setX(qBound(avail.left(), topLeft.x(),
                                qMax(avail.left(), avail.right() - restored.width() + 1)));
            topLeft.setY(qBound(avail.top(), topLeft.y(),
                                qMax(avail.top(), avail.bottom() - restored.height() + 1)));
            setGeometry(QRect(topLeft, restored));
            m_expandedWindowRect = geometry();
            applyCardGeometry();
            m_tabDragActive = false;
            m_dragActive = true;
            m_dragMoved = true;
            m_dragOffset = global - frameGeometry().topLeft();
            m_titleBlock->setCursor(Qt::ClosedHandCursor);
            WinUtils::activateWindow(this);
            return true;
        } else if (type == QEvent::MouseButtonRelease && m_tabDragActive) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_tabDragActive = false;
                if (!m_tabDragMoved && m_collapsed) {
                    // Clicking the tab opens the note temporarily; moving the
                    // pointer away lets it collapse again. Permanent expansion
                    // is the job of the pin button.
                    expandFromTab(false);
                    if (m_todosView)
                        m_todosView->setFocus(Qt::OtherFocusReason);
                }
                m_tabDragMoved = false;
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

bool MainWindow::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    if (WinUtils::isGlobalHotkeyEvent(eventType, message)) {
        toggleByHotkey();
        if (result)
            *result = 0;
        return true;
    }
#ifndef Q_OS_WIN
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
#else
    if (eventType != "windows_generic_MSG" || !message)
        return false;

    auto *msg = static_cast<MSG *>(message);
    if (msg->message == static_cast<UINT>(WinUtils::activationMessage())) {
        showAndActivate();
        if (result)
            *result = 0;
        return true;
    }
    if (msg->message == static_cast<UINT>(WinUtils::trayCallbackMessage())) {
        // NOTIFYICON_VERSION_4 packs the event in the low word. The high word
        // is the icon id, so comparing the raw lParam never matches a click.
        const UINT event = LOWORD(static_cast<DWORD>(msg->lParam));
        const bool activate = event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK
            || event == NIN_SELECT || event == NIN_KEYSELECT || event == NIN_BALLOONUSERCLICK;
        const bool menu = event == WM_RBUTTONUP || event == WM_CONTEXTMENU;
        if (activate) {
            showAndActivate();
        } else if (menu && m_trayMenu) {
            SetForegroundWindow(msg->hwnd);
            m_trayMenu->popup(QCursor::pos());
            PostMessageW(msg->hwnd, WM_NULL, 0, 0);
        }
        if (result)
            *result = 0;
        return true;
    }
    if (msg->message == WM_QUERYENDSESSION) {
        if (m_store)
            m_store->flush();
        // Windows 关机/注销时不能把系统卡住；尽力保存后允许会话结束。
        if (result)
            *result = TRUE;
        return true;
    }
    if (msg->message == WM_ENDSESSION && msg->wParam) {
        if (m_store)
            m_store->flush();
    }
    return false;
#endif
}

void MainWindow::updateHeader()
{
    if (!m_store || !m_titleLabel || !m_subtitleLabel)
        return;

    int pending = 0;
    for (const TodoItem &item : m_store->todos()) {
        if (!item.completed)
            ++pending;
    }

    if (m_settingsVisible) {
        m_titleLabel->setText(QStringLiteral("设置"));
        m_subtitleLabel->setText(QStringLiteral("轻量、本地、离线"));
    } else if (m_dailyVisible) {
        m_titleLabel->setText(QStringLiteral("日报"));
        m_subtitleLabel->setText(dateHeadline(m_currentDate) + QStringLiteral(" · 今日完成快照"));
    } else if (m_weeklyVisible) {
        m_titleLabel->setText(QStringLiteral("周报"));
        const QDate start = reportWeekStart(m_currentDate);
        const QDate end = reportWeekEnd(m_currentDate);
        m_subtitleLabel->setText(QStringLiteral("%1 ~ %2 · 本周完成快照")
                                     .arg(start.toString(QStringLiteral("MM-dd")),
                                          end.toString(QStringLiteral("MM-dd"))));
    } else if (m_monthlyVisible) {
        m_titleLabel->setText(QStringLiteral("月报"));
        m_subtitleLabel->setText(reportMonthKey(m_currentDate) + QStringLiteral(" · 本月完成快照"));
    } else {
        m_titleLabel->setText(QStringLiteral("便签待办"));
        QString subtitle = dateHeadline(m_currentDate);
        if (!m_searchQuery.isEmpty())
            subtitle += QStringLiteral(" · 搜索中");
        subtitle += QStringLiteral(" · %1 项待办").arg(pending);
        m_subtitleLabel->setText(subtitle);
    }

    const bool overlay = m_settingsVisible || reportOverlayVisible();
    m_backButton->setVisible(overlay);
    m_searchBox->setVisible(!overlay);
    m_settingsButton->setVisible(!overlay);
    updateReportButtons();
    m_pinButton->setVisible(!overlay);
    updatePinButton();
    updateTrayState();

    if (m_tab) {
        auto *tab = static_cast<DockTab *>(m_tab);
        tab->setData(m_dockSide, pending, m_pinned);
    }
    if (m_settingsVisible)
        setWindowTitle(QStringLiteral("便签待办 · 设置"));
    else if (m_dailyVisible)
        setWindowTitle(QStringLiteral("便签待办 · 日报"));
    else if (m_weeklyVisible)
        setWindowTitle(QStringLiteral("便签待办 · 周报"));
    else if (m_monthlyVisible)
        setWindowTitle(QStringLiteral("便签待办 · 月报"));
    else
        setWindowTitle(QStringLiteral("便签待办 · %1 项待办").arg(pending));
}

void MainWindow::updatePinButton()
{
    if (!m_pinButton)
        return;
    m_pinButton->setVisible(!m_settingsVisible && !reportOverlayVisible());
    m_pinButton->setIconColor(m_pinned ? Theme::Accent : Theme::InkSoft);
    if (!isDocked()) {
        m_pinButton->setToolTip(QStringLiteral("贴边并固定展开（点击后停靠屏幕边缘）"));
    } else if (m_pinned) {
        m_pinButton->setToolTip(QStringLiteral("已固定展开（点击取消）"));
    } else {
        m_pinButton->setToolTip(QStringLiteral("固定展开，鼠标离开后不自动收起"));
    }
}

void MainWindow::updateTrayState()
{
    if (m_trayInstalled)
        WinUtils::updateTrayTip(this, trayTipText());

    if (m_trayShowAction)
        m_trayShowAction->setEnabled(!isVisible() || isMinimized());
    if (m_trayHideAction)
        m_trayHideAction->setEnabled(isVisible() && !isMinimized() && WinUtils::trayAvailable());
    if (m_hideButton) {
        if (WinUtils::trayAvailable()) {
            m_hideButton->setToolTip(QStringLiteral("隐藏到托盘（程序继续运行）"));
        } else {
            // Linux stub / no tray: hide routes to save+quit.
            m_hideButton->setToolTip(QStringLiteral("系统托盘不可用：将保存并退出"));
        }
    }
    if (m_closeButton) {
        if (WinUtils::trayAvailable()) {
            m_closeButton->setToolTip(QStringLiteral("隐藏到托盘；真正退出请用托盘菜单"));
        } else {
            m_closeButton->setToolTip(QStringLiteral("系统托盘不可用：将保存并退出"));
        }
    }
    if (m_trayAutoStartAction) {
        QSignalBlocker blocker(m_trayAutoStartAction);
        m_trayAutoStartAction->setChecked(m_store && m_store->settings().autoStart);
    }
}

void MainWindow::applyTopMost()
{
    if (!m_store || m_applyingTopMost)
        return;
    m_applyingTopMost = true;
    m_lastTopMost = m_store->settings().alwaysOnTop;
    WinUtils::setTopMost(this, m_lastTopMost);
    m_applyingTopMost = false;
}

void MainWindow::applyAutoStart(bool enabled, bool quiet)
{
    if (!m_store)
        return;
    // 启动时若偏好已关闭，不再访问注册表：既没有需要清理的启动项，
    // 也避免受限环境下弹出无意义的权限错误。
    if (quiet && !enabled) {
        updateTrayState();
        if (m_settingsPage)
            m_settingsPage->refresh();
        return;
    }
    QString error;
    // Pin the Run key to this session's resolved data dir (portable config),
    // not whatever Temp path a smoke test last exported via --data-dir.
    const bool ok = WinUtils::setAutoStart(enabled, &error, m_store->dataDir());
    if (ok) {
        m_store->setAutoStartPreference(enabled);
        if (!quiet)
            showToast(enabled ? QStringLiteral("已开启开机自动启动")
                              : QStringLiteral("已关闭开机自动启动"));
    } else {
        const bool actual = WinUtils::autoStartEnabled();
        m_store->setAutoStartPreference(actual);
        if (!quiet)
            showToast(QStringLiteral("无法修改开机启动项：%1").arg(error), QString(), {}, 8000);
    }
    updateTrayState();
    if (m_settingsPage)
        m_settingsPage->refresh();
}

void MainWindow::scheduleMidnightRefresh()
{
    if (!m_dateTimer)
        return;
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime next(QDate::currentDate().addDays(1), QTime(0, 0, 5));
    const qint64 msecs = qBound<qint64>(1000, now.msecsTo(next),
                                       24LL * 60 * 60 * 1000);
    m_dateTimer->start(static_cast<int>(msecs));
}

void MainWindow::refreshDateIfNeeded()
{
    const QDate today = QDate::currentDate();
    if (today == m_currentDate)
        return;

    const QDate previous = m_currentDate;
    m_currentDate = today;
    if (m_composer && m_composer->text().isEmpty() && m_composerDate
        && m_composerDate->date() == previous) {
        m_composerDate->setDate(today);
        m_store->setDraftDue(today);
    }
    refreshTodos();
}

void MainWindow::refreshTodos()
{
    if (!m_todosView || !m_store)
        return;
    m_currentDate = QDate::currentDate();
    const QVector<TodoGroup> groups =
        buildGroups(m_store->todos(), m_currentDate, m_searchQuery);
    m_searchBox->setEnabled(true);
    m_todosView->refresh(groups, m_searchQuery);
    updateHeader();
    updateTrayState();
    reapplyDockGeometry();
    QTimer::singleShot(0, this, [this] {
        reapplyDockGeometry();
        // 新增待办会先停掉收起计时，同时输入框仍保持焦点。
        // 若这时光标已经在窗口外，离开事件不会再响，要在列表重建后再判一次。
        if (!pointerInsideWindow())
            startCollapseCountdown(450);
    });
}

bool MainWindow::pointerInsideWindow() const
{
    if (!isVisible())
        return false;
    return geometry().contains(QCursor::pos());
}

bool MainWindow::canAutoCollapse() const
{
    if (!m_store || !isDocked() || m_collapsed || m_pinned || m_settingsVisible)
        return false;
    if (!m_store->settings().autoHide && !m_peekExpanded)
        return false;
    if (m_calendar && m_calendar->isVisible())
        return false;
    // 只有正在输入的内容才挡住收起。提交后的空输入框仍有焦点，
    // 不能因此让鼠标已经离开的便签一直摊开。
    if (m_composer && m_composer->hasFocus()
        && !m_composer->text().trimmed().isEmpty())
        return false;
    if (m_searchBox && m_searchBox->lineEdit() && m_searchBox->lineEdit()->hasFocus()
        && !m_searchBox->text().trimmed().isEmpty())
        return false;
    if (QWidget *focus = QApplication::focusWidget()) {
        // focusWidget() 会记住上一次点过的行内编辑框，必须确认它此刻真的有焦点。
        if (qobject_cast<QLineEdit *>(focus)
            && m_todosView && m_todosView->isAncestorOf(focus))
            return false;
    }
    if (m_snackbar && m_snackbar->isVisible())
        return false;
    return true;
}

void MainWindow::startCollapseCountdown(int delayMs)
{
    if (!m_collapseTimer || !canAutoCollapse())
        return;
    // 光标还在便签上就保持展开。用光标坐标，不用 underMouse()：
    // 删除并重建待办行之后，那个悬停标记会一直为真，离开事件也不再到来。
    if (pointerInsideWindow())
        return;
    m_collapseTimer->start(qMax(0, delayMs));
}

void MainWindow::stopCollapseCountdown()
{
    if (m_collapseTimer)
        m_collapseTimer->stop();
    m_forceDockCollapse = false;
}

void MainWindow::collapseNow(bool animate, bool persist)
{
    if (m_collapsed || m_animating || !isDocked() || !isVisible())
        return;
    if (m_settingsVisible)
        closeSettingsPage();

    stopCollapseCountdown();
    if (m_hoverTimer)
        m_hoverTimer->stop();
    if (m_snackbar)
        m_snackbar->dismiss();
    if (m_composer)
        m_composer->clearFocus();

    m_expandedWindowRect = geometry();
    m_peekExpanded = false;
    const QRect target = collapsedRect();
    const QRect start = geometry();

    auto finish = [this, target, persist] {
        m_animating = false;
        m_collapsed = true;
        m_pinned = false;
        m_forceDockCollapse = false;
        setGeometry(target);
        applyCardGeometry();
        // 如果指针正停在刚出现的标签上，先不要自动展开；
        // 等指针离开再进入时再触发悬停展开，避免收起后立刻弹开。
        m_suppressHover = target.contains(QCursor::pos());
        updateHeader();
        updatePinButton();
        updateTrayState();
        if (persist && m_store) {
            m_store->setCollapsed(true);
            m_store->setPinned(false);
            saveWindowState();
        }
    };

    if (!animate) {
        finish();
        return;
    }

    m_animating = true;
    auto *animation = new QPropertyAnimation(this, "geometry", this);
    animation->setDuration(200);
    animation->setStartValue(start);
    animation->setEndValue(target);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QPropertyAnimation::finished, this, [animation, finish] {
        finish();
        animation->deleteLater();
    });
    animation->start();
}

void MainWindow::expandFromTab(bool pin)
{
    if (!m_collapsed || m_animating || !isDocked())
        return;

    stopCollapseCountdown();
    if (m_composer)
        m_composer->clearFocus();
    QScreen *screen = screenForWindow();
    if (!screen)
        return;

    QSize size = m_expandedWindowRect.size();
    if (!size.isValid())
        size = QSize(kDefaultWidth, kDefaultHeight);
    const QRect target = dockedRectFor(size, screen, m_dockSide);
    const QRect start = geometry();

    m_collapsed = false;
    m_peekExpanded = !pin;
    m_pinned = pin;
    m_suppressHover = false;
    m_animating = true;

    // 只把纸卡按目标尺寸排版，窗口本身仍停在收起状态再开始动画。
    // 这样不会先闪现一次完整窗口，展开过程从第一帧就是连续的。
    if (m_tab)
        m_tab->hide();
    if (m_card) {
        m_card->setGeometry(kShadow, kShadow,
                            qMax(1, target.width() - 2 * kShadow),
                            qMax(1, target.height() - 2 * kShadow));
        m_card->show();
        m_card->raise();
    }
    placeSnackbarOnCard(m_card, m_composerBar, m_snackbar);
    if (m_resizeGrip && m_card) {
        m_resizeGrip->move(m_card->width() - 24, m_card->height() - 24);
    }
    setGeometry(start);

    auto *animation = new QPropertyAnimation(this, "geometry", this);
    animation->setDuration(200);
    animation->setStartValue(start);
    animation->setEndValue(target);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QPropertyAnimation::finished, this,
            [this, animation, target, pin] {
                m_animating = false;
                setGeometry(target);
                m_expandedWindowRect = target;
                applyCardGeometry();
                updateHeader();
                updatePinButton();
                updateTrayState();
                if (pin && m_store) {
                    m_store->setCollapsed(false);
                    m_store->setPinned(true);
                    saveWindowState();
                    WinUtils::activateWindow(this);
                }
                // A hover-expanded tab is kept open until the pointer leaves
                // the note; leaveEvent() starts the collapse countdown.
                animation->deleteLater();
            });
    animation->start();
}

void MainWindow::expandForInteraction()
{
    if (m_collapsed)
        expandFromTab(false);
    else
        stopCollapseCountdown();
}

void MainWindow::undockForDrag()
{
    if (!isDocked())
        return;
    QScreen *screen = screenForWindow();
    if (!screen)
        return;
    const QRect avail = screen->availableGeometry();
    QSize size = m_expandedWindowRect.size();
    if (!size.isValid())
        size = QSize(kDefaultWidth, kDefaultHeight);
    QRect target = geometry();
    target.setSize(size);
    target.moveLeft(m_dockSide == 1 ? avail.left() : avail.right() - size.width() + 1);
    target.moveTop(qBound(avail.top(), target.top(), avail.bottom() - size.height() + 1));

    m_dockSide = 0;
    m_pinned = false;
    m_peekExpanded = false;
    m_collapsed = false;
    m_animating = false;
    setGeometry(target);
    m_expandedWindowRect = target;
    applyCardGeometry();
    updateHeader();
    updatePinButton();
}

void MainWindow::finishDrag()
{
    m_dragActive = false;
    m_dragMoved = false;
    m_titleBlock->setCursor(Qt::OpenHandCursor);
    if (!isVisible())
        return;

    QScreen *screen = nearestScreen(frameGeometry().center());
    if (!screen)
        screen = nearestScreen(QCursor::pos());
    if (!screen)
        screen = screenForWindow();
    if (!screen)
        return;
    // Persist the snap screen before animate/save so dock reapply cannot
    // pull the window back onto a stale m_screenName monitor.
    m_screenName = screen->name();
    const QRect avail = screen->availableGeometry();
    const QRect current = geometry();
    const int leftGap = qAbs(current.left() - avail.left());
    const int rightGap = qAbs(current.right() - avail.right());
    const bool snapLeft = leftGap <= 40 && leftGap <= rightGap;
    const bool snapRight = rightGap <= 40 && rightGap < leftGap;

    if (snapLeft || snapRight) {
        const int side = snapLeft ? 1 : 2;
        m_dockSide = side;
        m_pinned = false;
        m_peekExpanded = false;
        m_expandedWindowRect = current;
        const QRect target = dockedRectFor(current.size(), screen, side);
        m_animating = true;
        auto *animation = new QPropertyAnimation(this, "geometry", this);
        animation->setDuration(160);
        animation->setStartValue(current);
        animation->setEndValue(target);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QPropertyAnimation::finished, this,
                [this, animation, target] {
                    m_animating = false;
                    setGeometry(target);
                    m_expandedWindowRect = target;
                    applyCardGeometry();
                    updateHeader();
                    updatePinButton();
                    saveWindowState();
                    if (m_store->settings().autoHide && canAutoCollapse()) {
                        // 拖拽吸附后，即使指针还停在便签上，也在短暂停留后
                        // 真正收起；若指针正好落在标签上，会先抑制悬停展开。
                        m_forceDockCollapse = true;
                        m_collapseTimer->start(650);
                    }
                    animation->deleteLater();
                });
        animation->start();
        return;
    }

    m_dockSide = 0;
    m_pinned = false;
    m_peekExpanded = false;
    QRect target = current;
    target.moveLeft(qBound(avail.left(), target.left(),
                           qMax(avail.left(), avail.right() - target.width() + 1)));
    target.moveTop(qBound(avail.top(), target.top(),
                          qMax(avail.top(), avail.bottom() - target.height() + 1)));
    setGeometry(target);
    m_expandedWindowRect = target;
    applyCardGeometry();
    updateHeader();
    updatePinButton();
    saveWindowState();
}

void MainWindow::finishResize()
{
    m_resizing = false;
    QScreen *screen = screenForWindow();
    QRect target = geometry();
    if (screen) {
        const QRect avail = screen->availableGeometry();
        QSize size = target.size();
        size.setWidth(qBound(kMinWidth, size.width(), qMax(kMinWidth, avail.width())));
        size.setHeight(qBound(kMinHeight, size.height(), qMax(kMinHeight, avail.height())));
        target.setSize(size);
        target.moveLeft(qBound(avail.left(), target.left(),
                               qMax(avail.left(), avail.right() - size.width() + 1)));
        target.moveTop(qBound(avail.top(), target.top(),
                              qMax(avail.top(), avail.bottom() - size.height() + 1)));
        if (isDocked())
            target = dockedRectFor(size, screen, m_dockSide);
    }
    setGeometry(target);
    m_expandedWindowRect = target;
    applyCardGeometry();
    saveWindowState();
}

void MainWindow::openSettingsPage()
{
    if (m_composer)
        m_composer->clearFocus();
    if (m_settingsVisible)
        return;
    m_dailyVisible = false;
    m_weeklyVisible = false;
    m_monthlyVisible = false;
    if (m_collapsed)
        expandFromTab(false);
    stopCollapseCountdown();
    m_settingsVisible = true;
    m_stack->setCurrentWidget(m_settingsPage);
    m_settingsPage->refresh();
    if (m_composerBar && m_composerBar->parentWidget())
        m_composerBar->parentWidget()->hide();
    if (m_snackbar)
        m_snackbar->dismiss();
    updateHeader();
    m_settingsPage->setFocus(Qt::OtherFocusReason);
}

void MainWindow::closeSettingsPage()
{
    closeOverlayPage();
}

bool MainWindow::reportOverlayVisible() const
{
    return m_dailyVisible || m_weeklyVisible || m_monthlyVisible;
}

void MainWindow::updateReportButtons()
{
    const bool overlay = m_settingsVisible || reportOverlayVisible();
    const AppSettings &settings = m_store ? m_store->settings() : AppSettings();
    if (m_dailyButton)
        m_dailyButton->setVisible(!overlay && settings.showDailyReport);
    if (m_weeklyButton)
        m_weeklyButton->setVisible(!overlay && settings.showWeeklyReport);
    if (m_monthlyButton)
        m_monthlyButton->setVisible(!overlay && settings.showMonthlyReport);
}

void MainWindow::openDailyPage()
{
    openReportPage(ReportKind::Daily);
}

void MainWindow::openWeeklyPage()
{
    openReportPage(ReportKind::Weekly);
}

void MainWindow::openMonthlyPage()
{
    openReportPage(ReportKind::Monthly);
}

void MainWindow::openReportPage(ReportKind kind)
{
    if (m_composer)
        m_composer->clearFocus();
    DailyReportPage *page = nullptr;
    bool *flag = nullptr;
    switch (kind) {
    case ReportKind::Daily:
        page = m_dailyPage;
        flag = &m_dailyVisible;
        break;
    case ReportKind::Weekly:
        page = m_weeklyPage;
        flag = &m_weeklyVisible;
        break;
    case ReportKind::Monthly:
        page = m_monthlyPage;
        flag = &m_monthlyVisible;
        break;
    }
    if (!page || !flag)
        return;
    if (*flag)
        return;
    m_settingsVisible = false;
    m_dailyVisible = false;
    m_weeklyVisible = false;
    m_monthlyVisible = false;
    if (m_collapsed)
        expandFromTab(false);
    stopCollapseCountdown();
    *flag = true;
    m_stack->setCurrentWidget(page);
    page->refresh();
    if (m_composerBar && m_composerBar->parentWidget())
        m_composerBar->parentWidget()->hide();
    if (m_snackbar)
        m_snackbar->dismiss();
    updateHeader();
    page->setFocus(Qt::OtherFocusReason);
}

void MainWindow::closeOverlayPage()
{
    if (!m_settingsVisible && !reportOverlayVisible())
        return;
    m_settingsVisible = false;
    m_dailyVisible = false;
    m_weeklyVisible = false;
    m_monthlyVisible = false;
    m_stack->setCurrentWidget(m_todosView);
    if (m_composerBar && m_composerBar->parentWidget())
        m_composerBar->parentWidget()->show();
    if (m_todosView)
        m_todosView->setFocus(Qt::OtherFocusReason);
    updateHeader();
    refreshTodos();
    if (isDocked() && !m_pinned && m_store->settings().autoHide
        && !pointerInsideWindow()) {
        startCollapseCountdown(600);
    }
}

void MainWindow::togglePin()
{
    if (!isDocked()) {
        // The pin is visible while floating so the user can discover docking
        // without opening settings. First click docks to the nearest edge and
        // keeps the note expanded.
        QScreen *screen = screenForWindow();
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
        const QRect avail = screen->availableGeometry();
        const int side = geometry().center().x() < avail.center().x() ? 1 : 2;
        handleDockRequest(side);
        m_pinned = true;
        m_peekExpanded = false;
        m_store->setPinned(true);
        stopCollapseCountdown();
        updateHeader();
        saveWindowState();
        if (m_settingsPage)
            m_settingsPage->refresh();
        return;
    }
    m_pinned = !m_pinned;
    m_peekExpanded = false;
    m_store->setPinned(m_pinned);
    updatePinButton();
    updateHeader();
    if (m_pinned)
        stopCollapseCountdown();
    else
        startCollapseCountdown(650);
    saveWindowState();
}

void MainWindow::handleDockRequest(int side)
{
    if (side < 0 || side > 2)
        return;
    stopCollapseCountdown();
    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = screenForWindow();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    m_screenName = screen->name();

    QSize size = m_expandedWindowRect.size();
    if (!size.isValid())
        size = QSize(kDefaultWidth, kDefaultHeight);
    const QRect avail = screen->availableGeometry();

    if (side == 0) {
        m_collapsed = false;
        m_dockSide = 0;
        m_pinned = false;
        m_peekExpanded = false;
        size.setWidth(qBound(kMinWidth, size.width(), avail.width()));
        size.setHeight(qBound(kMinHeight, size.height(), avail.height()));
        QPoint topLeft(avail.center().x() - size.width() / 2,
                       avail.center().y() - size.height() / 2);
        topLeft.setX(qBound(avail.left(), topLeft.x(),
                            qMax(avail.left(), avail.right() - size.width() + 1)));
        topLeft.setY(qBound(avail.top(), topLeft.y(),
                            qMax(avail.top(), avail.bottom() - size.height() + 1)));
        setGeometry(QRect(topLeft, size));
        m_expandedWindowRect = geometry();
        applyCardGeometry();
    } else {
        m_collapsed = false;
        m_dockSide = side;
        m_pinned = false;
        m_peekExpanded = false;
        const QRect target = dockedRectFor(size, screen, side);
        setGeometry(target);
        m_expandedWindowRect = target;
        applyCardGeometry();
    }

    updateHeader();
    updatePinButton();
    saveWindowState();
    if (m_settingsPage)
        m_settingsPage->refresh();
    if (isDocked() && m_store->settings().autoHide && !m_settingsVisible)
        startCollapseCountdown(650);
}

void MainWindow::handleCollapseAction()
{
    if (m_collapsed)
        return;
    if (m_settingsVisible)
        closeSettingsPage();

    if (!isDocked()) {
        QScreen *screen = screenForWindow();
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
        const QRect avail = screen->availableGeometry();
        const int leftDistance = qAbs(geometry().center().x() - avail.left());
        const int rightDistance = qAbs(avail.right() - geometry().center().x());
        m_dockSide = leftDistance < rightDistance ? 1 : 2;
        QSize size = m_expandedWindowRect.size().isValid()
                         ? m_expandedWindowRect.size()
                         : QSize(kDefaultWidth, kDefaultHeight);
        const QRect target = dockedRectFor(size, screen, m_dockSide);
        setGeometry(target);
        m_expandedWindowRect = target;
        applyCardGeometry();
    }
    m_pinned = false;
    m_store->setPinned(false);
    collapseNow(true, true);
}

void MainWindow::resetWindowPosition()
{
    stopCollapseCountdown();
    m_collapsed = false;
    m_dockSide = 0;
    m_pinned = false;
    m_peekExpanded = false;
    m_animating = false;
    QScreen *screen = screenForWindow();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    QRect target = defaultExpandedRect(screen);
    setGeometry(target);
    m_expandedWindowRect = target;
    m_screenName = screen ? screen->name() : QString();
    applyCardGeometry();
    updateHeader();
    updatePinButton();
    saveWindowState();
    if (m_settingsPage)
        m_settingsPage->refresh();
}

void MainWindow::showAndActivate()
{
    if (isMinimized())
        showNormal();
    if (m_collapsed)
        expandFromTab(false);
    else
        stopCollapseCountdown();
    if (!isVisible())
        show();
    if (m_todosView)
        m_todosView->setFocus(Qt::OtherFocusReason);
    raise();
    WinUtils::activateWindow(this);
    updateTrayState();
}

void MainWindow::minimizeToTray()
{
    // No taskbar button exists in this mode, so "minimize" always means the
    // tray. hideToTray() flushes data and shows the one-time recovery hint.
    hideToTray();
}

void MainWindow::hideToTray()
{
    if (!WinUtils::trayAvailable()) {
        // No system tray (Linux stub / explorer down): cannot recover from a
        // background hide. Treat as save+quit so the first dismiss exits.
        quitApplication();
        return;
    }
    if (!m_trayInstalled)
        m_trayInstalled = WinUtils::installTrayIcon(this, trayImage(), trayTipText());
    if (!m_trayInstalled) {
        // Tray shell exists but icon install failed (e.g. low integrity on Win).
        // No tray menu "退出" path — save+quit so X/— never strand the user.
        // Aligns with button tooltips: 系统托盘不可用：将保存并退出.
        quitApplication();
        return;
    }
    if (!m_store->flush()) {
        showAndActivate();
        showToast(QStringLiteral("数据未能保存，已取消隐藏。"), QString(), {}, 8000);
        return;
    }
    if (m_calendar) {
        CalendarPopup *calendar = m_calendar;
        m_calendar = nullptr;
        calendar->hide();
        calendar->deleteLater();
    }
    stopCollapseCountdown();
    if (m_hoverTimer)
        m_hoverTimer->stop();
    hide();
    updateTrayState();
    if (!m_firstHideHintShown) {
        WinUtils::showTrayBalloon(this, QStringLiteral("Sticky Todo"),
                                  QStringLiteral("便签仍在后台运行，可从托盘图标重新打开。"));
        m_firstHideHintShown = true;
    }
}

bool MainWindow::isOsForeground() const
{
    return WinUtils::isOsForeground(const_cast<MainWindow *>(this));
}

void MainWindow::toggleByHotkey()
{
    // Visible + OS foreground → hide; otherwise show/focus (including collapsed tab).
    // Win32: use GetForegroundWindow — isActiveWindow() can stay true while another
    // process owns the real foreground, which wrongly hid a still-visible note.
    const bool shouldHide = isVisible() && !isMinimized() && isOsForeground() && !m_collapsed;
    if (!shouldHide) {
        showAndActivate();
        return;
    }
    if (WinUtils::trayAvailable()) {
        hideToTray();
        return;
    }
    // No tray (Linux stub): hotkey is the recovery path — hide without quitting.
    // (Tray hide path still quits when tray is unavailable; quit path unchanged.)
    if (!m_store->flush()) {
        showAndActivate();
        showToast(QStringLiteral("数据未能保存，已取消隐藏。"), QString(), {}, 8000);
        return;
    }
    if (m_calendar) {
        CalendarPopup *calendar = m_calendar;
        m_calendar = nullptr;
        calendar->hide();
        calendar->deleteLater();
    }
    stopCollapseCountdown();
    if (m_hoverTimer)
        m_hoverTimer->stop();
    hide();
    updateTrayState();
}

void MainWindow::applyGlobalHotkey(bool quiet)
{
    if (!m_store)
        return;
    const QString raw = m_store->settings().globalHotkey;
    const QString normalized = WinUtils::normalizeGlobalHotkey(raw);
    if (!raw.trimmed().isEmpty() && normalized.isEmpty()) {
        if (!quiet)
            showToast(QStringLiteral("快捷键无效，已清除"));
        m_store->setGlobalHotkey(QString());
        WinUtils::clearGlobalHotkey();
        return;
    }
    if (normalized != raw && !raw.trimmed().isEmpty())
        m_store->setGlobalHotkey(normalized);

    QString error;
    if (!WinUtils::setGlobalHotkey(this, normalized, &error)) {
        if (!quiet)
            showToast(error.isEmpty() ? QStringLiteral("全局热键注册失败") : error, QString(), {}, 7000);
        // Keep the preference so user can retry / see it in settings; just unregister.
        WinUtils::clearGlobalHotkey();
        return;
    }
    if (!quiet && !normalized.isEmpty())
        showToast(QStringLiteral("全局热键已设为 %1").arg(normalized));
    else if (!quiet && normalized.isEmpty())
        showToast(QStringLiteral("已关闭全局热键"));
}

void MainWindow::finishQuit()
{
    if (m_quitting)
        return;
    m_quitting = true;
    WinUtils::clearGlobalHotkey();
    stopCollapseCountdown();
    if (m_hoverTimer)
        m_hoverTimer->stop();
    if (m_dateTimer)
        m_dateTimer->stop();
    if (m_calendar) {
        CalendarPopup *calendar = m_calendar;
        m_calendar = nullptr;
        calendar->hide();
        calendar->deleteLater();
    }
    WinUtils::removeTrayIcon(this);
    m_trayInstalled = false;
    // Hide before quit so platforms with QuitOnLastWindowClosed=false still
    // drop the visible tool window in the same click that requested exit.
    hide();
    qApp->quit();
}

void MainWindow::quitApplication()
{
    if (m_quitting)
        return;
    if (!m_store->flush()) {
        showAndActivate();
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("保存失败"));
        box.setIcon(QMessageBox::Warning);
        box.setMinimumWidth(430);
        box.setText(QStringLiteral("待办或设置未能保存。"));
        box.setInformativeText(
            QStringLiteral("程序同级的 config 目录不可写，或磁盘空间不足。\n"
                           "可以修复目录权限后重试；现在退出会丢失本次未保存的修改。"));
        QPushButton *retry = box.addButton(QStringLiteral("重试"), QMessageBox::AcceptRole);
        QPushButton *discard =
            box.addButton(QStringLiteral("放弃更改并退出"), QMessageBox::DestructiveRole);
        box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        box.setDefaultButton(retry);
        box.exec();
        if (box.clickedButton() == retry) {
            quitApplication();
        } else if (box.clickedButton() == discard) {
            finishQuit();
        }
        return;
    }
    finishQuit();
}

void MainWindow::showToast(const QString &message, const QString &actionText,
                           std::function<void()> action, int timeoutMs)
{
    if (!m_snackbar)
        return;
    stopCollapseCountdown();
    placeSnackbarOnCard(m_card, m_composerBar, m_snackbar);
    m_snackbar->showMessage(message, actionText, std::move(action), timeoutMs);
    m_snackbar->raise();
}

void MainWindow::openCalendarForTodo(const QString &id, const QPoint &globalAnchor)
{
    const TodoItem *item = m_store->findTodo(id);
    if (!item)
        return;
    if (m_calendar) {
        CalendarPopup *old = m_calendar;
        m_calendar = nullptr;
        old->hide();
        old->deleteLater();
    }
    stopCollapseCountdown();
    auto *popup = new CalendarPopup(this);
    m_calendar = popup;
    connect(popup, &QObject::destroyed, this, [this, popup] {
        if (m_calendar == popup)
            m_calendar = nullptr;
    });
    popup->installEventFilter(this);
    popup->openFor(item->dueDate, globalAnchor,
                   [this, id](const QDate &date) {
                       if (m_store)
                           m_store->updateDue(id, date);
                   },
                   [this, id] {
                       if (m_store)
                           m_store->updateDue(id, QDate());
                   });
}

void MainWindow::openCalendarForComposer()
{
    if (m_settingsVisible)
        return;
    if (m_collapsed)
        expandForInteraction();
    if (m_calendar) {
        CalendarPopup *old = m_calendar;
        m_calendar = nullptr;
        old->hide();
        old->deleteLater();
    }
    stopCollapseCountdown();
    auto *popup = new CalendarPopup(this);
    m_calendar = popup;
    connect(popup, &QObject::destroyed, this, [this, popup] {
        if (m_calendar == popup)
            m_calendar = nullptr;
    });
    popup->installEventFilter(this);
    const QPoint anchor = m_composerDate->mapToGlobal(
        QPoint(m_composerDate->width() / 2, m_composerDate->height()));
    popup->openFor(m_composerDate->date(), anchor,
                   [this](const QDate &date) {
                       m_composerDate->setDate(date);
                       m_store->setDraftDue(date);
                   },
                   [this] {
                       m_composerDate->setDate(QDate());
                       m_store->setDraftDue(QDate());
                   });
}

void MainWindow::submitComposer()
{
    if (!m_composer)
        return;
    const QString text = m_composer->text().trimmed();
    if (text.isEmpty()) {
        focusComposer();
        return;
    }
    if (m_settingsVisible)
        closeSettingsPage();
    const QDate due = m_composerDate->date();
    const QString id = m_store->addTodo(text, due);
    if (id.isEmpty()) {
        showToast(QStringLiteral("这条待办未能保存，请检查数据目录。"));
        return;
    }
    m_composer->clear();
    m_composerDate->setDate(QDate::currentDate());
    m_store->setDraft(QString());
    m_store->setDraftDue(QDate::currentDate());
    stopCollapseCountdown();
    focusComposer();
}

void MainWindow::focusComposer()
{
    if (m_settingsVisible)
        closeSettingsPage();
    if (m_collapsed)
        expandForInteraction();
    if (!isVisible())
        show();
    stopCollapseCountdown();
    if (m_composer) {
        m_composer->setFocus(Qt::ShortcutFocusReason);
        m_composer->setCursorPosition(m_composer->text().size());
    }
}

void MainWindow::focusSearch()
{
    if (m_settingsVisible)
        closeSettingsPage();
    if (m_collapsed)
        expandForInteraction();
    if (!isVisible())
        show();
    stopCollapseCountdown();
    if (m_searchBox) {
        m_searchBox->setFocus(Qt::ShortcutFocusReason);
        m_searchBox->lineEdit()->selectAll();
    }
}

void MainWindow::handleDelete(const QString &id)
{
    TodoItem removed;
    int index = -1;
    if (!m_store->removeTodo(id, &removed, &index))
        return;
    const QString label = elidedOneLine(removed.text, 30, Theme::uiFont(12));
    showToast(QStringLiteral("已删除“%1”").arg(label), QStringLiteral("撤销"),
              [this, removed, index] {
                  if (m_store->restoreTodo(removed, index))
                      showToast(QStringLiteral("已恢复待办"));
                  else
                      showToast(QStringLiteral("恢复失败，原条目可能已存在。"));
              },
              7000);
}

void MainWindow::handleCompletion(const QString &id, bool completed)
{
    m_store->setCompleted(id, completed);
    if (completed) {
        if (m_dailyVisible && m_dailyPage)
            m_dailyPage->refresh();
        if (m_weeklyVisible && m_weeklyPage)
            m_weeklyPage->refresh();
        if (m_monthlyVisible && m_monthlyPage)
            m_monthlyPage->refresh();
    }
}

void MainWindow::handleTextEdited(const QString &id, const QString &text)
{
    if (!text.trimmed().isEmpty())
        m_store->updateText(id, text);
}

void MainWindow::handleGroupExpansion(GroupId id, bool expanded)
{
    if (!m_searchQuery.isEmpty())
        return;
    m_store->setGroupExpanded(id, expanded);
}

void MainWindow::handleSearchChanged(const QString &text)
{
    m_searchQuery = text.trimmed();
    if (m_searchQuery.isEmpty()) {
        if (m_searchTimer)
            m_searchTimer->stop();
        runSearch();
        return;
    }
    if (m_searchTimer)
        m_searchTimer->start(80);
}

void MainWindow::runSearch()
{
    if (!m_searchBox)
        return;
    m_searchQuery = m_searchBox->text().trimmed();
    refreshTodos();
}
