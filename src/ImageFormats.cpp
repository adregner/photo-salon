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

QString findPairPartner(const QString &path) {
    QFileInfo info(path);
    if (!info.completeBaseName().endsWith(QLatin1String("_pair")))
        return {};

    QDir dir = info.absoluteDir();
    QStringList exts = supportedExtensions();
    exts.removeAll(QStringLiteral("*.svg"));   // folder navigation excludes SVGs
    const QStringList files = dir.entryList(exts, QDir::Files, QDir::Name);

    QStringList pairFiles;
    for (const QString &name : files) {
        if (QFileInfo(name).completeBaseName().endsWith(QLatin1String("_pair")))
            pairFiles << name;
    }

    // Only pair up when exactly two "_pair" images share the folder — with more,
    // which one is this file's actual partner would be ambiguous.
    if (pairFiles.size() != 2)
        return {};

    const QString fileName = info.fileName();
    if (!pairFiles.contains(fileName))
        return {};

    const QString other = (pairFiles.first() == fileName) ? pairFiles.at(1) : pairFiles.first();
    return dir.absoluteFilePath(other);
}
