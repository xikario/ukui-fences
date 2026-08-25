#include "OfficeDocumentFactory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QVector>
#include <QDebug>

namespace {

struct PackageExpectation
{
    QString suffix;
    QVector<QByteArray> requiredEntries;
};

bool verifyPackage(const QString &path,
                   const QVector<QByteArray> &requiredEntries)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "cannot read generated package" << path;
        return false;
    }

    const QByteArray data = file.readAll();
    if (!data.startsWith("PK\x03\x04") || data.size() < 512) {
        qCritical() << "generated file is not a non-empty ZIP package" << path;
        return false;
    }
    for (const QByteArray &entry : requiredEntries) {
        if (!data.contains(entry)) {
            qCritical() << "generated package is missing" << entry << path;
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    const QString outputDirectory = argc > 1
        ? QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()
        : directory.path();
    if ((argc <= 1 && !directory.isValid()) ||
        !QDir().mkpath(outputDirectory))
        return 1;

    const QVector<PackageExpectation> packages = {
        {QStringLiteral("docx"),
         {"[Content_Types].xml", "word/document.xml", "word/styles.xml"}},
        {QStringLiteral("xlsx"),
         {"[Content_Types].xml", "xl/workbook.xml",
          "xl/worksheets/sheet1.xml"}},
        {QStringLiteral("pptx"),
         {"[Content_Types].xml", "ppt/presentation.xml",
          "ppt/slides/slide1.xml", "ppt/slideMasters/slideMaster1.xml"}}
    };

    for (const PackageExpectation &package : packages) {
        const QString path = QDir(outputDirectory).absoluteFilePath(
            QStringLiteral("blank.") + package.suffix);
        QString error;
        if (!OfficeDocumentFactory::createBlankFile(path, &error)) {
            qCritical() << "failed to create" << path << error;
            return 2;
        }
        if (!verifyPackage(path, package.requiredEntries))
            return 3;
    }

    const QString textPath =
        QDir(outputDirectory).absoluteFilePath(QStringLiteral("blank.txt"));
    QString error;
    if (!OfficeDocumentFactory::createBlankFile(textPath, &error) ||
        QFileInfo(textPath).size() != 0) {
        qCritical() << "regular blank file creation failed" << error;
        return 4;
    }

    return 0;
}
