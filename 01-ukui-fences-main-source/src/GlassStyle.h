#pragma once

#include <QColor>
#include <QImage>
#include <QRectF>

class QPainter;
class QPainterPath;

namespace GlassStyle {

enum class SurfaceRole {
    Fence,
    Monitor,
    SmartSpace
};

struct Profile {
    QColor tint;
    QColor border;
    int topHighlightAlpha = 52;
    int leftHighlightAlpha = 34;
    int bottomShadowAlpha = 28;
    int rightShadowAlpha = 22;
    qreal noiseOpacity = 0.22;
    qreal borderWidth = 1.0;
};

Profile profile(SurfaceRole role, QColor tint, bool lightSurface = false);
const QImage &noiseTile();
void paintLayers(QPainter &painter, const QPainterPath &path,
                 const QRectF &bounds, const Profile &profile,
                 bool drawBorder = true);

} // namespace GlassStyle
