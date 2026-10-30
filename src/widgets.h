#pragma once

#include "icons.h"

#include <QAbstractButton>
#include <QColor>
#include <QDate>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QVariantAnimation;

class PaperCard : public QWidget
{
public:
    explicit PaperCard(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPointF> m_specks;
};

class IconButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit IconButton(AppIcon icon, int buttonSize = 26, int iconSize = 17,
                        QWidget *parent = nullptr);
    void setIconColor(const QColor &color);
    void setHoverColor(const QColor &color);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    AppIcon m_icon;
    int m_iconSize;
    QColor m_iconColor;
    QColor m_hoverColor;
};

class TextAction : public QAbstractButton
{
    Q_OBJECT
public:
    explicit TextAction(const QString &text, QWidget *parent = nullptr);
    void setText(const QString &text);
    void setIcon(AppIcon icon);
    void setColors(const QColor &normal, const QColor &hover);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString m_text;
    bool m_hasIcon = false;
    AppIcon m_icon = AppIcon::Plus;
    QColor m_normal;
    QColor m_hover;
};

class SearchBox : public QWidget
{
    Q_OBJECT
public:
    explicit SearchBox(QWidget *parent = nullptr);

    QLineEdit *lineEdit() const { return m_edit; }
    QString text() const;
    void setText(const QString &text);
    void clear();
    void setPlaceholderText(const QString &text);

signals:
    void textChanged(const QString &text);
    void returnPressed();
    void escapePressed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    QSize sizeHint() const override;

private:
    QLineEdit *m_edit = nullptr;
};

class DateChip : public QAbstractButton
{
    Q_OBJECT
public:
    explicit DateChip(QWidget *parent = nullptr);

    QDate date() const { return m_date; }
    void setDate(QDate date);
    void setOverdue(bool overdue);
    void setCompact(bool compact);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QDate m_date;
    bool m_overdue = false;
    bool m_compact = false;
    bool m_hover = false;
};

class CircleAddButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit CircleAddButton(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_hover = false;
};

class SwitchButton : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal knobPosition READ knobPosition WRITE setKnobPosition)
public:
    explicit SwitchButton(QWidget *parent = nullptr);
    qreal knobPosition() const { return m_knob; }
    void setKnobPosition(qreal value);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_knob = 0.0;
    QPropertyAnimation *m_animation = nullptr;
};

class Snackbar : public QWidget
{
    Q_OBJECT
public:
    explicit Snackbar(QWidget *parent = nullptr);

    void showMessage(const QString &message, const QString &actionText = QString(),
                     std::function<void()> action = {}, int timeoutMs = 6000);
    void dismiss();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void fadeTo(qreal opacity, int duration, bool hideWhenDone);
    void refreshGeometry();
    void triggerAction();

    QLabel *m_label = nullptr;
    QPushButton *m_actionButton = nullptr;
    QTimer *m_timer = nullptr;
    QGraphicsOpacityEffect *m_effect = nullptr;
    QPropertyAnimation *m_animation = nullptr;
    std::function<void()> m_action;
    bool m_hiding = false;
};

class SideNavBar : public QWidget
{
    Q_OBJECT
public:
    enum class Tab {
        Todos = 0,
        Daily,
        Weekly,
        Monthly
    };

    explicit SideNavBar(QWidget *parent = nullptr);

    void setTabVisible(Tab tab, bool visible);
    void setCurrentTab(Tab tab);
    Tab currentTab() const { return m_current; }

signals:
    void tabSelected(Tab tab);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void onButtonClicked(Tab tab);

    QAbstractButton *m_todos = nullptr;
    QAbstractButton *m_daily = nullptr;
    QAbstractButton *m_weekly = nullptr;
    QAbstractButton *m_monthly = nullptr;
    Tab m_current = Tab::Todos;
};
