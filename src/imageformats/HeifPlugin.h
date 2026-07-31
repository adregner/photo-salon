#pragma once
#include <QByteArray>
#include <QImageIOPlugin>
#include <QSize>

struct heif_context;
struct heif_image_handle;

// QImageIOHandler for HEIF/HEIC (ISO/IEC 23008-12) backed by libheif.
//
// Registered as a static Qt image-format plugin, so QImageReader — and therefore
// supportedExtensions(), folder navigation and every file dialog — picks HEIC up
// with no call-site changes.
//
// Orientation: libheif applies the container's own rotate/mirror transform
// properties while decoding, so the handler deliberately does *not* report
// ImageTransformation. Reporting the EXIF orientation tag as well would rotate
// such files twice under QImageReader::setAutoTransform(true).
class HeifHandler : public QImageIOHandler {
public:
    HeifHandler();
    ~HeifHandler() override;

    bool canRead() const override;
    bool read(QImage *image) override;
    QVariant option(ImageOption option) const override;
    bool supportsOption(ImageOption option) const override;

    // True if the first bytes of device look like a HEIF file we can decode.
    // Leaves the device position untouched (peek only).
    static bool canRead(QIODevice *device);

private:
    // Reads the whole device and opens the primary image handle. Cheap enough to
    // run for a size query — libheif only parses the metadata boxes here.
    bool readHeader() const;

    mutable QByteArray         m_data;
    mutable heif_context      *m_context = nullptr;
    mutable heif_image_handle *m_handle  = nullptr;
    mutable QSize              m_size;
    mutable bool               m_hasAlpha   = false;
    mutable bool               m_headerRead = false;
    mutable bool               m_headerOk   = false;
};

class HeifPlugin : public QImageIOPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface"
                      FILE "heif.json")
public:
    Capabilities capabilities(QIODevice *device, const QByteArray &format) const override;
    QImageIOHandler *create(QIODevice *device, const QByteArray &format) const override;
};

// Returns the file's EXIF metadata as a JPEG-style APP1 payload — "Exif\0\0"
// followed by the TIFF header — ready for easyexif's parseFromEXIFSegment().
// Empty if the file is not HEIF or carries no EXIF item.
QByteArray heifExifSegment(const QString &filePath);
