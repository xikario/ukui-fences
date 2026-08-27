#include "KWinBlur.h"

#include <QCoreApplication>
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
    ok &= check(QString::fromLatin1(KWinBlur::propertyName()) ==
                    QStringLiteral("_KDE_NET_WM_BLUR_BEHIND_REGION"),
                "unexpected KWin blur property name");

    const QVector<quint32> single =
        KWinBlur::encodeRegion(QRegion(QRect(3, 5, 90, 60)));
    ok &= check(single == QVector<quint32>({3, 5, 90, 60}),
                "single rectangle encoding mismatch");
    ok &= check(KWinBlur::encodeRegion(QRegion()).isEmpty(),
                "empty region should encode as full-window request");
    ok &= check(!KWinBlur::request(nullptr),
                "null widget request should fail safely");

    if (ok)
        QTextStream(stdout) << "KWin blur codec checks passed\n";
    return ok ? 0 : 1;
}
