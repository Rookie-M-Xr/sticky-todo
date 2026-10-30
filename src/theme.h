#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace Theme {

inline const QColor PaperTop(255, 252, 238);
inline const QColor PaperBottom(253, 243, 208);
inline const QColor PaperEdge(233, 216, 171);
inline const QColor PaperLine(225, 208, 160);
inline const QColor Ink(59, 56, 46);
inline const QColor InkSoft(112, 105, 87);
inline const QColor Muted(157, 148, 126);
inline const QColor Faint(188, 178, 153);
inline const QColor Accent(111, 124, 81);
inline const QColor AccentDark(84, 96, 58);
inline const QColor AccentSoft(220, 226, 194);
inline const QColor Danger(188, 103, 79);
inline const QColor DangerSoft(237, 205, 184);
inline const QColor White(255, 255, 255);

QString uiFamily();
QString displayFamily();
QFont uiFont(int pixelSize, int weight = QFont::Normal);
QFont titleFont(int pixelSize);
QString styleSheet();

} // namespace Theme
