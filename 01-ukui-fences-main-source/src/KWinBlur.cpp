#include "KWinBlur.h"

#include <QGuiApplication>
#include <QWidget>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace KWinBlur {

namespace {

Atom blurAtom(Display *display, bool onlyIfExists)
{
    return display
        ? XInternAtom(display, propertyName(), onlyIfExists ? True : False)
        : None;
}

bool usesX11()
{
    const QString platform = QGuiApplication::platformName().toLower();
    return platform == QLatin1String("xcb") ||
           platform.contains(QLatin1String("x11"));
}

} // namespace

const char *propertyName()
{
    return "_KDE_NET_WM_BLUR_BEHIND_REGION";
}

QVector<quint32> encodeRegion(const QRegion &region)
{
    QVector<quint32> encoded;
    const QVector<QRect> rects = region.rects();
    encoded.reserve(rects.size() * 4);
    for (const QRect &rect : rects) {
        if (rect.isEmpty())
            continue;
        encoded << static_cast<quint32>(qMax(0, rect.x()))
                << static_cast<quint32>(qMax(0, rect.y()))
                << static_cast<quint32>(rect.width())
                << static_cast<quint32>(rect.height());
    }
    return encoded;
}

bool isSupported()
{
    if (!usesX11())
        return false;
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return false;
    const bool supported = blurAtom(display, true) != None;
    XCloseDisplay(display);
    return supported;
}

bool request(QWidget *widget, const QRegion &region)
{
    if (!widget || !usesX11())
        return false;

    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return false;
    const Atom atom = blurAtom(display, true);
    if (atom == None) {
        XCloseDisplay(display);
        return false;
    }

    const Window window = static_cast<Window>(widget->winId());
    const QVector<quint32> encoded = encodeRegion(region);
    QVector<unsigned long> x11Data;
    x11Data.reserve(encoded.size());
    for (quint32 value : encoded)
        x11Data.append(static_cast<unsigned long>(value));

    const int status = XChangeProperty(
        display, window, atom, XA_CARDINAL, 32, PropModeReplace,
        x11Data.isEmpty()
            ? nullptr
            : reinterpret_cast<const unsigned char *>(x11Data.constData()),
        x11Data.size());
    XFlush(display);
    XCloseDisplay(display);
    return status != 0;
}

bool clear(QWidget *widget)
{
    if (!widget || !usesX11() || !widget->testAttribute(Qt::WA_WState_Created))
        return false;
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return false;
    const Atom atom = blurAtom(display, true);
    if (atom == None) {
        XCloseDisplay(display);
        return false;
    }
    const int status =
        XDeleteProperty(display, static_cast<Window>(widget->winId()), atom);
    XFlush(display);
    XCloseDisplay(display);
    return status != 0;
}

bool hasProperty(const QWidget *widget)
{
    if (!widget || !usesX11() || !widget->testAttribute(Qt::WA_WState_Created))
        return false;
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return false;
    const Atom atom = blurAtom(display, true);
    if (atom == None) {
        XCloseDisplay(display);
        return false;
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char *data = nullptr;
    const int status = XGetWindowProperty(
        display, static_cast<Window>(widget->winId()), atom,
        0, 1, False, AnyPropertyType, &actualType, &actualFormat,
        &itemCount, &bytesAfter, &data);
    if (data)
        XFree(data);
    XCloseDisplay(display);
    return status == Success && actualType == XA_CARDINAL &&
           actualFormat == 32;
}

} // namespace KWinBlur
