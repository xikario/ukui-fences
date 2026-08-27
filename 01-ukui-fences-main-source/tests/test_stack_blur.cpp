#include "StackBlur.h"

#include <QCoreApplication>
#include <QImage>
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

    QImage impulse(9, 9, QImage::Format_ARGB32);
    impulse.fill(qRgba(0, 0, 0, 255));
    impulse.setPixel(4, 4, qRgba(255, 255, 255, 255));
    GlassEffect::stackBlur(impulse, 2);
    const int center = qRed(impulse.pixel(4, 4));
    const int neighbor = qRed(impulse.pixel(4, 3));
    const int distant = qRed(impulse.pixel(0, 0));
    ok &= check(center > 0 && center < 255,
                "center impulse should be softened");
    ok &= check(neighbor > 0 && neighbor < center,
                "energy should spread to a neighboring pixel");
    ok &= check(distant == 0, "unreached pixels should remain black");
    ok &= check(qAlpha(impulse.pixel(4, 4)) == 255,
                "opaque alpha should remain opaque");

    QImage unchanged(5, 5, QImage::Format_RGB32);
    unchanged.fill(QColor(12, 34, 56));
    const QImage original = unchanged.copy();
    GlassEffect::stackBlur(unchanged, 0);
    ok &= check(unchanged == original, "radius zero should be a no-op");

    if (ok)
        QTextStream(stdout) << "StackBlur correctness checks passed\n";
    return ok ? 0 : 1;
}
