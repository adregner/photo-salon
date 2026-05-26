#include "ImageFormats.h"
#include <QImageReader>

QStringList supportedExtensions() {
    QStringList filters;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        filters << QString("*.%1").arg(QString::fromLatin1(fmt).toLower());
    return filters;
}
