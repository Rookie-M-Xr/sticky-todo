#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRectF>

enum class AppIcon {
    Plus,
    Search,
    Settings,
    Close,
    Minimize,
    Trash,
    ChevronDown,
    ChevronRight,
    Calendar,
    Week,
    Month,
    Pin,
    Check,
    Undo,
    ArrowLeft,
    ArrowRight,
    External,
    More,
    Dot
};

QPixmap iconPixmap(AppIcon icon, int size, const QColor &color, qreal dpr = 1.0);
QIcon makeIcon(AppIcon icon, int size, const QColor &color);
void drawAppIcon(QPainter &painter, const QRectF &rect, AppIcon icon, const QColor &color,
                 qreal strokeWidth = 1.7);
