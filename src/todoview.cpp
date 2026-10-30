#include "todoview.h"

#include "datastore.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int kCheckboxSize = 18;
constexpr int kRowTextLeft = 40;
constexpr int kRowSidePadding = 10;
constexpr int kDeleteSize = 24;
constexpr int kMaxTextLines = 2;

QString elideQuery(const QString &query, const QFont &font, int width)
{
    return QFontMetrics(font).elidedText(query, Qt::ElideRight, qMax(40, width));
}

class EmptyStateWidget : public QWidget
{
public:
    EmptyStateWidget(bool searching, const QString &query, std::function<void()> action,
                     QWidget *parent = nullptr)
        : QWidget(parent)
        , m_searching(searching)
        , m_query(query)
        , m_action(new TextAction(searching ? QStringLiteral("清空搜索") : QStringLiteral("写一条待办"), this))
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAutoFillBackground(false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(252);
        m_action->setColors(Theme::AccentDark, Theme::Accent);
        connect(m_action, &QAbstractButton::clicked, this, [callback = std::move(action)] {
            if (callback)
                callback();
        });
    }

    QSize sizeHint() const override { return QSize(220, 252); }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        const QSize hint = m_action->sizeHint();
        const int actionWidth = qMin(width() - 20, hint.width() + 16);
        m_action->setGeometry((width() - actionWidth) / 2, 190, actionWidth, 26);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const qreal centerX = width() / 2.0;
        const QRectF note(centerX - 29, 30, 58, 66);
        painter.save();
        painter.translate(note.center());
        painter.rotate(-3.5);
        painter.translate(-note.center());
        QPainterPath notePath;
        notePath.addRoundedRect(note, 4, 4);
        painter.setPen(QPen(QColor(215, 199, 158), 1.0));
        painter.setBrush(QColor(255, 249, 219));
        painter.drawPath(notePath);

        QPainterPath fold;
        fold.moveTo(note.right() - 15, note.top());
        fold.lineTo(note.right(), note.top() + 15);
        fold.lineTo(note.right() - 15, note.top() + 15);
        fold.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(242, 228, 185));
        painter.drawPath(fold);
        painter.setPen(QPen(QColor(218, 202, 162), 0.9));
        painter.drawLine(QPointF(note.right() - 15, note.top()),
                         QPointF(note.right(), note.top() + 15));

        if (m_searching) {
            drawAppIcon(painter, QRectF(centerX - 13, 54, 26, 26), AppIcon::Search,
                        QColor(132, 143, 103), 1.6);
        } else {
            const qreal left = note.left() + 11;
            const qreal right = note.right() - 10;
            for (int i = 0; i < 3; ++i) {
                const qreal y = note.top() + 20 + i * 13;
                painter.setPen(QPen(QColor(154, 158, 126), 1.2));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(QPointF(left + 3, y), 3.1, 3.1);
                painter.setPen(QPen(QColor(177, 170, 143), 1.5, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(QPointF(left + 11, y), QPointF(right - i * 4, y));
            }
        }
        painter.restore();

        painter.setPen(Theme::Ink);
        painter.setFont(Theme::titleFont(15));
        painter.drawText(QRect(18, 108, width() - 36, 24), Qt::AlignHCenter | Qt::AlignVCenter,
                         m_searching ? QStringLiteral("没有找到相关待办")
                                     : QStringLiteral("还没有待办"));

        painter.setFont(Theme::uiFont(12));
        painter.setPen(Theme::Muted);
        const int textWidth = qMax(80, width() - 44);
        if (m_searching) {
            const QString shown = elideQuery(m_query, font(), textWidth - 12);
            const QString subtitle = shown.isEmpty() ? QStringLiteral("换个关键词试试，或者清空搜索。")
                                       : QStringLiteral("没有找到“%1”，换个关键词试试。").arg(shown);
            painter.drawText(QRect(22, 136, width() - 44, 38),
                             Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, subtitle);
            painter.setFont(Theme::uiFont(11));
            painter.setPen(Theme::Faint);
            painter.drawText(QRect(16, 225, width() - 32, 18), Qt::AlignHCenter,
                             QStringLiteral("按 Esc 也可以清空搜索"));
        } else {
            painter.drawText(QRect(22, 136, width() - 44, 38),
                             Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                             QStringLiteral("在下方写下待办"));
        }
    }

private:
    bool m_searching = false;
    QString m_query;
    TextAction *m_action = nullptr;
};

class BottomFade : public QWidget
{
public:
    explicit BottomFade(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QLinearGradient gradient(0, 0, 0, height());
        QColor top = Theme::PaperBottom;
        top.setAlpha(0);
        QColor middle = Theme::PaperBottom;
        middle.setAlpha(0);
        QColor bottom = Theme::PaperBottom;
        bottom.setAlpha(245);
        gradient.setColorAt(0.0, top);
        gradient.setColorAt(0.45, middle);
        gradient.setColorAt(1.0, bottom);
        painter.fillRect(rect(), gradient);
    }
};

} // namespace

TodoRow::TodoRow(const TodoItem &item, QWidget *parent)
    : QWidget(parent)
    , m_item(item)
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFont(Theme::uiFont(13));
    setCursor(Qt::ArrowCursor);
    setFixedHeight(44);

    m_dateChip = new DateChip(this);
    m_dateChip->setCompact(true);
    m_dateChip->setDate(item.dueDate);
    connect(m_dateChip, &QAbstractButton::clicked, this, [this] {
        emit dateRequested(m_item.id, m_dateChip->mapToGlobal(QPoint(0, m_dateChip->height())));
    });

    m_deleteButton = new IconButton(AppIcon::Trash, kDeleteSize, 15, this);
    m_deleteButton->setIconColor(QColor(151, 132, 108));
    m_deleteButton->setHoverColor(QColor(189, 112, 84, 38));
    m_deleteButton->setToolTip(QStringLiteral("删除（可撤销）"));
    connect(m_deleteButton, &QAbstractButton::clicked, this, [this] {
        emit deleteRequested(m_item.id);
    });

    m_editor = new QLineEdit(this);
    m_editor->setFrame(false);
    m_editor->setFont(Theme::uiFont(13));
    m_editor->setPlaceholderText(QStringLiteral("待办内容"));
    m_editor->setProperty("editor", true);
    m_editor->setTextMargins(4, 0, 4, 0);
    m_editor->installEventFilter(this);
    m_editor->hide();

    setItem(item);
}

void TodoRow::setItem(const TodoItem &item)
{
    m_item = item;
    m_dateChip->setDate(item.dueDate);
    m_dateChip->setOverdue(!item.completed && item.dueDate.isValid()
                           && item.dueDate < QDate::currentDate());
    if (!m_editing)
        m_editor->setText(item.text);
    setAccessibleName(QStringLiteral("待办：%1").arg(item.text));
    setAccessibleDescription(item.completed ? QStringLiteral("已完成") : QStringLiteral("未完成"));
    if (item.text.size() > 34 || item.text.contains(QLatin1Char('\n')))
        setToolTip(item.text);
    else
        setToolTip(QString());
    updateHeight();
    updateChildGeometry();
    updateDeleteVisibility();
    update();
}

QSize TodoRow::sizeHint() const
{
    return QSize(190, height());
}

QRect TodoRow::checkboxRect() const
{
    return QRect(12, (height() - kCheckboxSize) / 2, kCheckboxSize, kCheckboxSize);
}

int TodoRow::textWidth() const
{
    const int chipWidth = m_dateChip ? m_dateChip->sizeHint().width() : 44;
    const int rightBlock = kDeleteSize + 4 + chipWidth + 8;
    return qMax(45, width() - kRowTextLeft - kRowSidePadding - rightBlock);
}

QRect TodoRow::textRect() const
{
    return QRect(kRowTextLeft, 8, textWidth(), qMax(20, height() - 16));
}

void TodoRow::updateHeight()
{
    if (width() < 90)
        return;
    const int tw = textWidth();
    QTextLayout layout(m_item.text, font());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    int count = 0;
    qreal total = 0;
    while (count < kMaxTextLines) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(tw);
        total += line.height();
        ++count;
    }
    layout.endLayout();
    m_lineCount = qMax(1, count);
    const int target = qMax(44, qRound(total) + 18);
    if (height() != target)
        setFixedHeight(target);
}

void TodoRow::updateChildGeometry()
{
    if (!m_dateChip || !m_deleteButton || !m_editor)
        return;
    const int chipWidth = m_dateChip->sizeHint().width();
    const int deleteX = width() - 8 - kDeleteSize;
    m_deleteButton->setGeometry(deleteX, (height() - kDeleteSize) / 2, kDeleteSize, kDeleteSize);
    const int chipX = deleteX - 4 - chipWidth;
    m_dateChip->setGeometry(chipX, (height() - m_dateChip->height()) / 2, chipWidth,
                            m_dateChip->height());
    m_editor->setGeometry(kRowTextLeft, (height() - 26) / 2, textWidth(), 26);
}

void TodoRow::updateDeleteVisibility()
{
    if (!m_deleteButton)
        return;
    m_deleteButton->setVisible(m_hover || hasFocus());
}

void TodoRow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_hover || hasFocus() || m_editing) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, m_editing ? 100 : 56));
        painter.drawRoundedRect(QRectF(rect()).adjusted(2, 3, -2, -3), 9, 9);
    }
    if (hasFocus() && !m_editing) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(141, 155, 108, 120), 1.0));
        painter.drawRoundedRect(QRectF(rect()).adjusted(2.5, 3.5, -2.5, -3.5), 8, 8);
    }

    const QRectF box = QRectF(checkboxRect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (m_item.completed) {
        painter.setPen(QPen(QColor(104, 118, 76), 1.0));
        painter.setBrush(Theme::Accent);
        painter.drawRoundedRect(box, 5, 5);
        drawAppIcon(painter, box.adjusted(2.4, 2.4, -2.4, -2.4), AppIcon::Check,
                    QColor(255, 255, 245), 2.0);
    } else {
        painter.setPen(QPen(m_hover ? QColor(122, 138, 92, 210) : QColor(158, 147, 119, 180), 1.15));
        painter.setBrush(QColor(255, 255, 255, m_hover ? 125 : 76));
        painter.drawRoundedRect(box, 5, 5);
    }

    if (!m_editing) {
        const QRect area = textRect();
        const int tw = qMax(20, area.width());
        QTextLayout layout(m_item.text, font());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        QVector<QTextLine> lines;
        qreal total = 0;
        while (lines.size() < kMaxTextLines) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(tw);
            total += line.height();
            lines.append(line);
        }
        layout.endLayout();

        const QColor textColor = m_item.completed ? QColor(163, 155, 137) : Theme::Ink;
        QColor strikeColor = QColor(164, 155, 136, 150);
        painter.setFont(font());
        painter.setPen(textColor);
        const QFontMetrics metrics(font());
        qreal y = area.y() + qMax<qreal>(0, (area.height() - total) / 2.0);
        for (int i = 0; i < lines.size(); ++i) {
            const QTextLine &line = lines.at(i);
            const int start = line.textStart();
            const int length = line.textLength();
            const bool last = (i == lines.size() - 1);
            QString visible = m_item.text.mid(start, length);
            if (last && start + length < m_item.text.size())
                visible = metrics.elidedText(m_item.text.mid(start), Qt::ElideRight, tw);
            const qreal baseline = y + line.ascent();
            painter.setPen(textColor);
            painter.drawText(QPointF(area.x(), baseline), visible);
            if (m_item.completed) {
                const qreal strikeWidth = qMin<qreal>(line.naturalTextWidth(), tw);
                painter.setPen(QPen(strikeColor, 1.0));
                painter.drawLine(QPointF(area.x(), baseline - metrics.xHeight() * 0.38),
                                 QPointF(area.x() + strikeWidth, baseline - metrics.xHeight() * 0.38));
            }
            y += line.height();
        }
    }

    if (!m_dateChip->isVisible())
        m_dateChip->show();
}

void TodoRow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateHeight();
    updateChildGeometry();
}

void TodoRow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        if (checkboxRect().adjusted(-5, -5, 5, 5).contains(event->pos())) {
            emit completionRequested(m_item.id, !m_item.completed);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void TodoRow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && textRect().adjusted(0, 0, 8, 0).contains(event->pos()))
        startEditing();
    else
        QWidget::mouseDoubleClickEvent(event);
}

void TodoRow::keyPressEvent(QKeyEvent *event)
{
    if (m_editing) {
        QWidget::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_Space:
        emit completionRequested(m_item.id, !m_item.completed);
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_F2:
        startEditing();
        event->accept();
        return;
    case Qt::Key_Delete:
        emit deleteRequested(m_item.id);
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void TodoRow::contextMenuEvent(QContextMenuEvent *event)
{
    openContextMenu(event->globalPos());
    event->accept();
}

void TodoRow::openContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    menu.setFont(Theme::uiFont(12));
    QAction *editAction = menu.addAction(makeIcon(AppIcon::Plus, 14, Theme::InkSoft),
                                         QStringLiteral("编辑内容"));
    QAction *dateAction = menu.addAction(makeIcon(AppIcon::Calendar, 14, Theme::InkSoft),
                                         QStringLiteral("更改计划日期"));
    menu.addSeparator();
    QAction *toggleAction = menu.addAction(
        makeIcon(AppIcon::Check, 14, Theme::AccentDark),
        m_item.completed ? QStringLiteral("恢复为未完成") : QStringLiteral("标记为已完成"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(makeIcon(AppIcon::Trash, 14, Theme::Danger),
                                           QStringLiteral("删除待办"));

    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    if (chosen == editAction)
        startEditing();
    else if (chosen == dateAction)
        emit dateRequested(m_item.id, m_dateChip->mapToGlobal(QPoint(0, m_dateChip->height())));
    else if (chosen == toggleAction)
        emit completionRequested(m_item.id, !m_item.completed);
    else if (chosen == deleteAction)
        emit deleteRequested(m_item.id);
}

void TodoRow::startEditing()
{
    if (m_editing)
        return;
    m_editing = true;
    m_editor->setText(m_item.text);
    m_editor->show();
    m_editor->setFocus(Qt::OtherFocusReason);
    m_editor->selectAll();
    updateChildGeometry();
    updateDeleteVisibility();
    update();
    emit editingStarted();
}

void TodoRow::commitEdit()
{
    if (!m_editing || m_committing)
        return;
    m_committing = true;
    const QString next = m_editor->text().trimmed();
    const QString id = m_item.id;
    const bool changed = !next.isEmpty() && next != m_item.text;
    m_editing = false;
    m_editor->hide();
    updateDeleteVisibility();
    update();
    m_committing = false;
    if (changed)
        emit textEdited(id, next);
}

void TodoRow::cancelEdit()
{
    if (!m_editing)
        return;
    m_editing = false;
    m_editor->hide();
    updateDeleteVisibility();
    update();
}

bool TodoRow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor) {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                commitEdit();
                return true;
            }
            if (key->key() == Qt::Key_Escape) {
                cancelEdit();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            if (m_editing && !m_committing)
                commitEdit();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TodoRow::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    m_hover = true;
    updateDeleteVisibility();
    update();
}

void TodoRow::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hover = false;
    updateDeleteVisibility();
    update();
}

void TodoRow::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    updateDeleteVisibility();
    update();
}

void TodoRow::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    updateDeleteVisibility();
    update();
}

GroupHeader::GroupHeader(GroupId id, QWidget *parent)
    : QWidget(parent)
    , m_id(id)
{
    setFixedHeight(31);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::NoFocus);
}

void GroupHeader::setData(const QString &title, int pendingCount, bool expanded)
{
    m_title = title;
    m_count = pendingCount;
    m_expanded = expanded;
    update();
}

void GroupHeader::setExpanded(bool expanded)
{
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;
    update();
}

void GroupHeader::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (m_hover) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 42));
        painter.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 7, 7);
    }

    const QColor chevronColor = m_hover ? Theme::AccentDark : Theme::Muted;
    drawAppIcon(painter, QRectF(5, (height() - 14) / 2.0, 14, 14),
                m_expanded ? AppIcon::ChevronDown : AppIcon::ChevronRight, chevronColor, 1.55);

    QFont titleFont = Theme::uiFont(12, QFont::DemiBold);
    const QFontMetrics titleMetrics(titleFont);
    QColor titleColor = m_hover ? Theme::AccentDark : Theme::InkSoft;
    if (m_id == GroupId::Overdue && m_count > 0)
        titleColor = Theme::Danger;
    else if (m_id == GroupId::PastCompleted)
        titleColor = Theme::Faint;

    painter.setFont(titleFont);
    painter.setPen(titleColor);
    const int titleX = 24;
    const int baseline = height() / 2 + titleMetrics.ascent() / 2 - 1;
    painter.drawText(QPointF(titleX, baseline), m_title);

    const int titleWidth = titleMetrics.horizontalAdvance(m_title);
    const QFont countFont = Theme::uiFont(11);
    const QFontMetrics countMetrics(countFont);
    const QString countText = QStringLiteral("(%1)").arg(m_count);
    painter.setFont(countFont);
    painter.setPen(m_id == GroupId::Overdue && m_count > 0 ? QColor(190, 126, 101) : Theme::Muted);
    painter.drawText(QPointF(titleX + titleWidth + 4, baseline), countText);

    const int lineStart = titleX + titleWidth + 8 + countMetrics.horizontalAdvance(countText);
    if (lineStart < width() - 14) {
        painter.setPen(QPen(QColor(213, 198, 159, 80), 1.0));
        painter.drawLine(QPointF(lineStart, height() / 2.0), QPointF(width() - 7, height() / 2.0));
    }
}

void GroupHeader::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && clicked)
        clicked();
    QWidget::mouseReleaseEvent(event);
}

void GroupHeader::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    m_hover = true;
    update();
}

void GroupHeader::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hover = false;
    update();
}

GroupSection::GroupSection(const TodoGroup &group, bool expanded, QWidget *parent)
    : QWidget(parent)
    , m_id(group.id)
    , m_title(groupTitle(group.id))
    , m_pendingCount(group.pendingCount)
    , m_expanded(expanded)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_header = new GroupHeader(group.id, this);
    m_header->setData(m_title, m_pendingCount, m_expanded);
    root->addWidget(m_header);

    m_content = new QWidget(this);
    m_content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(1);
    for (const TodoItem &item : group.items) {
        auto *row = new TodoRow(item, m_content);
        connect(row, &TodoRow::completionRequested, this, &GroupSection::completionRequested);
        connect(row, &TodoRow::textEdited, this, &GroupSection::textEdited);
        connect(row, &TodoRow::deleteRequested, this, &GroupSection::deleteRequested);
        connect(row, &TodoRow::dateRequested, this, &GroupSection::dateRequested);
        m_contentLayout->addWidget(row);
    }
    root->addWidget(m_content);

    m_animation = new QPropertyAnimation(m_content, "maximumHeight", this);
    m_animation->setDuration(180);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this, [this] {
        if (m_expanded)
            m_content->setMaximumHeight(QWIDGETSIZE_MAX);
        else
            m_content->setMaximumHeight(0);
    });

    m_header->clicked = [this] {
        setExpanded(!m_expanded, true);
        emit expansionChanged(m_id, m_expanded);
    };

    setExpanded(expanded, false);
}

void GroupSection::setExpanded(bool expanded, bool animate)
{
    const bool changed = (m_expanded != expanded);
    m_expanded = expanded;
    m_header->setExpanded(expanded);
    m_header->setData(m_title, m_pendingCount, expanded);

    if (!animate) {
        m_animation->stop();
        m_content->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : 0);
        updateGeometry();
        return;
    }
    if (!changed)
        return;

    int start = m_content->maximumHeight();
    if (start >= QWIDGETSIZE_MAX)
        start = m_content->height();
    int target = 0;
    if (expanded) {
        target = m_content->layout()->sizeHint().height();
        if (target <= 0)
            target = m_content->sizeHint().height();
        target = qMax(1, target);
    }
    m_animation->stop();
    m_animation->setDuration(180);
    m_animation->setStartValue(start);
    m_animation->setEndValue(target);
    m_animation->start();
}

TodoScrollView::TodoScrollView(DataStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Keep the scrollbar channel reserved even when the list is empty.
    // Otherwise adding the first item changes the viewport width and makes
    // the whole note look like it suddenly became wider/narrower.
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    m_scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->verticalScrollBar()->setSingleStep(28);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    m_content = new QWidget;
    m_content->setAutoFillBackground(false);
    m_content->setStyleSheet(QStringLiteral("background: transparent;"));
    m_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_layout = new QVBoxLayout(m_content);
    m_layout->setContentsMargins(14, 3, 9, 36);
    m_layout->setSpacing(2);
    m_layout->setAlignment(Qt::AlignTop);

    m_scroll->setWidget(m_content);
    root->addWidget(m_scroll);

    // The always-on channel is invisible while there is nothing to scroll,
    // but its width stays constant. This removes the first-item width jump.
    QScrollBar *bar = m_scroll->verticalScrollBar();
    auto syncBar = [bar]() {
        if (bar->maximum() <= 0) {
            bar->setStyleSheet(QStringLiteral(
                "QScrollBar:vertical { background: transparent; width: 7px; margin: 2px 0; }"
                "QScrollBar::handle:vertical { background: transparent; min-height: 28px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
                "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { height: 0; background: transparent; }"));
        } else {
            bar->setStyleSheet(QString());
        }
    };
    connect(bar, &QScrollBar::rangeChanged, this, [syncBar](int, int) { syncBar(); });
    syncBar();

    m_bottomFade = new BottomFade(this);
    m_bottomFade->setFixedHeight(36);
    m_bottomFade->setGeometry(0, height() - 36, width(), 36);
    m_bottomFade->raise();
}

void TodoScrollView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_bottomFade) {
        m_bottomFade->setGeometry(0, height() - 36, width(), 36);
        m_bottomFade->raise();
    }
}

void TodoScrollView::refresh(const QVector<TodoGroup> &groups, const QString &search)
{
    const bool searchChanged = (m_search != search);
    m_search = search;
    const bool searching = !search.trimmed().isEmpty();
    const int previousScroll = m_scroll->verticalScrollBar()->value();
    QWidget *focus = QApplication::focusWidget();
    const bool focusInList = focus && m_content
        && (focus == m_content || m_content->isAncestorOf(focus));
    // Destroying the focused row would move focus onto the composer.
    if (focusInList)
        setFocus(Qt::OtherFocusReason);

    m_scroll->setUpdatesEnabled(false);
    clearContent();

    if (groups.isEmpty()) {
        showEmptyState(searching);
        m_bottomFade->setVisible(false);
    } else {
        m_bottomFade->setVisible(true);
        for (const TodoGroup &group : groups) {
            const bool expanded = searching || (m_store && m_store->groupExpanded(group.id));
            auto *section = new GroupSection(group, expanded, m_content);
            connectSection(section);
            m_layout->addWidget(section);
        }
    }
    m_layout->addStretch(1);
    m_scroll->setUpdatesEnabled(true);
    m_content->updateGeometry();
    m_scroll->widget()->updateGeometry();

    const int restore = searchChanged ? 0 : previousScroll;
    QTimer::singleShot(0, this, [this, restore] {
        QScrollBar *bar = m_scroll->verticalScrollBar();
        bar->setValue(qMin(restore, bar->maximum()));
    });
}

void TodoScrollView::clearContent()
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    m_content->updateGeometry();
}

void TodoScrollView::connectSection(GroupSection *section)
{
    connect(section, &GroupSection::completionRequested, this, &TodoScrollView::completionRequested);
    connect(section, &GroupSection::textEdited, this, &TodoScrollView::textEdited);
    connect(section, &GroupSection::deleteRequested, this, &TodoScrollView::deleteRequested);
    connect(section, &GroupSection::dateRequested, this, &TodoScrollView::dateRequested);
    connect(section, &GroupSection::expansionChanged, this, &TodoScrollView::groupExpansionChanged);
}

void TodoScrollView::showEmptyState(bool searching)
{
    auto *empty = new EmptyStateWidget(searching, m_search, [this, searching] {
        if (searching)
            emit clearSearch();
        else
            emit composeRequested();
    }, m_content);
    m_layout->addWidget(empty);
}
