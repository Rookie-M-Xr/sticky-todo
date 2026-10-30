#include "theme.h"
#include "winutils.h"

#include <QApplication>
#include <QBitmap>
#include <QEvent>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QRegion>
#include <QStringList>
#include <QStyleHints>
#include <QToolTip>
#include <QTransform>
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

constexpr int kMenuRadius = 12;
const QColor kMenuFill(255, 253, 244); // #fffdf4

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
    border-radius: %1px;
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
)").arg(kMenuRadius);
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

QRegion roundedRectRegion(const QSize &size, int radius)
{
    const int w = size.width();
    const int h = size.height();
    QRegion region(QRect(QPoint(0, 0), size));
    if (w < 4 || h < 4)
        return region;
    const int r = qBound(1, radius, qMin(w, h) / 2);

    // Punch each AABB corner: square minus the quarter-circle that belongs
    // inside the 12px path. toFillPolygon() was coarse and only cleared TL.
    auto punch = [&](int sx, int sy, int ex, int ey) {
        region -= QRegion(QRect(sx, sy, r, r))
            - QRegion(QRect(ex, ey, 2 * r, 2 * r), QRegion::Ellipse);
    };
    punch(0, 0, 0, 0);
    punch(w - r, 0, w - 2 * r, 0);
    punch(0, h - r, 0, h - 2 * r);
    punch(w - r, h - r, w - 2 * r, h - 2 * r);

    const QPoint aabb[] = {
        {0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1},
    };
    for (const QPoint &corner : aabb)
        region -= QRegion(QRect(corner, QSize(1, 1)));
    return region;
}

QRegion rasterRoundedMask(const QSize &size, int radius, qreal dpr)
{
    const qreal scale = qMax(qreal(1), dpr);
    const int wPx = qMax(1, qRound(size.width() * scale));
    const int hPx = qMax(1, qRound(size.height() * scale));
    QImage image(wPx, hPx, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.scale(scale, scale);
        painter.drawRoundedRect(QRectF(0, 0, size.width(), size.height()), radius, radius);
    }
    QRegion region(QBitmap::fromImage(image.createAlphaMask()));
    if (!qFuzzyCompare(scale, qreal(1))) {
        QTransform toLogical;
        toLogical.scale(qreal(1) / scale, qreal(1) / scale);
        region = toLogical.map(region);
    }
    return region;
}

void applyMenuRoundedMask(QWidget *menu)
{
    const QSize size = menu->size();
    if (size.width() < 4 || size.height() < 4)
        return;
    // Raster at device pixels (smoother HiDPI curve) and intersect the
    // geometric 4-corner punch so AABB samples cannot stay paper.
    QRegion mask = rasterRoundedMask(size, kMenuRadius, menu->devicePixelRatioF());
    mask &= roundedRectRegion(size, kMenuRadius);
    menu->setMask(mask);
}

void paintMenuPaper(QWidget *menu)
{
    if (menu->width() < 2 || menu->height() < 2)
        return;
    QPainter painter(menu);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kMenuFill);
    painter.drawRoundedRect(QRectF(menu->rect()), kMenuRadius, kMenuRadius);
}

void polishMenuWindow(QWidget *menu)
{
    polishPopupWidget(menu);
    // Translucent + QSS fill is glass on Win Fusion. Opaque palette/auto-fill
    // plus a painted rounded rect keeps the body paper; the mask punches corners.
    menu->setAttribute(Qt::WA_TranslucentBackground, false);
    menu->setAttribute(Qt::WA_NoSystemBackground, false);
    menu->setAutoFillBackground(true);
    menu->setAttribute(Qt::WA_StyledBackground, true);
    // Flags before the native HWND exists. setWindowFlags during Show can hide
    // the popup. Tray already has these; context menus do not.
    if (!menu->isVisible()) {
        const Qt::WindowFlags hints = Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
        if ((menu->windowFlags() & hints) != hints)
            menu->setWindowFlags(menu->windowFlags() | Qt::Popup | hints);
    }
}

void polishMenuShape(QWidget *menu)
{
    applyMenuRoundedMask(menu);
    WinUtils::prepareRoundedPopup(menu);
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
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget)
            return false;
        const QEvent::Type type = event->type();
        if (widget->inherits("QMenu")) {
            if (type == QEvent::Polish || type == QEvent::Show)
                polishMenuWindow(widget);
            if (type == QEvent::Polish || type == QEvent::Show || type == QEvent::Resize)
                polishMenuShape(widget);
            if (type == QEvent::Paint)
                paintMenuPaper(widget);
            return false;
        }
        if (type != QEvent::Polish && type != QEvent::Show)
            return false;
        if (widget->inherits("QTipLabel") || widget->inherits("QMessageBox"))
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
