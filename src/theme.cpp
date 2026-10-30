#include "theme.h"

#include <QFontDatabase>
#include <QStringList>

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
    background-color: #fffdf3;
    color: #4d483a;
    border: 1px solid #e2d5b3;
    border-radius: 5px;
    padding: 5px 8px;
}
QMenu {
    background-color: #fffdf4;
    border: 1px solid #e3d6b4;
    border-radius: 12px;
    padding: 6px;
}
QMenu#trayMenu {
    background-color: #fffdf4;
    border: 1px solid #e3d6b4;
    border-radius: 12px;
    padding: 6px;
}
QMenu::item {
    padding: 6px 24px 6px 18px;
    border-radius: 5px;
    color: #4a4538;
}
QMenu::item:selected {
    background-color: #f2e7c6;
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
