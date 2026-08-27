#include "KWinBlur.h"
#include "MenuStyle.h"

#include <QApplication>
#include <QMenu>
#include <QPainterPath>
#include <QRegion>
#include <QTextStream>
#include <QTimer>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget glass;
    glass.setAttribute(Qt::WA_TranslucentBackground, true);
    glass.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    glass.resize(320, 180);
    glass.show();

    QMenu menu;
    MenuStyle::applyVenturaContextMenu(&menu);
    menu.addAction(QStringLiteral("Tier 2 blur probe"));
    menu.addAction(QStringLiteral("Fallback tint remains available"));

    QTimer::singleShot(0, &app, [&] {
        QTextStream out(stdout);
        const bool supported = KWinBlur::isSupported();
        QPainterPath path;
        path.addRoundedRect(QRectF(glass.rect()), 16, 16);
        const QRegion region(path.toFillPolygon().toPolygon());
        const bool requested = KWinBlur::request(&glass, region);
        const bool propertySet = KWinBlur::hasProperty(&glass);
        out << "kwin.blur.supported=" << (supported ? "true" : "false") << '\n'
            << "kwin.blur.requested=" << (requested ? "true" : "false") << '\n'
            << "kwin.blur.property=" << (propertySet ? "present" : "absent") << '\n';

        menu.popup(glass.mapToGlobal(QPoint(24, 48)));
        QTimer::singleShot(80, &app, [&] {
            QTextStream delayed(stdout);
            const bool menuProperty = KWinBlur::hasProperty(&menu);
            delayed << "kwin.blur.menu_property="
                    << (menuProperty ? "present" : "absent") << '\n';
            menu.hide();
            const bool cleared = KWinBlur::clear(&glass);
            const bool removed = !KWinBlur::hasProperty(&glass);
            delayed << "kwin.blur.clear=" << (cleared ? "pass" : "skip") << '\n'
                    << "kwin.blur.removed=" << (removed ? "true" : "false") << '\n';
            delayed.flush();
            const bool pass = !supported ||
                (requested && propertySet && menuProperty && removed);
            QCoreApplication::exit(pass ? 0 : 2);
        });
        out.flush();
    });
    QTimer::singleShot(10000, &app, [] { QCoreApplication::exit(124); });
    return app.exec();
}
