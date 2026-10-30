#include "widgets.h"

#include "models.h"
#include "theme.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

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
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_label, 1);

    m_actionButton = new QPushButton(this);
    m_actionButton->setCursor(Qt::PointingHandCursor);
    m_actionButton->setFocusPolicy(Qt::NoFocus);
    m_actionButton->setFont(Theme::uiFont(12, QFont::DemiBold));
    m_actionButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; color: #f1e6b9; padding: 3px 6px; "
        "border-radius: 5px; } QPushButton:hover { background: rgba(255,255,255,0.12); }"));
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
    m_action = std::move(action);
    m_label->setText(message);
    m_label->setToolTip(message);
    m_actionButton->setText(actionText);
    m_actionButton->setVisible(!actionText.isEmpty());
    m_timer->stop();
    m_hiding = false;
    show();
    raise();
    fadeTo(1.0, 150, false);
    if (timeoutMs > 0)
        m_timer->start(timeoutMs);
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

void Snackbar::refreshGeometry()
{
    if (!m_label)
        return;
    const int actionWidth = m_actionButton->isVisible()
        ? m_actionButton->sizeHint().width() + 4
        : 0;
    m_label->setMaximumWidth(qMax(40, width() - 28 - actionWidth));
}
