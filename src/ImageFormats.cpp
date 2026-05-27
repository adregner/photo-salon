#include "ImageFormats.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

QStringList supportedExtensions() {
    QStringList filters;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        filters << QString("*.%1").arg(QString::fromLatin1(fmt).toLower());
    return filters;
}

QString resolveImagePath(const QString &arg, QString *error) {
    QFileInfo info(arg);

    if (!info.exists()) {
        if (error) *error = QString("File or folder not found: %1").arg(arg);
        return {};
    }

    if (info.isDir()) {
        QDir dir(arg);
        QStringList files = dir.entryList(supportedExtensions(), QDir::Files, QDir::Name);
        if (files.isEmpty()) {
            if (error) *error = QString("No supported images found in: %1").arg(arg);
            return {};
        }
        return dir.absoluteFilePath(files.first());
    }

    return info.absoluteFilePath();
}
