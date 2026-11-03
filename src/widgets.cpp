#include "widgets.h"

#include "models.h"
#include "theme.h"

#include <QAction>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QButtonGroup>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QWidgetAction>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSignalBlocker>
#include <QStringList>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

PaperCard::PaperCard(QWidget *parent)
    : QWidget(parent)
{
    // 不要给子控件开 WA_TranslucentBackground。Qt 6.11 会因此再造一个
    // 原生窗口，纸卡就会比外层窗口晚一拍冒出来，看起来像叠了两个程序。
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NativeWindow, false);
    setAutoFillBackground(false);

    quint32 seed = 0x51ed2701u;
    m_specks.reserve(180);
    for (int i = 0; i < 180; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const qreal nx = ((seed >> 8) & 0xffffu) / 65535.0;
        seed = seed * 1664525u + 1013904223u;
        const qreal ny = ((seed >> 8) & 0xffffu) / 65535.0;
        m_specks.append(QPointF(nx, ny));
    }
}

void PaperCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QRectF outer = QRectF(rect()).adjusted(0.7, 0.7, -0.7, -0.7);
    if (outer.width() < 4 || outer.height() < 4)
        return;

    QPainterPath clip;
    clip.addRoundedRect(outer, 16, 16);
    painter.setClipPath(clip);

    QLinearGradient paper(outer.topLeft(), outer.bottomLeft());
    paper.setColorAt(0.0, Theme::PaperTop);
    paper.setColorAt(0.55, QColor(255, 248, 224));
    paper.setColorAt(1.0, Theme::PaperBottom);
    painter.fillRect(rect(), paper);

    for (int i = 0; i < m_specks.size(); ++i) {
        const QPointF &n = m_specks.at(i);
        const QColor dust = (i % 5 == 0) ? QColor(255, 255, 255, 30) : QColor(126, 105, 58, 10);
        painter.setPen(Qt::NoPen);
        painter.setBrush(dust);
        const qreal radius = 0.55 + (i % 4) * 0.22;
        painter.drawEllipse(QPointF(outer.left() + n.x() * outer.width(),
                                    outer.top() + n.y() * outer.height()),
                            radius, radius);
    }

    QLinearGradient bottomShade(outer.topLeft(), outer.bottomLeft());
    bottomShade.setColorAt(0.72, QColor(130, 108, 55, 0));
    bottomShade.setColorAt(1.0, QColor(130, 108, 55, 15));
    painter.fillRect(rect(), bottomShade);

    painter.setClipping(false);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(224, 207, 163, 220), 1.0));
    painter.drawPath(clip);

    painter.setPen(QPen(QColor(255, 255, 255, 130), 1.0));
    painter.drawLine(QPointF(outer.left() + 18, outer.top() + 1.2),
                     QPointF(outer.right() - 18, outer.top() + 1.2));
}

IconButton::IconButton(AppIcon icon, int buttonSize, int iconSize, QWidget *parent)
    : QAbstractButton(parent)
    , m_icon(icon)
    , m_iconSize(iconSize)
    , m_iconColor(Theme::InkSoft)
    , m_hoverColor(99, 88, 55, 24)
{
    setFixedSize(buttonSize, buttonSize);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    connect(this, &QAbstractButton::pressed, this, [this] { update(); });
    connect(this, &QAbstractButton::released, this, [this] { update(); });
}

void IconButton::setIconColor(const QColor &color)
{
    m_iconColor = color;
    update();
}

void IconButton::setHoverColor(const QColor &color)
{
    m_hoverColor = color;
    update();
}

QSize IconButton::sizeHint() const
{
    return size();
}

void IconButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if ((underMouse() || isDown()) && isEnabled()) {
        QColor background = m_hoverColor;
        if (isDown())
            background = QColor(background.red(), background.green(), background.blue(),
                                qMin(255, background.alpha() + 22));
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 7, 7);
    }
    const int side = m_iconSize;
    const QRectF iconRect((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    QColor color = m_iconColor;
    if (!isEnabled())
        color.setAlpha(80);
    drawAppIcon(painter, iconRect, m_icon, color, 1.6);
}

void IconButton::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    update();
}

void IconButton::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    update();
}

TextAction::TextAction(const QString &text, QWidget *parent)
    : QAbstractButton(parent)
    , m_text(text)
    , m_normal(Theme::InkSoft)
    , m_hover(Theme::AccentDark)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFont(Theme::uiFont(12, QFont::DemiBold));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void TextAction::setText(const QString &text)
{
    m_text = text;
    updateGeometry();
    update();
}

void TextAction::setIcon(AppIcon icon)
{
    m_hasIcon = true;
    m_icon = icon;
    updateGeometry();
    update();
}

void TextAction::setColors(const QColor &normal, const QColor &hover)
{
    m_normal = normal;
    m_hover = hover;
    update();
}

QSize TextAction::sizeHint() const
{
    const QFontMetrics metrics(font());
    int width = metrics.horizontalAdvance(m_text) + 14;
    if (m_hasIcon)
        width += 20;
    return QSize(width, 26);
}

void TextAction::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (underMouse())
        painter.fillRect(QRectF(rect()).adjusted(0, 2, 0, -2), QColor(255, 255, 255, 60));
    painter.setFont(font());
    const bool active = underMouse();
    painter.setPen(active ? m_hover : m_normal);
    int x = 6;
    if (m_hasIcon) {
        drawAppIcon(painter, QRectF(x, (height() - 14) / 2.0, 14, 14), m_icon,
                    active ? m_hover : m_normal, 1.5);
        x += 19;
    }
    painter.drawText(QRect(x, 0, width() - x - 4, height()), Qt::AlignLeft | Qt::AlignVCenter, m_text);
}

void TextAction::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    update();
}

void TextAction::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    update();
}

SearchBox::SearchBox(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(34);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::IBeamCursor);

    m_edit = new QLineEdit(this);
    m_edit->setFrame(false);
    m_edit->setFont(Theme::uiFont(13));
    m_edit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; padding: 0; "
        "selection-background-color: #dce2c2; selection-color: #3b382e; }"));
    QPalette palette = m_edit->palette();
    palette.setColor(QPalette::Text, Theme::Ink);
    palette.setColor(QPalette::PlaceholderText, QColor(164, 154, 131));
    m_edit->setPalette(palette);
    m_edit->setClearButtonEnabled(false);
    setFocusProxy(m_edit);
    m_edit->installEventFilter(this);

    connect(m_edit, &QLineEdit::textChanged, this, &SearchBox::textChanged);
    connect(m_edit, &QLineEdit::returnPressed, this, &SearchBox::returnPressed);
}

QString SearchBox::text() const
{
    return m_edit->text();
}

void SearchBox::setText(const QString &text)
{
    const QSignalBlocker blocker(m_edit);
    m_edit->setText(text);
}

void SearchBox::clear()
{
    m_edit->clear();
}

void SearchBox::setPlaceholderText(const QString &text)
{
    m_edit->setPlaceholderText(text);
}

QSize SearchBox::sizeHint() const
{
    return QSize(180, 34);
}

void SearchBox::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_edit->setGeometry(14, 5, qMax(10, width() - 46), height() - 10);
}

void SearchBox::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const bool focused = m_edit->hasFocus();
    const QRectF box = QRectF(rect()).adjusted(0.7, 0.7, -0.7, -0.7);
    painter.setPen(QPen(focused ? QColor(142, 155, 108, 190) : QColor(189, 177, 145, 120), 1.0));
    painter.setBrush(focused ? QColor(255, 255, 255, 120) : QColor(255, 255, 255, 72));
    painter.drawRoundedRect(box, 9, 9);
    drawAppIcon(painter, QRectF(width() - 28, (height() - 15) / 2.0, 15, 15), AppIcon::Search,
                focused ? Theme::Accent : Theme::Muted, 1.5);
}

void SearchBox::mousePressEvent(QMouseEvent *event)
{
    QWidget::mousePressEvent(event);
    m_edit->setFocus(Qt::MouseFocusReason);
}

bool SearchBox::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            update();
        } else if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape) {
                if (!m_edit->text().isEmpty()) {
                    m_edit->clear();
                } else {
                    emit escapePressed();
                }
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

DateChip::DateChip(QWidget *parent)
    : QAbstractButton(parent)
    , m_date(QDate::currentDate())
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFont(Theme::uiFont(12, QFont::Medium));
    setFixedHeight(26);
    setAttribute(Qt::WA_Hover, true);
    setDate(m_date);
}

void DateChip::setDate(QDate date)
{
    m_date = date;
    setToolTip(fullDueLabel(m_date, QDate::currentDate()) + QStringLiteral("  ·  点击选择日期"));
    updateGeometry();
    update();
}

void DateChip::setOverdue(bool overdue)
{
    if (m_overdue == overdue)
        return;
    m_overdue = overdue;
    update();
}

void DateChip::setCompact(bool compact)
{
    m_compact = compact;
    setFixedHeight(compact ? 24 : 26);
    updateGeometry();
    update();
}

QSize DateChip::sizeHint() const
{
    const QFontMetrics metrics(font());
    const QString label = shortDueLabel(m_date, QDate::currentDate());
    const int iconWidth = m_compact ? 0 : 18;
    return QSize(12 + iconWidth + metrics.horizontalAdvance(label) + 11, height());
}

void DateChip::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.6, 0.6, -0.6, -0.6);
    QColor background = QColor(255, 255, 255, m_hover ? 115 : 62);
    QColor border(186, 173, 139, 110);
    QColor foreground = Theme::InkSoft;
    if (m_overdue) {
        background = QColor(241, 216, 198, m_hover ? 190 : 150);
        border = QColor(190, 118, 89, 105);
        foreground = Theme::Danger;
    } else if (m_hover) {
        foreground = Theme::AccentDark;
        border = QColor(160, 149, 113, 150);
    }
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(background);
    painter.drawRoundedRect(box, 7, 7);

    const QString label = shortDueLabel(m_date, QDate::currentDate());
    int x = 8;
    if (!m_compact) {
        drawAppIcon(painter, QRectF(x, (height() - 13) / 2.0, 13, 13), AppIcon::Calendar,
                    foreground, 1.35);
        x += 18;
    }
    painter.setFont(font());
    painter.setPen(foreground);
    painter.drawText(QRect(x, 0, width() - x - 4, height()), Qt::AlignLeft | Qt::AlignVCenter, label);
}

void DateChip::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    m_hover = true;
    update();
}

void DateChip::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    m_hover = false;
    update();
}

PomodoroChip::PomodoroChip(QWidget *parent)
    : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFont(Theme::uiFont(11, QFont::DemiBold));
    setFixedHeight(24);
    setAttribute(Qt::WA_Hover, true);
    refreshTip();
}

void PomodoroChip::setState(bool active, int remainingSeconds, bool paused)
{
    const int remaining = qMax(0, remainingSeconds);
    if (!active)
        paused = false;
    if (m_active == active && m_remaining == remaining && m_paused == paused)
        return;
    m_active = active;
    m_paused = paused;
    m_remaining = remaining;
    refreshTip();
    updateGeometry();
    update();
}

void PomodoroChip::setDefaultMinutes(int minutes)
{
    const int next = clampPomodoroMinutes(minutes);
    if (m_defaultMinutes == next)
        return;
    m_defaultMinutes = next;
    refreshTip();
}

void PomodoroChip::refreshTip()
{
    if (!m_active)
        setToolTip(formatPomodoroStartHint(m_defaultMinutes));
    else if (m_paused)
        setToolTip(formatPomodoroResumeHint(m_remaining));
    else
        setToolTip(formatPomodoroPauseHint(m_remaining));
}

QSize PomodoroChip::sizeHint() const
{
    if (!m_active)
        return QSize(24, 24);
    const QFontMetrics metrics(font());
    const int clockWidth = metrics.horizontalAdvance(QStringLiteral("00:00"));
    return QSize(10 + 13 + 4 + clockWidth + 8, 24);
}

void PomodoroChip::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.6, 0.6, -0.6, -0.6);

    if (m_active) {
        QColor background = m_paused
            ? QColor(210, 205, 180, m_hover || isDown() ? 200 : 150)
            : QColor(220, 226, 194, m_hover || isDown() ? 210 : 165);
        QColor border = m_paused ? QColor(157, 148, 126, 140) : QColor(141, 155, 108, 140);
        QColor foreground = m_paused ? Theme::InkSoft : Theme::AccentDark;
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(background);
        painter.drawRoundedRect(box, 7, 7);
        drawAppIcon(painter, QRectF(7, (height() - 13) / 2.0, 13, 13), AppIcon::Timer,
                    foreground, 1.4);
        painter.setFont(font());
        painter.setPen(foreground);
        painter.drawText(QRect(22, 0, width() - 26, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, formatPomodoroClock(m_remaining));
        return;
    }

    if (m_hover || isDown()) {
        QColor background = QColor(99, 88, 55, isDown() ? 46 : 24);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 7, 7);
    }
    drawAppIcon(painter, QRectF((width() - 15) / 2.0, (height() - 15) / 2.0, 15, 15),
                AppIcon::Timer, m_hover ? Theme::AccentDark : QColor(151, 132, 108), 1.55);
}

void PomodoroChip::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    m_hover = true;
    update();
}

void PomodoroChip::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    m_hover = false;
    update();
}

CircleAddButton::CircleAddButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setFixedSize(36, 36);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setToolTip(QStringLiteral("新建待办  Ctrl+N"));
    setAttribute(Qt::WA_Hover, true);
}

QSize CircleAddButton::sizeHint() const
{
    return QSize(36, 36);
}

void CircleAddButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF circle = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);
    QLinearGradient gradient(circle.topLeft(), circle.bottomRight());
    gradient.setColorAt(0.0, m_hover ? QColor(139, 153, 102) : QColor(126, 140, 92));
    gradient.setColorAt(1.0, m_hover ? QColor(92, 106, 65) : QColor(81, 95, 57));
    painter.setPen(QPen(QColor(255, 255, 255, 44), 1.0));
    painter.setBrush(gradient);
    painter.drawEllipse(circle);
    painter.setPen(QPen(QColor(255, 255, 255, 222), 1.8, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(18, 12.5), QPointF(18, 23.5));
    painter.drawLine(QPointF(12.5, 18), QPointF(23.5, 18));
}

void CircleAddButton::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    m_hover = true;
    update();
}

void CircleAddButton::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    m_hover = false;
    update();
}

SwitchButton::SwitchButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(40, 22);
    m_animation = new QPropertyAnimation(this, "knobPosition", this);
    m_animation->setDuration(120);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(this, &QAbstractButton::toggled, this, [this](bool on) {
        m_animation->stop();
        m_animation->setStartValue(m_knob);
        m_animation->setEndValue(on ? 1.0 : 0.0);
        m_animation->start();
    });
}

void SwitchButton::setKnobPosition(qreal value)
{
    if (qFuzzyCompare(m_knob + 1.0, value + 1.0))
        return;
    m_knob = qBound<qreal>(0.0, value, 1.0);
    update();
}

QSize SwitchButton::sizeHint() const
{
    return QSize(40, 22);
}

void SwitchButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF track(2, 2, width() - 4.0, height() - 4.0);
    QColor trackColor = isChecked() ? Theme::Accent : QColor(215, 203, 174);
    if (!isEnabled())
        trackColor.setAlpha(120);
    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);

    if (hasFocus()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(111, 124, 81, 150), 1.0));
        painter.drawRoundedRect(track.adjusted(-1.0, -1.0, 1.0, 1.0), 10, 10);
    }

    const qreal knobSize = height() - 6.0;
    const qreal left = 3.0;
    const qreal right = width() - knobSize - 3.0;
    const qreal x = left + (right - left) * m_knob;
    const QRectF knob(x, 3.0, knobSize, knobSize);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(76, 67, 45, 34));
    painter.drawEllipse(knob.translated(0, 1.0));
    painter.setBrush(QColor(255, 254, 246));
    painter.drawEllipse(knob);
}

Snackbar::Snackbar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NativeWindow, false);
    setAutoFillBackground(false);
    setFixedHeight(42);
    setFocusPolicy(Qt::NoFocus);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 8, 0);
    layout->setSpacing(8);

    m_label = new QLabel(this);
    m_label->setFont(Theme::uiFont(12));
    m_label->setStyleSheet(QStringLiteral("QLabel { color: #fffdf2; background: transparent; }"));
    m_label->setTextFormat(Qt::PlainText);
    m_label->setWordWrap(false);
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_label, 1);

    m_actionButton = makeActionButton(QString());
    layout->addWidget(m_actionButton, 0);
    m_actionButton->hide();
    connect(m_actionButton, &QPushButton::clicked, this, &Snackbar::triggerAction);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Snackbar::dismiss);

    m_effect = new QGraphicsOpacityEffect(this);
    m_effect->setOpacity(0.0);
    setGraphicsEffect(m_effect);
    m_animation = new QPropertyAnimation(m_effect, "opacity", this);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this, [this] {
        if (m_hiding) {
            hide();
            m_hiding = false;
        }
    });

    hide();
}

void Snackbar::showMessage(const QString &message, const QString &actionText,
                           std::function<void()> action, int timeoutMs)
{
    QVector<QPair<QString, std::function<void()>>> actions;
    if (!actionText.isEmpty())
        actions.append({actionText, std::move(action)});
    showActions(message, actions, timeoutMs);
}

void Snackbar::showActions(const QString &message,
                           const QVector<QPair<QString, std::function<void()>>> &actions,
                           int timeoutMs, const QString &overflowMenuText,
                           const QVector<QPair<QString, std::function<void()>>> &leadingActions,
                           const QString &customInputPlaceholder,
                           std::function<void(int)> customMinutesHandler)
{
    clearExtraActions();
    m_action = nullptr;
    m_fullMessage = message;
    m_timeoutMs = timeoutMs;
    m_customInputPlaceholder = customInputPlaceholder.trimmed();
    m_customMinutesHandler = std::move(customMinutesHandler);
    const bool overflow = !overflowMenuText.trimmed().isEmpty() && actions.size() > 1;
    m_labelLines = overflow ? 2 : 1;
    applyBarHeight(overflow);
    m_label->setToolTip(m_fullMessage);
    setToolTip(m_fullMessage);

    auto *box = qobject_cast<QHBoxLayout *>(layout());
    const auto addLeading = [&](const QPair<QString, std::function<void()>> &item) {
        QPushButton *button = makeActionButton(item.first);
        button->setToolTip(item.first == reminderDismissLabel()
                               ? QStringLiteral("忽略本次提醒，周期规则不变")
                               : item.first);
        const std::function<void()> callback = item.second;
        connect(button, &QPushButton::clicked, this, [this, callback] {
            m_action = nullptr;
            dismiss();
            if (callback)
                callback();
        });
        if (box)
            box->addWidget(button, 0);
        m_extraButtons.append(button);
    };
    for (const auto &item : leadingActions)
        addLeading(item);

    if (actions.size() == 1 && leadingActions.isEmpty()) {
        m_action = actions.front().second;
        m_actionButton->setText(actions.front().first);
        m_actionButton->setVisible(true);
    } else {
        m_actionButton->hide();
        if (overflow) {
            m_overflowActions = actions;
            QPushButton *button = makeActionButton(overflowMenuText);
            QStringList tipParts;
            for (const auto &item : actions)
                tipParts.append(item.first);
            if (!m_customInputPlaceholder.isEmpty() && m_customMinutesHandler)
                tipParts.append(QStringLiteral("自定义"));
            button->setToolTip(tipParts.join(QStringLiteral(" / ")));
            connect(button, &QPushButton::clicked, this, &Snackbar::openOverflowMenu);
            if (box)
                box->addWidget(button, 0);
            m_extraButtons.append(button);
            m_overflowButton = button;
        } else if (actions.size() == 1 && !leadingActions.isEmpty()) {
            const auto &item = actions.front();
            QPushButton *button = makeActionButton(item.first);
            const std::function<void()> callback = item.second;
            connect(button, &QPushButton::clicked, this, [this, callback] {
                m_action = nullptr;
                dismiss();
                if (callback)
                    callback();
            });
            if (box)
                box->addWidget(button, 0);
            m_extraButtons.append(button);
        } else {
            for (const auto &item : actions) {
                QPushButton *button = makeActionButton(item.first);
                button->setToolTip(QStringLiteral("%1 分钟").arg(item.first));
                const std::function<void()> callback = item.second;
                connect(button, &QPushButton::clicked, this, [this, callback] {
                    m_action = nullptr;
                    dismiss();
                    if (callback)
                        callback();
                });
                if (box)
                    box->addWidget(button, 0);
                m_extraButtons.append(button);
            }
        }
    }
    m_timer->stop();
    m_hiding = false;
    show();
    raise();
    fadeTo(1.0, 150, false);
    refreshGeometry();
    if (timeoutMs > 0)
        m_timer->start(timeoutMs);
}

QPushButton *Snackbar::makeActionButton(const QString &text)
{
    auto *button = new QPushButton(text, this);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFont(Theme::uiFont(12, QFont::DemiBold));
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; color: #f1e6b9; padding: 3px 5px; "
        "border-radius: 5px; } QPushButton:hover { background: rgba(255,255,255,0.12); }"));
    return button;
}

void Snackbar::clearExtraActions()
{
    for (QPushButton *button : std::as_const(m_extraButtons)) {
        if (!button)
            continue;
        if (auto *box = qobject_cast<QHBoxLayout *>(layout()))
            box->removeWidget(button);
        button->hide();
        button->deleteLater();
    }
    m_extraButtons.clear();
    m_overflowActions.clear();
    m_overflowButton = nullptr;
    m_customInputPlaceholder.clear();
    m_customMinutesHandler = nullptr;
}

void Snackbar::applyBarHeight(bool wrapped)
{
    setFixedHeight(wrapped ? 56 : 42);
    if (auto *box = qobject_cast<QHBoxLayout *>(layout()))
        box->setContentsMargins(14, wrapped ? 6 : 0, 8, wrapped ? 6 : 0);
}

void Snackbar::openOverflowMenu()
{
    if (m_overflowActions.isEmpty() || m_extraButtons.isEmpty())
        return;
    QPushButton *anchor = m_overflowButton ? m_overflowButton : m_extraButtons.front();
    if (!anchor)
        return;
    m_timer->stop();
    QMenu menu(this);
    menu.setFont(Theme::uiFont(12));
    QVector<QAction *> acts;
    acts.reserve(m_overflowActions.size());
    for (const auto &item : m_overflowActions)
        acts.append(menu.addAction(item.first));

    int customMinutes = 0;
    bool customChosen = false;
    if (!m_customInputPlaceholder.isEmpty() && m_customMinutesHandler) {
        menu.addSeparator();
        auto *row = new QWidget(&menu);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 4, 8, 6);
        rowLayout->setSpacing(6);
        auto *edit = new QLineEdit(row);
        edit->setPlaceholderText(m_customInputPlaceholder);
        edit->setFixedWidth(72);
        edit->setValidator(new QIntValidator(kReminderMinMinutes, kReminderMaxMinutes, edit));
        edit->setToolTip(QStringLiteral("输入 1–%1 的整数分钟").arg(kReminderMaxMinutes));
        auto *ok = new QPushButton(reminderCustomSnoozeConfirmLabel(), row);
        ok->setCursor(Qt::PointingHandCursor);
        ok->setFocusPolicy(Qt::NoFocus);
        ok->setFont(Theme::uiFont(12, QFont::DemiBold));
        rowLayout->addWidget(edit, 1);
        rowLayout->addWidget(ok, 0);
        auto *widgetAction = new QWidgetAction(&menu);
        widgetAction->setDefaultWidget(row);
        menu.addAction(widgetAction);

        const auto submitCustom = [edit, &customMinutes, &customChosen, &menu] {
            bool parsed = false;
            const int minutes = parseReminderSnoozeMinutes(edit->text(), &parsed);
            if (!parsed) {
                edit->setFocus();
                edit->selectAll();
                return;
            }
            customMinutes = minutes;
            customChosen = true;
            menu.close();
        };
        connect(ok, &QPushButton::clicked, &menu, submitCustom);
        connect(edit, &QLineEdit::returnPressed, &menu, submitCustom);
        edit->setFocus();
    }

    QAction *chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 2)));
    if (customChosen) {
        m_action = nullptr;
        dismiss();
        if (m_customMinutesHandler)
            m_customMinutesHandler(customMinutes);
        return;
    }
    if (!chosen) {
        if (m_timeoutMs > 0 && isVisible() && !m_hiding)
            m_timer->start(m_timeoutMs);
        return;
    }
    const int index = static_cast<int>(acts.indexOf(chosen));
    if (index < 0 || index >= m_overflowActions.size())
        return;
    const std::function<void()> callback = m_overflowActions.at(index).second;
    m_action = nullptr;
    dismiss();
    if (callback)
        callback();
}

void Snackbar::dismiss()
{
    if (!isVisible() || m_hiding)
        return;
    m_timer->stop();
    m_hiding = true;
    fadeTo(0.0, 160, true);
}

void Snackbar::fadeTo(qreal opacity, int duration, bool hideWhenDone)
{
    m_animation->stop();
    m_animation->setDuration(duration);
    m_animation->setStartValue(m_effect->opacity());
    m_animation->setEndValue(opacity);
    m_hiding = hideWhenDone;
    m_animation->start();
}

void Snackbar::triggerAction()
{
    auto callback = m_action;
    m_action = nullptr;
    dismiss();
    if (callback)
        callback();
}

void Snackbar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.7, 0.7, -0.7, -0.7);
    painter.setPen(QPen(QColor(255, 255, 255, 23), 1.0));
    painter.setBrush(QColor(65, 72, 47, 247));
    painter.drawRoundedRect(box, 10, 10);
}

void Snackbar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshGeometry();
}

namespace {

QString elideSnackbarText(const QString &text, int width, const QFont &font, int maxLines)
{
    if (text.isEmpty() || width <= 8)
        return text;
    QFontMetrics fm(font);
    if (maxLines <= 1)
        return fm.elidedText(text, Qt::ElideRight, width);

    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    QStringList lines;
    for (int i = 0; i < maxLines; ++i) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        const int start = line.textStart();
        if (i == maxLines - 1) {
            QTextLine extra = layout.createLine();
            if (extra.isValid())
                lines.append(fm.elidedText(text.mid(start), Qt::ElideRight, width));
            else
                lines.append(text.mid(start, line.textLength()));
            break;
        }
        lines.append(text.mid(start, line.textLength()));
    }
    layout.endLayout();
    return lines.join(QLatin1Char('\n'));
}

} // namespace

void Snackbar::refreshGeometry()
{
    if (!m_label)
        return;
    int actionWidth = 0;
    if (m_actionButton->isVisible())
        actionWidth += m_actionButton->sizeHint().width() + 4;
    for (const QPushButton *button : std::as_const(m_extraButtons)) {
        if (button && button->isVisible())
            actionWidth += button->sizeHint().width() + 8;
    }
    const int avail = qMax(40, width() - 28 - actionWidth);
    m_label->setMaximumWidth(avail);
    const QString shown = elideSnackbarText(m_fullMessage.isEmpty() ? m_label->text()
                                                                    : m_fullMessage,
                                            avail, m_label->font(), m_labelLines);
    if (m_label->text() != shown)
        m_label->setText(shown);
    m_label->setToolTip(m_fullMessage);
}

namespace {

class SideTabButton : public QAbstractButton
{
public:
    explicit SideTabButton(const QString &label, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_label(label)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setFixedWidth(34);
        setMinimumHeight(52);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFont(Theme::uiFont(11, QFont::DemiBold));
    }

    QSize sizeHint() const override
    {
        const QFontMetrics metrics(font());
        int textH = 0;
        for (const QChar ch : m_label) {
            if (ch.isSpace())
                continue;
            textH += metrics.height() - 2;
        }
        return QSize(34, qMax(52, textH + 16));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = QRectF(rect()).adjusted(3, 2, -3, -2);
        const bool active = isChecked();
        const bool hover = underMouse() && isEnabled();

        if (active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Theme::AccentSoft);
            painter.drawRoundedRect(box, 8, 8);
            painter.setPen(QPen(QColor(Theme::Accent.red(), Theme::Accent.green(),
                                       Theme::Accent.blue(), 140),
                                1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        } else if (hover) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(99, 88, 55, 22));
            painter.drawRoundedRect(box, 8, 8);
        }

        painter.setFont(font());
        painter.setPen(active ? Theme::AccentDark : (hover ? Theme::Ink : Theme::InkSoft));

        QString chars;
        chars.reserve(m_label.size());
        for (const QChar ch : m_label) {
            if (!ch.isSpace())
                chars.append(ch);
        }
        const QFontMetrics metrics(font());
        const int line = metrics.height() - 2;
        const int totalH = chars.size() * line;
        qreal y = (height() - totalH) / 2.0 + metrics.ascent();
        for (const QChar ch : chars) {
            const int w = metrics.horizontalAdvance(ch);
            painter.drawText(QPointF((width() - w) / 2.0, y), QString(ch));
            y += line;
        }
    }

    void enterEvent(QEnterEvent *event) override
    {
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        QAbstractButton::leaveEvent(event);
        update();
    }

private:
    QString m_label;
};

} // namespace

SideNavBar::SideNavBar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(false);
    setFixedWidth(40);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 4, 2, 8);
    layout->setSpacing(4);

    auto make = [this, layout](const QString &label, Tab tab) {
        auto *btn = new SideTabButton(label, this);
        layout->addWidget(btn, 0, Qt::AlignHCenter);
        connect(btn, &QAbstractButton::clicked, this, [this, tab] { onButtonClicked(tab); });
        return static_cast<QAbstractButton *>(btn);
    };

    m_todos = make(QStringLiteral("主待办"), Tab::Todos);
    m_daily = make(QStringLiteral("日报"), Tab::Daily);
    m_weekly = make(QStringLiteral("周报"), Tab::Weekly);
    m_monthly = make(QStringLiteral("月报"), Tab::Monthly);
    layout->addStretch(1);

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_todos);
    group->addButton(m_daily);
    group->addButton(m_weekly);
    group->addButton(m_monthly);

    setCurrentTab(Tab::Todos);
}

void SideNavBar::setTabVisible(Tab tab, bool visible)
{
    QAbstractButton *btn = nullptr;
    switch (tab) {
    case Tab::Todos:
        btn = m_todos;
        break;
    case Tab::Daily:
        btn = m_daily;
        break;
    case Tab::Weekly:
        btn = m_weekly;
        break;
    case Tab::Monthly:
        btn = m_monthly;
        break;
    }
    if (btn)
        btn->setVisible(visible);
}

void SideNavBar::setCurrentTab(Tab tab)
{
    m_current = tab;
    if (m_todos)
        m_todos->setChecked(tab == Tab::Todos);
    if (m_daily)
        m_daily->setChecked(tab == Tab::Daily);
    if (m_weekly)
        m_weekly->setChecked(tab == Tab::Weekly);
    if (m_monthly)
        m_monthly->setChecked(tab == Tab::Monthly);
}

void SideNavBar::onButtonClicked(Tab tab)
{
    if (tab == m_current)
        return;
    setCurrentTab(tab);
    emit tabSelected(tab);
}

void SideNavBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Soft divider on the right edge of the rail.
    painter.setPen(QPen(QColor(Theme::PaperLine.red(), Theme::PaperLine.green(),
                               Theme::PaperLine.blue(), 160),
                        1.0));
    const qreal x = width() - 0.5;
    painter.drawLine(QPointF(x, 8), QPointF(x, height() - 10));
}

QuickAddPopup::QuickAddPopup(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::NoDropShadowWindowHint)
{
    setObjectName(QStringLiteral("quickAddPopup"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("快速添加"));
    setFixedSize(316, 138);
    setPalette(Theme::applicationPalette());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 16);
    root->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("快速添加"), this);
    title->setFont(Theme::uiFont(13, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(Theme::Ink.name()));
    root->addWidget(title);

    m_edit = new QLineEdit(this);
    m_edit->setObjectName(QStringLiteral("quickAddEdit"));
    m_edit->setFont(Theme::uiFont(13));
    m_edit->setPlaceholderText(QStringLiteral("写下一件要做的事…"));
    m_edit->setMaxLength(4000);
    m_edit->setClearButtonEnabled(false);
    m_edit->setFixedHeight(32);
    m_edit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: rgba(255, 255, 255, 0.78); border: 1px solid #c9bd98; "
        "border-radius: 8px; padding: 4px 8px; "
        "selection-background-color: #dce2c2; selection-color: #3b382e; }"));
    QPalette editPalette = m_edit->palette();
    editPalette.setColor(QPalette::Text, Theme::Ink);
    editPalette.setColor(QPalette::PlaceholderText, QColor(158, 148, 123));
    m_edit->setPalette(editPalette);
    root->addWidget(m_edit);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(14);
    row->addStretch(1);
    m_cancel = new TextAction(QStringLiteral("取消"), this);
    m_cancel->setObjectName(QStringLiteral("quickAddCancel"));
    m_cancel->setFont(Theme::uiFont(12));
    m_cancel->setColors(Theme::Muted, Theme::InkSoft);
    m_confirm = new TextAction(QStringLiteral("添加"), this);
    m_confirm->setObjectName(QStringLiteral("quickAddConfirm"));
    m_confirm->setFont(Theme::uiFont(12, QFont::DemiBold));
    m_confirm->setColors(Theme::AccentDark, Theme::Accent);
    row->addWidget(m_cancel, 0, Qt::AlignVCenter);
    row->addWidget(m_confirm, 0, Qt::AlignVCenter);
    root->addLayout(row);

    connect(m_edit, &QLineEdit::returnPressed, this, &QuickAddPopup::confirm);
    connect(m_confirm, &QAbstractButton::clicked, this, &QuickAddPopup::confirm);
    connect(m_cancel, &QAbstractButton::clicked, this, &QuickAddPopup::cancel);
}

void QuickAddPopup::openNear(const QPoint &globalPos)
{
    m_closing = false;
    m_ignoreDeactivate = true;
    m_edit->clear();
    const QSize popupSize = size();
    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    int x = globalPos.x() - popupSize.width() / 2;
    int y = globalPos.y() + 8;
    if (y + popupSize.height() > available.bottom() + 1)
        y = globalPos.y() - popupSize.height() - 8;
    x = std::clamp(x, available.left() + 4, qMax(available.left() + 4,
                                                 available.right() - popupSize.width() - 3));
    y = std::clamp(y, available.top() + 4, qMax(available.top() + 4,
                                                available.bottom() - popupSize.height() - 3));
    move(x, y);
    show();
    raise();
    activateWindow();
    m_edit->setFocus(Qt::PopupFocusReason);
    QTimer::singleShot(180, this, [this] { m_ignoreDeactivate = false; });
}

QString QuickAddPopup::text() const
{
    return m_edit ? m_edit->text() : QString();
}

void QuickAddPopup::setText(const QString &text)
{
    if (m_edit)
        m_edit->setText(text);
}

void QuickAddPopup::confirm()
{
    if (m_closing)
        return;
    const QString trimmed = text().trimmed();
    if (trimmed.isEmpty()) {
        if (m_edit)
            m_edit->setFocus(Qt::OtherFocusReason);
        return;
    }
    finish(true);
}

void QuickAddPopup::cancel()
{
    if (m_closing)
        return;
    finish(false);
}

void QuickAddPopup::finish(bool accept)
{
    m_closing = true;
    m_ignoreDeactivate = true;
    const QString trimmed = text().trimmed();
    hide();
    if (accept)
        emit accepted(trimmed);
    else
        emit cancelled();
}

void QuickAddPopup::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(8, 7, -8, -7);
    for (int i = 6; i >= 1; --i) {
        const qreal spread = i * 1.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(76, 66, 42, 4 + (6 - i)));
        painter.drawRoundedRect(card.adjusted(-spread, -spread * 0.4, spread, spread + 2), 11 + i,
                                11 + i);
    }
    painter.setPen(QPen(QColor(217, 203, 166), 1.0));
    painter.setBrush(Theme::PaperTop);
    painter.drawRoundedRect(card, 12, 12);
}

void QuickAddPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancel();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void QuickAddPopup::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (!m_closing)
        cancel();
}

void QuickAddPopup::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::ActivationChange)
        return;
    if (m_ignoreDeactivate || m_closing || !isVisible() || isActiveWindow())
        return;
    cancel();
}
