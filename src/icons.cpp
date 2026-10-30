#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QtMath>

#include <cmath>

QPixmap iconPixmap(AppIcon icon, int size, const QColor &color, qreal dpr)
{
    const int deviceSize = qMax(1, qRound(size * dpr));
    QPixmap pixmap(deviceSize, deviceSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    drawAppIcon(painter, QRectF(0, 0, deviceSize, deviceSize), icon, color, 1.65);
    painter.end();
    return pixmap;
}

QIcon makeIcon(AppIcon icon, int size, const QColor &color)
{
    QIcon result;
    result.addPixmap(iconPixmap(icon, size, color, 1.0));
    result.addPixmap(iconPixmap(icon, size, color, 2.0));
    return result;
}

void drawAppIcon(QPainter &painter, const QRectF &rect, AppIcon icon, const QColor &color,
                 qreal strokeWidth)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.topLeft());
    painter.scale(rect.width() / 20.0, rect.height() / 20.0);

    QPen pen(color, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case AppIcon::Plus:
        painter.drawLine(QPointF(10, 5.2), QPointF(10, 14.8));
        painter.drawLine(QPointF(5.2, 10), QPointF(14.8, 10));
        break;
    case AppIcon::Search:
        painter.drawEllipse(QPointF(9.1, 9.1), 4.0, 4.0);
        painter.drawLine(QPointF(12.2, 12.2), QPointF(15.4, 15.4));
        break;
    case AppIcon::Settings:
        painter.drawLine(QPointF(5.0, 6.6), QPointF(15.0, 6.6));
        painter.drawLine(QPointF(5.0, 10.0), QPointF(15.0, 10.0));
        painter.drawLine(QPointF(5.0, 13.4), QPointF(15.0, 13.4));
        painter.drawEllipse(QPointF(8.2, 6.6), 1.55, 1.55);
        painter.drawEllipse(QPointF(11.8, 10.0), 1.55, 1.55);
        painter.drawEllipse(QPointF(9.2, 13.4), 1.55, 1.55);
        break;
    case AppIcon::Close:
        painter.drawLine(QPointF(6.2, 6.2), QPointF(13.8, 13.8));
        painter.drawLine(QPointF(13.8, 6.2), QPointF(6.2, 13.8));
        break;
    case AppIcon::Minimize:
        painter.drawLine(QPointF(5.8, 10), QPointF(14.2, 10));
        break;
    case AppIcon::Trash:
        painter.drawLine(QPointF(6.1, 6.8), QPointF(13.9, 6.8));
        painter.drawLine(QPointF(8.3, 5.0), QPointF(11.7, 5.0));
        painter.drawLine(QPointF(8.3, 5.0), QPointF(8.3, 6.8));
        painter.drawLine(QPointF(11.7, 5.0), QPointF(11.7, 6.8));
        painter.drawRoundedRect(QRectF(7.0, 6.8, 6.0, 8.2), 1.2, 1.2);
        painter.drawLine(QPointF(9.1, 9.2), QPointF(9.1, 12.7));
        painter.drawLine(QPointF(10.9, 9.2), QPointF(10.9, 12.7));
        break;
    case AppIcon::ChevronDown:
        painter.drawLine(QPointF(6.6, 8.4), QPointF(10, 11.8));
        painter.drawLine(QPointF(10, 11.8), QPointF(13.4, 8.4));
        break;
    case AppIcon::ChevronRight:
        painter.drawLine(QPointF(8.4, 6.6), QPointF(11.8, 10));
        painter.drawLine(QPointF(11.8, 10), QPointF(8.4, 13.4));
        break;
    case AppIcon::Calendar:
        painter.drawRoundedRect(QRectF(4.6, 6.0, 14.8, 11.4), 2.2, 2.2);
        painter.drawLine(QPointF(4.6, 9.3), QPointF(19.4, 9.3));
        painter.drawLine(QPointF(8.0, 4.3), QPointF(8.0, 7.2));
        painter.drawLine(QPointF(12.0, 4.3), QPointF(12.0, 7.2));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(8.0, 12.4), 0.85, 0.85);
        painter.drawEllipse(QPointF(12.0, 12.4), 0.85, 0.85);
        painter.drawEllipse(QPointF(16.0, 12.4), 0.85, 0.85);
        painter.drawEllipse(QPointF(8.0, 15.3), 0.85, 0.85);
        painter.drawEllipse(QPointF(12.0, 15.3), 0.85, 0.85);
        break;
    case AppIcon::Week:
        painter.drawRoundedRect(QRectF(4.6, 6.0, 14.8, 11.4), 2.2, 2.2);
        painter.drawLine(QPointF(4.6, 9.3), QPointF(19.4, 9.3));
        painter.drawLine(QPointF(8.0, 4.3), QPointF(8.0, 7.2));
        painter.drawLine(QPointF(12.0, 4.3), QPointF(12.0, 7.2));
        painter.drawLine(QPointF(7.2, 12.0), QPointF(16.8, 12.0));
        painter.drawLine(QPointF(7.2, 14.6), QPointF(14.2, 14.6));
        break;
    case AppIcon::Month: {
        painter.drawRoundedRect(QRectF(4.6, 5.4, 14.8, 12.2), 2.2, 2.2);
        painter.drawLine(QPointF(4.6, 8.8), QPointF(19.4, 8.8));
        painter.drawLine(QPointF(8.0, 3.8), QPointF(8.0, 6.8));
        painter.drawLine(QPointF(12.0, 3.8), QPointF(12.0, 6.8));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 3; ++col) {
                painter.drawEllipse(QPointF(7.2 + col * 3.8, 11.4 + row * 3.0), 0.8, 0.8);
            }
        }
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case AppIcon::Pin:
        painter.drawEllipse(QPointF(10, 7.1), 2.5, 2.5);
        painter.drawLine(QPointF(10, 9.6), QPointF(10, 15.7));
        painter.drawLine(QPointF(7.7, 15.7), QPointF(12.3, 15.7));
        break;
    case AppIcon::Check: {
        QPainterPath path;
        path.moveTo(4.8, 10.2);
        path.lineTo(8.3, 13.7);
        path.lineTo(15.4, 6.1);
        painter.setPen(QPen(color, strokeWidth * 1.08, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        break;
    }
    case AppIcon::Undo: {
        QPainterPath path;
        path.moveTo(6.0, 10.0);
        path.cubicTo(7.0, 6.8, 13.5, 6.4, 14.7, 10.5);
        path.cubicTo(15.2, 12.4, 13.6, 14.0, 11.0, 14.0);
        painter.drawPath(path);
        painter.drawLine(QPointF(6.0, 10.0), QPointF(6.0, 6.3));
        painter.drawLine(QPointF(6.0, 10.0), QPointF(9.7, 10.0));
        break;
    }
    case AppIcon::ArrowLeft:
        painter.drawLine(QPointF(15.2, 10), QPointF(5.2, 10));
        painter.drawLine(QPointF(5.2, 10), QPointF(9.2, 6));
        painter.drawLine(QPointF(5.2, 10), QPointF(9.2, 14));
        break;
    case AppIcon::ArrowRight:
        painter.drawLine(QPointF(4.8, 10), QPointF(14.8, 10));
        painter.drawLine(QPointF(14.8, 10), QPointF(10.8, 6));
        painter.drawLine(QPointF(14.8, 10), QPointF(10.8, 14));
        break;
    case AppIcon::External:
        painter.drawLine(QPointF(10.0, 10.0), QPointF(15.2, 4.8));
        painter.drawLine(QPointF(10.8, 4.8), QPointF(15.2, 4.8));
        painter.drawLine(QPointF(15.2, 4.8), QPointF(15.2, 9.2));
        painter.drawRoundedRect(QRectF(5.2, 8.5, 6.3, 6.3), 1.1, 1.1);
        break;
    case AppIcon::More:
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(6.0, 10), 1.15, 1.15);
        painter.drawEllipse(QPointF(10.0, 10), 1.15, 1.15);
        painter.drawEllipse(QPointF(14.0, 10), 1.15, 1.15);
        break;
    case AppIcon::Dot:
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(10, 10), 2.2, 2.2);
        break;
    }

    painter.restore();
}
