#include "theme.h"

#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QStringList>
#include <QStyleHints>
#include <QToolTip>
#include <QWidget>

namespace {

QString firstAvailable(const QStringList &candidates)
{
    const QStringList families = QFontDatabase::families();
    for (const QString &candidate : candidates) {
        for (const QString &family : families) {
            if (family.compare(candidate, Qt::CaseInsensitive) == 0)
                return family;
        }
    }
    return QStringLiteral("Segoe UI");
}

} // namespace

namespace Theme {

QString uiFamily()
{
    static const QString family = firstAvailable({
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("PingFang SC"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Segoe UI"),
    });
    return family;
}

QString displayFamily()
{
    return uiFamily();
}

QFont uiFont(int pixelSize, int weight)
{
    QFont font(uiFamily());
    font.setPixelSize(qMax(9, pixelSize));
    font.setWeight(static_cast<QFont::Weight>(weight));
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferDefaultHinting);
    return font;
}

QFont titleFont(int pixelSize)
{
    QFont font = uiFont(pixelSize, QFont::Bold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.2);
    return font;
}

QPalette applicationPalette()
{
    QPalette palette;
    const QColor window(255, 253, 244);      // #fffdf4 — QMenu fill
    const QColor tipBase(255, 253, 243);     // #fffdf3 — QToolTip fill
    const QColor tipText(77, 72, 58);        // #4d483a
    const QColor selected(242, 231, 198);    // #f2e7c6
    const QColor selectedText(54, 51, 40);   // #363328

    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, Ink);
    palette.setColor(QPalette::Base, QColor(255, 253, 248));
    palette.setColor(QPalette::AlternateBase, PaperBottom);
    palette.setColor(QPalette::Text, Ink);
    palette.setColor(QPalette::Button, PaperTop);
    palette.setColor(QPalette::ButtonText, Ink);
    palette.setColor(QPalette::BrightText, White);
    palette.setColor(QPalette::Light, White);
    palette.setColor(QPalette::Midlight, QColor(247, 238, 214));
    palette.setColor(QPalette::Mid, PaperLine);
    palette.setColor(QPalette::Dark, PaperEdge);
    palette.setColor(QPalette::Shadow, QColor(139, 128, 102));
    palette.setColor(QPalette::Highlight, selected);
    palette.setColor(QPalette::HighlightedText, selectedText);
    palette.setColor(QPalette::Link, AccentDark);
    palette.setColor(QPalette::LinkVisited, AccentDark);
    palette.setColor(QPalette::ToolTipBase, tipBase);
    palette.setColor(QPalette::ToolTipText, tipText);
    palette.setColor(QPalette::PlaceholderText, Muted);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    palette.setColor(QPalette::Accent, Accent);
#endif

    palette.setColor(QPalette::Disabled, QPalette::WindowText, Faint);
    palette.setColor(QPalette::Disabled, QPalette::Text, Faint);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, Faint);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(235, 224, 196));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, Muted);
    return palette;
}

QString styleSheet()
{
    return QStringLiteral(R"(
QWidget {
    color: #3b382e;
}
QLineEdit {
    background: transparent;
    border: none;
    color: #3b382e;
    placeholder-text-color: #a79d86;
    selection-background-color: #dce2c2;
    selection-color: #3b382e;
}
QLineEdit[editor="true"] {
    background: rgba(255, 255, 255, 0.68);
    border: 1px solid #c9bd98;
    border-radius: 6px;
    padding: 2px 5px;
}
QScrollArea, QAbstractScrollArea {
    background: transparent;
    border: none;
}
QScrollBar:vertical {
    background: transparent;
    width: 7px;
    margin: 2px 0 2px 0;
}
QScrollBar::handle:vertical {
    background: rgba(122, 112, 85, 0.26);
    min-height: 28px;
    border-radius: 3px;
}
QScrollBar::handle:vertical:hover {
    background: rgba(122, 112, 85, 0.42);
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
    height: 0;
}
QScrollBar:horizontal {
    height: 0;
}
QToolTip {
    background: #fffdf3;
    color: #4d483a;
    border: 1px solid #e2d5b3;
    border-radius: 5px;
    padding: 5px 8px;
    opacity: 255;
}
QMenu, QMenu#trayMenu {
    background: #fffdf4;
    color: #4a4538;
    border: 1px solid #e3d6b4;
    border-radius: 12px;
    padding: 6px;
}
QMenu::item {
    background: transparent;
    padding: 6px 24px 6px 18px;
    border-radius: 5px;
    color: #4a4538;
}
QMenu::item:selected {
    background: #f2e7c6;
    color: #363328;
}
QMenu::item:disabled {
    color: #b5aa91;
}
QMenu::separator {
    height: 1px;
    background: #ebe0c4;
    margin: 5px 8px;
}
QMenu::icon {
    padding-left: 6px;
}
)");
}

} // namespace Theme

namespace {

void polishPopupWidget(QWidget *widget)
{
    if (!widget)
        return;
    // The paper window sets Window to transparent. QMenu / QTipLabel parented
    // there inherit that role (or the OS dark palette) and paint black.
    const QPalette paper = Theme::applicationPalette();
    const QColor want = paper.color(QPalette::Window);
    const QColor have = widget->palette().color(QPalette::Window);
    if (have != want || have.alpha() < 255)
        widget->setPalette(paper);
    widget->setAttribute(Qt::WA_StyledBackground, true);
}

class PopupChromeFilter final : public QObject
{
public:
    explicit PopupChromeFilter(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Polish && event->type() != QEvent::Show)
            return false;
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget)
            return false;
        if (widget->inherits("QMenu") || widget->inherits("QTipLabel")
            || widget->inherits("QMessageBox"))
            polishPopupWidget(widget);
        return false;
    }
};

} // namespace

namespace Theme {

void applyApplicationChrome()
{
    QApplication *app = qApp;
    if (!app)
        return;

    // Fusion otherwise follows the OS dark color scheme, so QMenu / QToolTip
    // paint with a dark palette while the paper UI stays light. Pin Light
    // first — setColorScheme rewrites the application palette.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (QStyleHints *hints = app->styleHints())
        hints->setColorScheme(Qt::ColorScheme::Light);
#endif
    const QPalette paper = applicationPalette();
    app->setPalette(paper);
    QToolTip::setPalette(paper);
    app->setStyleSheet(styleSheet());
    app->installEventFilter(new PopupChromeFilter(app));
}

} // namespace Theme
