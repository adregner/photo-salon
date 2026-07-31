#include "HeifPlugin.h"

#include <QFile>
#include <QImage>
#include <QVariant>
#include <libheif/heif.h>
#include <cstring>
#include <mutex>

namespace {

// libheif loads its decoder back-ends (libde265, dav1d, …) on first use; heif_init()
// makes that explicit and is safe to call once per process.
void initLibheif() {
    static std::once_flag once;
    std::call_once(once, [] { heif_init(nullptr); });
}

// Bytes needed by heif_check_filetype() to recognise the ftyp box.
constexpr int SIGNATURE_BYTES = 12;

// Wraps a decoded heif_image so every early return releases it.
struct DecodedImage {
    heif_image *img = nullptr;
    ~DecodedImage() { if (img) heif_image_release(img); }
};

// Locates the TIFF header ("II\x2a\0" or "MM\0\x2a") inside a HEIF "Exif" metadata
// block. The block starts with a 4-byte big-endian offset to that header, but files
// in the wild are inconsistent, so the offset is validated and a short scan is used
// as a fallback. Returns -1 if no TIFF header is found.
int findTiffHeader(const QByteArray &block) {
    auto isTiff = [&block](int at) {
        if (at < 0 || at + 4 > block.size()) return false;
        const char *p = block.constData() + at;
        return (p[0] == 'I' && p[1] == 'I' && p[2] == '\x2a' && p[3] == '\0')
            || (p[0] == 'M' && p[1] == 'M' && p[2] == '\0' && p[3] == '\x2a');
    };

    if (block.size() < 8) return -1;

    const auto *u = reinterpret_cast<const unsigned char *>(block.constData());
    const qint64 declared = 4 + ((qint64(u[0]) << 24) | (qint64(u[1]) << 16)
                                 | (qint64(u[2]) << 8) | qint64(u[3]));
    if (declared <= block.size() - 4 && isTiff(int(declared)))
        return int(declared);

    // Fallback: scan the first few dozen bytes, which is all the preamble any
    // real-world encoder puts in front of the TIFF header.
    const int limit = std::min(block.size(), qsizetype(64));
    for (int i = 0; i + 4 <= limit; ++i)
        if (isTiff(i)) return i;
    return -1;
}

} // namespace

HeifHandler::HeifHandler() = default;

HeifHandler::~HeifHandler() {
    if (m_handle)  heif_image_handle_release(m_handle);
    if (m_context) heif_context_free(m_context);
}

bool HeifHandler::canRead(QIODevice *device) {
    if (!device) return false;
    const QByteArray header = device->peek(SIGNATURE_BYTES);
    if (header.size() < SIGNATURE_BYTES) return false;
    initLibheif();
    const heif_filetype_result result = heif_check_filetype(
        reinterpret_cast<const uint8_t *>(header.constData()), header.size());
    return result == heif_filetype_yes_supported || result == heif_filetype_maybe;
}

bool HeifHandler::canRead() const {
    return canRead(device());
}

bool HeifHandler::readHeader() const {
    if (m_headerRead) return m_headerOk;
    m_headerRead = true;

    if (!device()) return false;
    m_data = device()->readAll();
    if (m_data.isEmpty()) return false;

    initLibheif();
    m_context = heif_context_alloc();
    if (!m_context) return false;

    // ...without_copy: m_data outlives the context, which is destroyed with the handler.
    heif_error err = heif_context_read_from_memory_without_copy(
        m_context, m_data.constData(), size_t(m_data.size()), nullptr);
    if (err.code != heif_error_Ok) return false;

    err = heif_context_get_primary_image_handle(m_context, &m_handle);
    if (err.code != heif_error_Ok || !m_handle) return false;

    m_size = QSize(heif_image_handle_get_width(m_handle),
                   heif_image_handle_get_height(m_handle));
    m_hasAlpha = heif_image_handle_has_alpha_channel(m_handle) != 0;
    m_headerOk = m_size.isValid() && !m_size.isEmpty();
    return m_headerOk;
}

bool HeifHandler::read(QImage *image) {
    if (!image || !readHeader()) return false;

    const heif_chroma chroma = m_hasAlpha ? heif_chroma_interleaved_RGBA
                                          : heif_chroma_interleaved_RGB;
    DecodedImage decoded;
    // Default options apply the file's rotate/mirror/crop properties for us.
    const heif_error err = heif_decode_image(m_handle, &decoded.img,
                                             heif_colorspace_RGB, chroma, nullptr);
    if (err.code != heif_error_Ok || !decoded.img) return false;

    const int width  = heif_image_get_width(decoded.img, heif_channel_interleaved);
    const int height = heif_image_get_height(decoded.img, heif_channel_interleaved);
    if (width <= 0 || height <= 0) return false;

    int stride = 0;
    const uint8_t *plane =
        heif_image_get_plane_readonly(decoded.img, heif_channel_interleaved, &stride);
    if (!plane || stride <= 0) return false;

    QImage out(width, height, m_hasAlpha ? QImage::Format_RGBA8888
                                         : QImage::Format_RGB888);
    if (out.isNull()) return false;
    const qsizetype rowBytes = std::min(qsizetype(stride), out.bytesPerLine());
    for (int y = 0; y < height; ++y)
        memcpy(out.scanLine(y), plane + qsizetype(y) * stride, size_t(rowBytes));

    // The decoded size wins: transformed images differ from the untransformed header.
    m_size = out.size();
    *image = out;
    return true;
}

bool HeifHandler::supportsOption(ImageOption option) const {
    return option == Size || option == ImageFormat;
}

QVariant HeifHandler::option(ImageOption option) const {
    switch (option) {
    case Size:
        return readHeader() ? QVariant(m_size) : QVariant();
    case ImageFormat:
        if (!readHeader()) return {};
        // QImageReader reads this option back with toInt(), so hand it an int.
        return int(m_hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    default:
        return {};
    }
}

QImageIOPlugin::Capabilities HeifPlugin::capabilities(QIODevice *device,
                                                      const QByteArray &format) const {
    if (format == "heic" || format == "heif" || format == "hif")
        return Capabilities(CanRead);
    if (!format.isEmpty() || !device || !device->isOpen() || !device->isReadable())
        return {};
    return HeifHandler::canRead(device) ? Capabilities(CanRead) : Capabilities();
}

QImageIOHandler *HeifPlugin::create(QIODevice *device, const QByteArray &format) const {
    auto *handler = new HeifHandler;
    handler->setDevice(device);
    handler->setFormat(format.isEmpty() ? QByteArrayLiteral("heic") : format);
    return handler;
}

QByteArray heifExifSegment(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (!HeifHandler::canRead(&file)) return {};
    const QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty()) return {};

    initLibheif();
    heif_context *ctx = heif_context_alloc();
    if (!ctx) return {};
    heif_image_handle *handle = nullptr;
    QByteArray segment;

    heif_error err = heif_context_read_from_memory_without_copy(
        ctx, data.constData(), size_t(data.size()), nullptr);
    if (err.code == heif_error_Ok)
        err = heif_context_get_primary_image_handle(ctx, &handle);

    if (err.code == heif_error_Ok && handle) {
        heif_item_id id = 0;
        if (heif_image_handle_get_list_of_metadata_block_IDs(handle, "Exif", &id, 1) == 1) {
            const size_t size = heif_image_handle_get_metadata_size(handle, id);
            if (size > 8 && size < 64u * 1024 * 1024) {
                QByteArray block(int(size), Qt::Uninitialized);
                if (heif_image_handle_get_metadata(handle, id, block.data()).code
                        == heif_error_Ok) {
                    const int tiff = findTiffHeader(block);
                    if (tiff >= 0)
                        // easyexif wants a JPEG APP1 payload: the "Exif\0\0" marker
                        // followed by the TIFF block.
                        segment = QByteArray("Exif\0\0", 6) + block.mid(tiff);
                }
            }
        }
    }

    if (handle) heif_image_handle_release(handle);
    heif_context_free(ctx);
    return segment;
}
