#include "GlassStyle.h"

#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>

namespace {

bool check(bool condition, const char *message)
{
    if (!condition)
        QTextStream(stderr) << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    const QImage &noise = GlassStyle::noiseTile();
    ok &= check(noise.size() == QSize(64, 64),
                "noise tile should be 64x64");
    ok &= check(noise.format() == QImage::Format_ARGB32_Premultiplied,
                "noise tile should use premultiplied alpha");
    ok &= check(qAlpha(noise.pixel(7, 11)) >= 3 &&
                qAlpha(noise.pixel(7, 11)) <= 10,
                "noise alpha should remain subtle");

    QImage surface(240, 160, QImage::Format_ARGB32_Premultiplied);
    surface.fill(Qt::transparent);
    QPainter painter(&surface);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(12, 12, 216, 136), 18, 18);
    const GlassStyle::Profile style = GlassStyle::profile(
        GlassStyle::SurfaceRole::Fence, QColor(20, 40, 70, 120), false);
    GlassStyle::paintLayers(
        painter, path, QRectF(12, 12, 216, 136), style, true);
    painter.end();

    ok &= check(qAlpha(surface.pixel(120, 80)) > 0,
                "glass center should be painted");
    ok &= check(qAlpha(surface.pixel(0, 0)) == 0,
                "outside the rounded path should stay transparent");
    ok &= check(qGray(surface.pixel(30, 24)) >
                    qGray(surface.pixel(210, 136)),
                "top-left highlight should exceed bottom-right depth");

    if (ok)
        QTextStream(stdout) << "GlassStyle checks passed\n";
    return ok ? 0 : 1;
}
