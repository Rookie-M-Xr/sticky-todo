#pragma once

#include <QDate>
#include <QVector>
#include <QWidget>

#include <functional>

class QGridLayout;
class QLabel;
class QHideEvent;
class QKeyEvent;
class QPaintEvent;

class CalendarPopup : public QWidget
{
    Q_OBJECT

public:
    explicit CalendarPopup(QWidget *parent = nullptr);

    void openFor(const QDate &selected, const QPoint &globalAnchor,
                 std::function<void(const QDate &)> onDate,
                 std::function<void()> onClear);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void buildUi();
    void refresh();
    void shiftMonth(int delta);
    void chooseDate(const QDate &date);
    void chooseClear();
    void closePopup();

    QLabel *m_monthLabel = nullptr;
    QGridLayout *m_grid = nullptr;
    QVector<QWidget *> m_cells;
    QDate m_selected;
    QDate m_viewMonth;
    std::function<void(const QDate &)> m_onDate;
    std::function<void()> m_onClear;
};
