#include "calendarpopup.h"

#include "icons.h"
#include "theme.h"
#include "widgets.h"

#include <QDate>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

class DayCell : public QAbstractButton
{
public:
    explicit DayCell(QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(34, 30);
        setAttribute(Qt::WA_Hover, true);
    }

    void setState(const QDate &date, bool inMonth, bool selected, bool today)
    {
        m_date = date;
        m_inMonth = inMonth;
        m_selected = selected;
        m_today = today;
        setToolTip(QStringLiteral("%1年%2月%3日").arg(date.year()).arg(date.month()).arg(date.day()));
        update();
    }

    QDate date() const { return m_date; }

    QSize sizeHint() const override { return QSize(34, 30); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF cell = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);

        if (m_selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Theme::Accent);
            painter.drawRoundedRect(cell, 7, 7);
        } else if (m_hover) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 120));
            painter.drawRoundedRect(cell, 7, 7);
        }

        if (m_today && !m_selected) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(133, 146, 103, 190), 1.0));
            painter.drawRoundedRect(cell.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        }

        QFont font = Theme::uiFont(12, m_today ? QFont::DemiBold : QFont::Normal);
        painter.setFont(font);
        QColor textColor = m_inMonth ? Theme::Ink : QColor(190, 182, 160);
        if (m_selected)
            textColor = QColor(255, 255, 246);
        else if (m_today)
            textColor = Theme::AccentDark;
        painter.setPen(textColor);
        painter.drawText(rect(), Qt::AlignCenter, QString::number(m_date.day()));
    }

    void enterEvent(QEnterEvent *event) override
    {
        QAbstractButton::enterEvent(event);
        m_hover = true;
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        QAbstractButton::leaveEvent(event);
        m_hover = false;
        update();
    }

private:
    QDate m_date;
    bool m_inMonth = true;
    bool m_selected = false;
    bool m_today = false;
    bool m_hover = false;
};

QColor dayHeaderColor(bool weekend)
{
    return weekend ? QColor(169, 153, 128) : Theme::Muted;
}

} // namespace

CalendarPopup::CalendarPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(288, 342);
    buildUi();
}

void CalendarPopup::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(7);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(4);

    auto *prev = new IconButton(AppIcon::ArrowLeft, 26, 14, this);
    prev->setIconColor(Theme::InkSoft);
    prev->setToolTip(QStringLiteral("上个月（PageUp）"));
    connect(prev, &QAbstractButton::clicked, this, [this] { shiftMonth(-1); });

    m_monthLabel = new QLabel(this);
    m_monthLabel->setAlignment(Qt::AlignCenter);
    m_monthLabel->setFont(Theme::uiFont(14, QFont::DemiBold));
    m_monthLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Ink.name()));

    auto *next = new IconButton(AppIcon::ArrowRight, 26, 14, this);
    next->setIconColor(Theme::InkSoft);
    next->setToolTip(QStringLiteral("下个月（PageDown）"));
    connect(next, &QAbstractButton::clicked, this, [this] { shiftMonth(1); });

    header->addWidget(prev, 0, Qt::AlignVCenter);
    header->addStretch(1);
    header->addWidget(m_monthLabel, 0, Qt::AlignVCenter);
    header->addStretch(1);
    header->addWidget(next, 0, Qt::AlignVCenter);
    root->addLayout(header);

    auto *weekdays = new QHBoxLayout;
    weekdays->setContentsMargins(0, 0, 0, 0);
    weekdays->setSpacing(2);
    static const char *names[] = {"一", "二", "三", "四", "五", "六", "日"};
    for (int i = 0; i < 7; ++i) {
        auto *label = new QLabel(QString::fromUtf8(names[i]), this);
        label->setAlignment(Qt::AlignCenter);
        label->setFixedSize(34, 16);
        label->setFont(Theme::uiFont(10, QFont::DemiBold));
        QColor color = dayHeaderColor(i >= 5);
        label->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(color.name()));
        weekdays->addWidget(label);
    }
    weekdays->addStretch(1);
    root->addLayout(weekdays);

    m_grid = new QGridLayout;
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(2);
    m_grid->setVerticalSpacing(2);
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 7; ++col) {
            auto *cell = new DayCell(this);
            connect(cell, &QAbstractButton::clicked, this, [this, cell] {
                chooseDate(cell->date());
            });
            m_cells.append(cell);
            m_grid->addWidget(cell, row, col);
        }
    }
    root->addLayout(m_grid);

    root->addSpacing(2);
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(3);
    auto addShortcut = [this, footer](const QString &text, int days, bool clear) {
        auto *action = new TextAction(text, this);
        action->setFont(Theme::uiFont(11));
        action->setColors(clear ? Theme::Muted : Theme::AccentDark,
                          clear ? Theme::InkSoft : Theme::Accent);
        action->setToolTip(clear ? QStringLiteral("不设置计划日期")
                                 : QStringLiteral("快速选择"));
        connect(action, &QAbstractButton::clicked, this, [this, days, clear] {
            if (clear) {
                chooseClear();
            } else if (days >= 0) {
                chooseDate(QDate::currentDate().addDays(days));
            } else {
                const QDate today = QDate::currentDate();
                int delta = (8 - today.dayOfWeek()) % 7;
                if (delta == 0)
                    delta = 7;
                chooseDate(today.addDays(delta));
            }
        });
        footer->addWidget(action);
    };
    addShortcut(QStringLiteral("今天"), 0, false);
    addShortcut(QStringLiteral("明天"), 1, false);
    addShortcut(QStringLiteral("下周一"), -1, false);
    addShortcut(QStringLiteral("无日期"), 0, true);
    footer->addStretch(1);
    root->addLayout(footer);
}

void CalendarPopup::openFor(const QDate &selected, const QPoint &globalAnchor,
                            std::function<void(const QDate &)> onDate,
                            std::function<void()> onClear)
{
    m_onDate = std::move(onDate);
    m_onClear = std::move(onClear);
    m_selected = selected.isValid() ? selected : QDate::currentDate();
    m_viewMonth = QDate(m_selected.year(), m_selected.month(), 1);
    refresh();

    adjustSize();
    const QSize popupSize = size();
    QScreen *screen = QGuiApplication::screenAt(globalAnchor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    int x = globalAnchor.x() - popupSize.width() / 2;
    int y = globalAnchor.y() + 6;
    if (y + popupSize.height() > available.bottom() + 1)
        y = globalAnchor.y() - popupSize.height() - 6;
    x = std::clamp(x, available.left() + 4, available.right() - popupSize.width() - 3);
    y = std::clamp(y, available.top() + 4, available.bottom() - popupSize.height() - 3);

    move(x, y);
    show();
    raise();
    activateWindow();
    setFocus(Qt::PopupFocusReason);
}

void CalendarPopup::refresh()
{
    const QDate today = QDate::currentDate();
    m_monthLabel->setText(QStringLiteral("%1 年 %2 月").arg(m_viewMonth.year()).arg(m_viewMonth.month()));

    const QDate first(m_viewMonth.year(), m_viewMonth.month(), 1);
    const int offset = (first.dayOfWeek() + 6) % 7;
    const QDate gridStart = first.addDays(-offset);
    for (int i = 0; i < m_cells.size(); ++i) {
        const QDate date = gridStart.addDays(i);
        static_cast<DayCell *>(m_cells.at(i))->setState(date, date.month() == m_viewMonth.month(),
                                                        date == m_selected, date == today);
    }
    update();
}

void CalendarPopup::shiftMonth(int delta)
{
    m_viewMonth = m_viewMonth.addMonths(delta);
    refresh();
}

void CalendarPopup::chooseDate(const QDate &date)
{
    if (!date.isValid())
        return;
    m_selected = date;
    refresh();
    auto callback = m_onDate;
    closePopup();
    if (callback)
        callback(date);
}

void CalendarPopup::chooseClear()
{
    auto callback = m_onClear;
    closePopup();
    if (callback)
        callback();
}

void CalendarPopup::closePopup()
{
    m_onDate = nullptr;
    m_onClear = nullptr;
    hide();
    deleteLater();
}

void CalendarPopup::keyPressEvent(QKeyEvent *event)
{
    QDate next = m_selected;
    switch (event->key()) {
    case Qt::Key_Left:
        next = m_selected.addDays(-1);
        break;
    case Qt::Key_Right:
        next = m_selected.addDays(1);
        break;
    case Qt::Key_Up:
        next = m_selected.addDays(-7);
        break;
    case Qt::Key_Down:
        next = m_selected.addDays(7);
        break;
    case Qt::Key_PageUp:
        shiftMonth(-1);
        event->accept();
        return;
    case Qt::Key_PageDown:
        shiftMonth(1);
        event->accept();
        return;
    case Qt::Key_Home:
        next = QDate::currentDate();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        chooseDate(m_selected);
        event->accept();
        return;
    case Qt::Key_Escape:
        closePopup();
        event->accept();
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    if (next.isValid()) {
        m_selected = next;
        m_viewMonth = QDate(next.year(), next.month(), 1);
        refresh();
    }
    event->accept();
}

void CalendarPopup::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(10, 9, -10, -9);
    for (int i = 8; i >= 1; --i) {
        const qreal spread = i * 1.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(76, 66, 42, 4 + (8 - i)));
        painter.drawRoundedRect(card.adjusted(-spread, -spread * 0.4, spread, spread + 3), 12 + i, 12 + i);
    }
    painter.setPen(QPen(QColor(217, 203, 166), 1.0));
    painter.setBrush(Theme::PaperTop);
    painter.drawRoundedRect(card, 12, 12);
}

void CalendarPopup::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_onDate = nullptr;
    m_onClear = nullptr;
}
