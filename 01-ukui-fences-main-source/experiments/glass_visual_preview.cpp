#include "StackBlur.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>

namespace {

void paintGlassPanel(QPainter &painter, const QImage &blurred,
                     const QRect &rect, const QColor &tint,
                     const QString &title, int radius)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(rect), radius, radius);

    painter.save();
    painter.setClipPath(path);
    painter.drawImage(rect, blurred, rect);
    painter.fillPath(path, tint);

    QLinearGradient top(rect.topLeft(),
                        QPoint(rect.left(), rect.top() + qMin(72, rect.height() / 3)));
    top.setColorAt(0.0, QColor(255, 255, 255, 62));
    top.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(rect, top);

    QLinearGradient bottom(QPoint(rect.left(), rect.bottom() - 48),
                           rect.bottomLeft());
    bottom.setColorAt(0.0, QColor(0, 0, 0, 0));
    bottom.setColorAt(1.0, QColor(0, 0, 0, 34));
    painter.fillRect(rect, bottom);
    painter.restore();

    painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    painter.setPen(Qt::white);
    QFont titleFont = painter.font();
    titleFont.setPixelSize(20);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(rect.adjusted(22, 15, -18, -12),
                     Qt::AlignLeft | Qt::AlignTop, title);

    QFont bodyFont = titleFont;
    bodyFont.setPixelSize(13);
    bodyFont.setBold(false);
    painter.setFont(bodyFont);
    painter.setPen(QColor(238, 244, 255, 220));
    painter.drawText(rect.adjusted(22, 58, -22, -20),
                     Qt::AlignLeft | Qt::AlignTop,
                     QStringLiteral("真实壁纸切片 · StackBlur 28px\n"
                                    "静态预缓存 · 运行时仅 QPixmap 合成\n"
                                    "顶部高光 / 底部内阴影 / 1px 玻璃边框"));
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString wallpaper = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral("/usr/share/backgrounds/island.jpg");
    const QString output = argc > 2
        ? QString::fromLocal8Bit(argv[2])
        : QStringLiteral("glass-visual-preview.png");

    QImage source(wallpaper);
    if (source.isNull()) {
        QTextStream(stderr) << "Unable to load wallpaper: " << wallpaper << '\n';
        return 2;
    }

    const int requestedWidth = argc > 3
        ? qMax(320, QString::fromLocal8Bit(argv[3]).toInt()) : 1440;
    const int requestedHeight = argc > 4
        ? qMax(200, QString::fromLocal8Bit(argv[4]).toInt()) : 900;
    const QSize canvasSize(requestedWidth, requestedHeight);
    QImage canvas = source.scaled(canvasSize, Qt::KeepAspectRatioByExpanding,
                                  Qt::SmoothTransformation);
    const int cropX = qMax(0, (canvas.width() - canvasSize.width()) / 2);
    const int cropY = qMax(0, (canvas.height() - canvasSize.height()) / 2);
    canvas = canvas.copy(cropX, cropY, canvasSize.width(), canvasSize.height())
                 .convertToFormat(QImage::Format_RGB32);

    QElapsedTimer timer;
    timer.start();
    QImage blurred = canvas.copy();
    GlassEffect::stackBlur(blurred, 28);
    const qint64 blurMs = timer.elapsed();

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    const qreal sx = canvas.width() / 1440.0;
    const qreal sy = canvas.height() / 900.0;
    const auto scaledRect = [sx, sy](const QRect &rect) {
        return QRect(qRound(rect.x() * sx), qRound(rect.y() * sy),
                     qRound(rect.width() * sx), qRound(rect.height() * sy));
    };
    paintGlassPanel(painter, blurred, scaledRect(QRect(90, 110, 390, 310)),
                    QColor(26, 39, 61, 142), QStringLiteral("Fence 区域"), 16);
    paintGlassPanel(painter, blurred, scaledRect(QRect(520, 180, 820, 260)),
                    QColor(18, 28, 46, 152), QStringLiteral("System Monitor"), 18);
    paintGlassPanel(painter, blurred, scaledRect(QRect(210, 510, 1010, 280)),
                    QColor(15, 23, 42, 160), QStringLiteral("Smart Space"), 20);
    painter.end();

    if (!canvas.save(output)) {
        QTextStream(stderr) << "Unable to save preview: " << output << '\n';
        return 3;
    }
    QTextStream(stdout) << "preview.output=" << output << '\n'
                        << "preview.size=" << canvas.width() << 'x'
                        << canvas.height() << '\n'
                        << "stackblur.radius=28\n"
                        << "stackblur.elapsed_ms=" << blurMs << '\n';
    return 0;
}
