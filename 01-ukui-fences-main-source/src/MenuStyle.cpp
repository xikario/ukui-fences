#include "MenuStyle.h"

#include "KWinBlur.h"

#include <QAction>
#include <QMenu>
#include <QPainterPath>
#include <QPointer>
#include <QRegion>
#include <QTimer>

namespace MenuStyle {

namespace {

QRegion roundedMenuRegion(const QMenu *menu)
{
    if (!menu || menu->width() <= 1 || menu->height() <= 1)
        return QRegion();
    QPainterPath path;
    path.addRoundedRect(
        QRectF(menu->rect()).adjusted(0.5, 0.5, -0.5, -0.5),
        12.0, 12.0);
    return QRegion(path.toFillPolygon().toPolygon());
}

void applyNativeGlass(QMenu *menu)
{
    if (!menu)
        return;
    menu->ensurePolished();
    menu->adjustSize();
    const QRegion region = roundedMenuRegion(menu);
    if (!region.isEmpty())
        menu->setMask(region);
    const bool active = KWinBlur::request(menu, region);
    menu->setProperty("kwinBlurActive", active);
}

} // namespace

QString venturaContextMenuStyleSheet()
{
    return QStringLiteral(R"(
        QMenu {
            background: qlineargradient(
                x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(45,55,72,218),
                stop:0.38 rgba(30,41,59,210),
                stop:1 rgba(15,23,42,204));
            color: rgba(241,245,249,245);
            border: 1px solid rgba(255,255,255,58);
            border-radius: 12px;
            padding: 5px;
        }
        QMenu::item {
            background-color: transparent;
            padding: 5px 24px 5px 12px;
            border: 1px solid transparent;
            border-radius: 6px;
            min-height: 17px;
        }
        QMenu::item:selected {
            background-color: rgba(59,130,246,205);
            border-color: rgba(255,255,255,45);
            color: white;
        }
        QMenu::item:disabled {
            color: rgba(241,245,249,90);
        }
        QMenu::separator {
            height: 1px;
            background-color: rgba(255,255,255,28);
            margin: 4px 7px;
        }
        QMenu::indicator {
            width: 14px;
            height: 14px;
        }
    )");
}

void prepareGlassMenu(QMenu *menu)
{
    if (!menu)
        return;

    menu->setAttribute(Qt::WA_TranslucentBackground, true);
    menu->setAutoFillBackground(false);
    if (menu->property("ukuiGlassMenuPrepared").toBool())
        return;
    menu->setProperty("ukuiGlassMenuPrepared", true);

    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu] {
        for (QAction *action : menu->actions()) {
            if (QMenu *subMenu = action->menu())
                prepareGlassMenu(subMenu);
        }
        applyNativeGlass(menu);
        QPointer<QMenu> guard(menu);
        QTimer::singleShot(0, menu, [guard] {
            if (guard)
                applyNativeGlass(guard);
        });
        QTimer::singleShot(16, menu, [guard] {
            if (guard)
                applyNativeGlass(guard);
        });
    });
    QObject::connect(menu, &QMenu::aboutToHide, menu, [menu] {
        KWinBlur::clear(menu);
        menu->setProperty("kwinBlurActive", false);
    });
}

void applyVenturaContextMenu(QMenu *menu)
{
    if (!menu)
        return;
    prepareGlassMenu(menu);
    menu->setStyleSheet(venturaContextMenuStyleSheet());
}

} // namespace MenuStyle
