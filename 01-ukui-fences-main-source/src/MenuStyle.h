#pragma once

#include <QString>

class QMenu;

namespace MenuStyle {

QString venturaContextMenuStyleSheet();
void prepareGlassMenu(QMenu *menu);
void applyVenturaContextMenu(QMenu *menu);

} // namespace MenuStyle
