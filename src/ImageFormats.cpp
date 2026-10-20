#include "ImageFormats.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>

void raiseImageAllocationLimit() {
    // Qt refuses to decode any image whose pixel buffer would exceed
    // QImageReader::allocationLimit(), which defaults to 256 MB. That is well
    // under what current cameras produce, and the failure is opaque: the read
    // stops with "Unable to read image data" and the only hint is a
    // qt.gui.imageio warning that is invisible unless logging is turned on.
    //
    // A 7200x5400 16-bit TIFF out of a Leica Q3 is the case that found this.
    // Qt expands 16-bit RGB to RGBA64 — 8 bytes a pixel — so the buffer is
    // 311 MB and the file would not open at all, on any platform.
    //
    // The limit is there to stop a malformed or hostile file from exhausting
    // memory, so raise it rather than removing it (0 disables it entirely).
    // 2 GB covers a 150-megapixel frame at 16 bits per channel, which is past
    // the largest medium-format backs currently sold.
    QImageReader::setAllocationLimit(kImageAllocationLimitMb);
}

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
