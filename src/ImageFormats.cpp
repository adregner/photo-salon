#include "ImageFormats.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>

QStringList supportedExtensions() {
    QStringList filters;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        filters << QString("*.%1").arg(QString::fromLatin1(fmt).toLower());
    return filters;
}

QString supportedFileFilter() {
    return QStringLiteral("Images (%1);;All Files (*)").arg(supportedExtensions().join(' '));
}

QString supportedSaveFilter() {
    QStringList globs;      // "*.png", "*.jpg", ...
    QStringList perFormat;  // "PNG (*.png)", "JPG (*.jpg)", ...
    for (const QByteArray &fmt : QImageWriter::supportedImageFormats()) {
        const QString ext  = QString::fromLatin1(fmt).toLower();
        const QString glob = QStringLiteral("*.%1").arg(ext);
        globs << glob;
        perFormat << QStringLiteral("%1 (%2)").arg(ext.toUpper(), glob);
    }
    QString filter = QStringLiteral("All Images (%1)").arg(globs.join(' '));
    if (!perFormat.isEmpty())
        filter += QStringLiteral(";;") + perFormat.join(QStringLiteral(";;"));
    return filter + QStringLiteral(";;All Files (*)");
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
