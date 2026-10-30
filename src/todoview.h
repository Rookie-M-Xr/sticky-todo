#pragma once

#include "models.h"
#include "widgets.h"

#include <QWidget>

#include <functional>

class DataStore;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;
class QPropertyAnimation;

class TodoRow : public QWidget
{
    Q_OBJECT
public:
    explicit TodoRow(const TodoItem &item, QWidget *parent = nullptr);

    QString itemId() const { return m_item.id; }
    const TodoItem &item() const { return m_item; }
    void setItem(const TodoItem &item);
    void startEditing();
    bool isEditing() const { return m_editing; }

    QSize sizeHint() const override;

signals:
    void completionRequested(const QString &id, bool completed);
    void textEdited(const QString &id, const QString &text);
    void deleteRequested(const QString &id);
    void dateRequested(const QString &id, const QPoint &globalPos);
    void editingStarted();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QRect checkboxRect() const;
    QRect textRect() const;
    int textWidth() const;
    void updateHeight();
    void updateChildGeometry();
    void commitEdit();
    void cancelEdit();
    void openContextMenu(const QPoint &globalPos);
    void updateDeleteVisibility();

    TodoItem m_item;
    QLineEdit *m_editor = nullptr;
    IconButton *m_deleteButton = nullptr;
    DateChip *m_dateChip = nullptr;
    int m_lineCount = 1;
    bool m_hover = false;
    bool m_editing = false;
    bool m_committing = false;
};

class GroupHeader : public QWidget
{
public:
    explicit GroupHeader(GroupId id, QWidget *parent = nullptr);

    void setData(const QString &title, int pendingCount, bool expanded);
    void setExpanded(bool expanded);
    std::function<void()> clicked;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    GroupId m_id;
    QString m_title;
    int m_count = 0;
    bool m_expanded = true;
    bool m_hover = false;
};

class GroupSection : public QWidget
{
    Q_OBJECT
public:
    explicit GroupSection(const TodoGroup &group, bool expanded, QWidget *parent = nullptr);

    GroupId groupId() const { return m_id; }
    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded, bool animate);
    int pendingCount() const { return m_pendingCount; }

signals:
    void completionRequested(const QString &id, bool completed);
    void textEdited(const QString &id, const QString &text);
    void deleteRequested(const QString &id);
    void dateRequested(const QString &id, const QPoint &globalPos);
    void expansionChanged(GroupId id, bool expanded);

private:
    void animateTo(int targetHeight, int duration);

    GroupId m_id;
    QString m_title;
    int m_pendingCount = 0;
    bool m_expanded = true;
    GroupHeader *m_header = nullptr;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QPropertyAnimation *m_animation = nullptr;
};

class TodoScrollView : public QWidget
{
    Q_OBJECT
public:
    explicit TodoScrollView(DataStore *store, QWidget *parent = nullptr);

    void refresh(const QVector<TodoGroup> &groups, const QString &search);

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void completionRequested(const QString &id, bool completed);
    void textEdited(const QString &id, const QString &text);
    void deleteRequested(const QString &id);
    void dateRequested(const QString &id, const QPoint &globalPos);
    void groupExpansionChanged(GroupId id, bool expanded);
    void clearSearch();
    void composeRequested();

private:
    void clearContent();
    void showEmptyState(bool searching);
    void connectSection(GroupSection *section);

    DataStore *m_store = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_content = nullptr;
    QWidget *m_bottomFade = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QString m_search;
};
