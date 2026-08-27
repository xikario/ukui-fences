#include "GlassStyle.h"

#include <QBrush>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace GlassStyle {

Profile profile(SurfaceRole role, QColor tint, bool lightSurface)
{
    Profile result;
    result.tint = tint;

    switch (role) {
    case SurfaceRole::Fence:
        result.tint.setAlpha(qBound(42, tint.alpha(), 180));
        result.topHighlightAlpha = lightSurface ? 42 : 58;
        result.leftHighlightAlpha = lightSurface ? 28 : 38;
        result.bottomShadowAlpha = lightSurface ? 20 : 30;
        result.rightShadowAlpha = lightSurface ? 16 : 24;
        result.noiseOpacity = 0.20;
        break;
    case SurfaceRole::Monitor:
        result.tint.setAlpha(qMin(150, tint.alpha()));
        result.topHighlightAlpha = lightSurface ? 38 : 54;
        result.leftHighlightAlpha = lightSurface ? 24 : 34;
        result.bottomShadowAlpha = lightSurface ? 18 : 28;
        result.rightShadowAlpha = lightSurface ? 14 : 22;
        result.noiseOpacity = 0.18;
        break;
    case SurfaceRole::SmartSpace:
        result.tint.setAlpha(lightSurface ? 138 : 168);
        result.topHighlightAlpha = lightSurface ? 38 : 52;
        result.leftHighlightAlpha = lightSurface ? 24 : 34;
        result.bottomShadowAlpha = lightSurface ? 18 : 28;
        result.rightShadowAlpha = lightSurface ? 14 : 22;
        result.noiseOpacity = 0.18;
        break;
    }

    result.border = lightSurface
        ? QColor(255, 255, 255, 105)
        : QColor(255, 255, 255, 62);
    return result;
}

const QImage &noiseTile()
{
    static const QImage tile = [] {
        QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        quint32 state = 0x9e3779b9U;
        for (int y = 0; y < image.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                state = state * 1664525U + 1013904223U;
                const int value = static_cast<int>((state >> 24) & 0xffU);
                const int alpha = 3 + static_cast<int>((state >> 20) & 0x07U);
                row[x] = qPremultiply(value >= 128
                    ? qRgba(255, 255, 255, alpha)
                    : qRgba(0, 0, 0, alpha));
            }
        }
        return image;
    }();
    return tile;
}

void paintLayers(QPainter &painter, const QPainterPath &path,
                 const QRectF &bounds, const Profile &style,
                 bool drawBorder)
{
    if (path.isEmpty() || bounds.isEmpty())
        return;

    painter.save();
    painter.setClipPath(path);
    painter.fillPath(path, style.tint);

    const qreal topDepth = qMin<qreal>(72.0, bounds.height() * 0.30);
    QLinearGradient top(bounds.topLeft(),
                        QPointF(bounds.left(), bounds.top() + topDepth));
    top.setColorAt(0.0, QColor(255, 255, 255, style.topHighlightAlpha));
    top.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(QRectF(bounds.left(), bounds.top(),
                            bounds.width(), topDepth), top);

    const qreal leftDepth = qMin<qreal>(42.0, bounds.width() * 0.14);
    QLinearGradient left(bounds.topLeft(),
                         QPointF(bounds.left() + leftDepth, bounds.top()));
    left.setColorAt(0.0, QColor(255, 255, 255, style.leftHighlightAlpha));
    left.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(QRectF(bounds.left(), bounds.top(),
                            leftDepth, bounds.height()), left);

    const qreal bottomDepth = qMin<qreal>(48.0, bounds.height() * 0.22);
    QLinearGradient bottom(
        QPointF(bounds.left(), bounds.bottom() - bottomDepth),
        bounds.bottomLeft());
    bottom.setColorAt(0.0, QColor(0, 0, 0, 0));
    bottom.setColorAt(1.0, QColor(0, 0, 0, style.bottomShadowAlpha));
    painter.fillRect(QRectF(bounds.left(), bounds.bottom() - bottomDepth,
                            bounds.width(), bottomDepth), bottom);

    const qreal rightDepth = qMin<qreal>(42.0, bounds.width() * 0.14);
    QLinearGradient right(
        QPointF(bounds.right() - rightDepth, bounds.top()), bounds.topRight());
    right.setColorAt(0.0, QColor(0, 0, 0, 0));
    right.setColorAt(1.0, QColor(0, 0, 0, style.rightShadowAlpha));
    painter.fillRect(QRectF(bounds.right() - rightDepth, bounds.top(),
                            rightDepth, bounds.height()), right);

    QBrush noise;
    noise.setTextureImage(noiseTile());
    painter.setOpacity(style.noiseOpacity);
    painter.fillPath(path, noise);
    painter.restore();

    if (drawBorder) {
        painter.save();
        painter.setPen(QPen(style.border, style.borderWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        painter.restore();
    }
}

} // namespace GlassStyle
