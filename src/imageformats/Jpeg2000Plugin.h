#pragma once
#include <QByteArray>
#include <QImageIOPlugin>
#include <QSize>

// QImageIOHandler for the JPEG 2000 family — `.jpf`/`.jpx` (Part 2), `.jp2`
// (Part 1) and raw `.j2k`/`.j2c` codestreams — backed by OpenJPEG.
//
// Registered as a static Qt image-format plugin, so QImageReader — and therefore
// supportedExtensions(), folder navigation and every file dialog — picks these up
// with no call-site changes.
class Jpeg2000Handler : public QImageIOHandler {
public:
    Jpeg2000Handler() = default;

    bool canRead() const override;
    bool read(QImage *image) override;
    QVariant option(ImageOption option) const override;
    bool supportsOption(ImageOption option) const override;

    // True if the first bytes of device are a JP2 signature box or a raw
    // JPEG 2000 codestream. Leaves the device position untouched (peek only).
    static bool canRead(QIODevice *device);

private:
    // Decodes on first use and caches the result: OpenJPEG has no cheap
    // header-only path that a later opj_decode() could reuse.
    bool decode() const;

    mutable QImage m_image;
    mutable bool   m_decoded  = false;
    mutable bool   m_decodeOk = false;
};

class Jpeg2000Plugin : public QImageIOPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface"
                      FILE "jpeg2000.json")
public:
    Capabilities capabilities(QIODevice *device, const QByteArray &format) const override;
    QImageIOHandler *create(QIODevice *device, const QByteArray &format) const override;
};
