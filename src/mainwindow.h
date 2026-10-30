#pragma once

#include "models.h"

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QDate>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <functional>

class CalendarPopup;
class CircleAddButton;
class DataStore;
class DateChip;
class IconButton;
class QAction;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QScreen;
class QStackedWidget;
class QTimer;
class SearchBox;
class SettingsPage;
class Snackbar;
class TodoScrollView;
class QWidget;

class MainWindow : public QWidget, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void buildTray();
    void restoreWindowState();
    void installShortcuts();
    void scheduleMidnightRefresh();
    void refreshDateIfNeeded();
    void refreshTodos();
    void updateHeader();
    void updatePinButton();
    void updateTrayState();
    void applyTopMost();
    void applyAutoStart(bool enabled, bool quiet);

    void saveWindowState();
    void applyCardGeometry();
    void reapplyDockGeometry();
    void ensureTrayIcon();
    QImage trayImage() const;
    QString trayTipText() const;
    QScreen *screenForWindow() const;
    QScreen *screenByName(const QString &name) const;
    QRect defaultExpandedRect(QScreen *screen) const;
    QRect dockedRectFor(const QSize &size, QScreen *screen, int side) const;
    QRect collapsedRect() const;
    void ensureVisible();

    bool isDocked() const { return m_dockSide == 1 || m_dockSide == 2; }
    bool pointerInsideWindow() const;
    bool canAutoCollapse() const;
    void startCollapseCountdown(int delayMs);
    void stopCollapseCountdown();
    void collapseNow(bool animate, bool persist = true);
    void expandFromTab(bool pin);
    void expandForInteraction();
    void undockForDrag();
    void finishDrag();
    void finishResize();

    void openSettingsPage();
    void closeSettingsPage();
    void togglePin();
    void handleDockRequest(int side);
    void handleCollapseAction();
    void resetWindowPosition();

    void showAndActivate();
    void minimizeToTray();
    void hideToTray();
    void quitApplication();
    void finishQuit();
    void showToast(const QString &message, const QString &actionText = QString(),
                   std::function<void()> action = {}, int timeoutMs = 5000);

    void openCalendarForTodo(const QString &id, const QPoint &globalAnchor);
    void openCalendarForComposer();

    void submitComposer();
    void focusComposer();
    void focusSearch();
    void handleDelete(const QString &id);
    void handleCompletion(const QString &id, bool completed);
    void handleTextEdited(const QString &id, const QString &text);
    void handleGroupExpansion(GroupId id, bool expanded);
    void handleSearchChanged(const QString &text);
    void runSearch();

    DataStore *m_store = nullptr;
    QWidget *m_card = nullptr;
    QWidget *m_header = nullptr;
    QWidget *m_titleBlock = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    IconButton *m_backButton = nullptr;
    IconButton *m_pinButton = nullptr;
    IconButton *m_settingsButton = nullptr;
    IconButton *m_hideButton = nullptr;
    IconButton *m_closeButton = nullptr;
    SearchBox *m_searchBox = nullptr;
    QStackedWidget *m_stack = nullptr;
    TodoScrollView *m_todosView = nullptr;
    SettingsPage *m_settingsPage = nullptr;
    QWidget *m_composerBar = nullptr;
    QLineEdit *m_composer = nullptr;
    DateChip *m_composerDate = nullptr;
    CircleAddButton *m_addButton = nullptr;
    Snackbar *m_snackbar = nullptr;
    QWidget *m_resizeGrip = nullptr;
    QWidget *m_tab = nullptr;

    QMenu *m_trayMenu = nullptr;
    QAction *m_trayAutoStartAction = nullptr;
    QAction *m_trayShowAction = nullptr;
    QAction *m_trayHideAction = nullptr;

    CalendarPopup *m_calendar = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QTimer *m_searchTimer = nullptr;
    QTimer *m_collapseTimer = nullptr;
    QTimer *m_hoverTimer = nullptr;
    QTimer *m_dateTimer = nullptr;

    QDate m_currentDate;
    QString m_searchQuery;
    QRect m_expandedWindowRect;
    QString m_screenName;
    int m_dockSide = 0;
    bool m_collapsed = false;
    bool m_pinned = false;
    bool m_animating = false;
    bool m_peekExpanded = false;
    bool m_settingsVisible = false;
    bool m_quitting = false;
    bool m_forceDockCollapse = false;
    bool m_suppressHover = false;
    bool m_firstHideHintShown = false;
    bool m_startupNoticesPending = false;
    bool m_applyingTopMost = false;
    bool m_lastTopMost = true;
    bool m_trayInstalled = false;
    bool m_restoringDock = false;
    int m_trayAttempts = 0;

    bool m_dragActive = false;
    bool m_dragMoved = false;
    QPoint m_dragOffset;
    QPoint m_dragStartGlobal;

    bool m_tabDragActive = false;
    bool m_tabDragMoved = false;
    QPoint m_tabDragOffset;

    bool m_resizing = false;
    QSize m_resizeStartSize;
    QPoint m_resizeStartGlobal;
    QPoint m_resizeStartPos;
};
