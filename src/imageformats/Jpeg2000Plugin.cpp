#include "Jpeg2000Plugin.h"

#include <QImage>
#include <QVariant>
#include <QtGlobal>
#include <openjpeg.h>
#include <algorithm>
#include <cstring>

namespace {

// 12-byte JP2/JPX signature box, shared by .jp2, .jpx and .jpf.
constexpr char JP2_SIGNATURE[] = "\x00\x00\x00\x0C\x6A\x50\x20\x20\x0D\x0A\x87\x0A";
constexpr int  JP2_SIGNATURE_LEN = 12;
// Raw codestream: SOC marker followed by SIZ.
constexpr char J2K_SIGNATURE[] = "\xFF\x4F\xFF\x51";
constexpr int  J2K_SIGNATURE_LEN = 4;

OPJ_CODEC_FORMAT codecFor(const QByteArray &header) {
    if (header.size() >= JP2_SIGNATURE_LEN
        && memcmp(header.constData(), JP2_SIGNATURE, JP2_SIGNATURE_LEN) == 0)
        return OPJ_CODEC_JP2;
    if (header.size() >= J2K_SIGNATURE_LEN
        && memcmp(header.constData(), J2K_SIGNATURE, J2K_SIGNATURE_LEN) == 0)
        return OPJ_CODEC_J2K;
    return OPJ_CODEC_UNKNOWN;
}

// OpenJPEG reads through callbacks; this feeds it the file already in memory.
struct MemoryStream {
    QByteArray data;
    qint64     pos = 0;
};

OPJ_SIZE_T streamRead(void *buffer, OPJ_SIZE_T bytes, void *userData) {
    auto *s = static_cast<MemoryStream *>(userData);
    const qint64 available = s->data.size() - s->pos;
    if (available <= 0) return OPJ_SIZE_T(-1);   // OpenJPEG's EOF signal
    const qint64 n = std::min<qint64>(qint64(bytes), available);
    memcpy(buffer, s->data.constData() + s->pos, size_t(n));
    s->pos += n;
    return OPJ_SIZE_T(n);
}

OPJ_OFF_T streamSkip(OPJ_OFF_T bytes, void *userData) {
    auto *s = static_cast<MemoryStream *>(userData);
    const qint64 target = std::clamp<qint64>(s->pos + bytes, 0, s->data.size());
    const qint64 moved  = target - s->pos;
    s->pos = target;
    return OPJ_OFF_T(moved);
}

OPJ_BOOL streamSeek(OPJ_OFF_T bytes, void *userData) {
    auto *s = static_cast<MemoryStream *>(userData);
    if (bytes < 0 || bytes > s->data.size()) return OPJ_FALSE;
    s->pos = bytes;
    return OPJ_TRUE;
}

void logError(const char *msg, void *) {
    qWarning("JPEG 2000 decode error: %s", msg ? msg : "unknown");
}

// Scales one component sample to 8 bits, undoing any signed offset first.
// 64-bit math throughout: component precision runs up to 31 bits, which would
// overflow the shifts in int.
inline int toByte(qint64 value, OPJ_UINT32 precision, bool isSigned) {
    if (precision == 0 || precision > 32) return 0;
    if (isSigned) value += qint64(1) << (precision - 1);
    const qint64 maxValue = (qint64(1) << precision) - 1;
    value = std::clamp<qint64>(value, 0, maxValue);
    return precision == 8 ? int(value) : int((value * 255 + maxValue / 2) / maxValue);
}

inline int clampByte(int v) { return std::clamp(v, 0, 255); }

// sYCC/e-YCC are stored like JFIF YCbCr: full-range, offset chroma.
inline void yccToRgb(int y, int cb, int cr, int *r, int *g, int *b) {
    const double dy = y, dcb = cb - 128, dcr = cr - 128;
    *r = clampByte(int(dy + 1.402 * dcr + 0.5));
    *g = clampByte(int(dy - 0.344136 * dcb - 0.714136 * dcr + 0.5));
    *b = clampByte(int(dy + 1.772 * dcb + 0.5));
}

// Frees the codec/stream/image trio however the decode exits.
struct DecodeContext {
    opj_codec_t  *codec  = nullptr;
    opj_stream_t *stream = nullptr;
    opj_image_t  *image  = nullptr;
    ~DecodeContext() {
        if (codec)  opj_destroy_codec(codec);
        if (stream) opj_stream_destroy(stream);
        if (image)  opj_image_destroy(image);
    }
};

QImage toQImage(const opj_image_t *image) {
    const OPJ_UINT32 numComps = image->numcomps;
    if (numComps == 0 || !image->comps) return {};
    if (image->color_space == OPJ_CLRSPC_CMYK) {
        qWarning("JPEG 2000: CMYK images are not supported");
        return {};
    }

    // The component grid can be subsampled (4:2:0 chroma, say), so the output is
    // sized to the largest component and the rest are sampled up to match.
    int width = 0, height = 0;
    for (OPJ_UINT32 c = 0; c < numComps; ++c) {
        if (!image->comps[c].data) return {};
        width  = std::max(width,  int(image->comps[c].w));
        height = std::max(height, int(image->comps[c].h));
    }
    if (width <= 0 || height <= 0) return {};

    // 1 = gray, 2 = gray+alpha, 3 = colour, 4 = colour+alpha.
    const bool isColor = numComps >= 3;
    const bool hasAlpha = (numComps == 2 || numComps >= 4);
    const bool isYcc = isColor && (image->color_space == OPJ_CLRSPC_SYCC
                                   || image->color_space == OPJ_CLRSPC_EYCC);

    QImage out(width, height, hasAlpha ? QImage::Format_ARGB32
                                       : QImage::Format_RGB32);
    if (out.isNull()) return {};

    const int alphaIndex = hasAlpha ? (isColor ? 3 : 1) : -1;

    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < width; ++x) {
            auto sample = [&](int c) {
                const opj_image_comp_t &comp = image->comps[c];
                // Nearest-neighbour upsample from this component's own grid.
                const int cx = int(qint64(x) * comp.w / width);
                const int cy = int(qint64(y) * comp.h / height);
                return toByte(comp.data[qint64(cy) * comp.w + cx], comp.prec, comp.sgnd);
            };

            int r, g, b;
            if (isColor) {
                r = sample(0); g = sample(1); b = sample(2);
                if (isYcc) yccToRgb(r, g, b, &r, &g, &b);
            } else {
                r = g = b = sample(0);
            }
            const int a = alphaIndex >= 0 ? sample(alphaIndex) : 255;
            line[x] = qRgba(r, g, b, a);
        }
    }
    return out;
}

} // namespace

bool Jpeg2000Handler::canRead(QIODevice *device) {
    if (!device) return false;
    return codecFor(device->peek(JP2_SIGNATURE_LEN)) != OPJ_CODEC_UNKNOWN;
}

bool Jpeg2000Handler::canRead() const {
    return canRead(device());
}

bool Jpeg2000Handler::decode() const {
    if (m_decoded) return m_decodeOk;
    m_decoded = true;

    if (!device()) return false;
    MemoryStream memory{device()->readAll(), 0};
    const OPJ_CODEC_FORMAT format = codecFor(memory.data.left(JP2_SIGNATURE_LEN));
    if (format == OPJ_CODEC_UNKNOWN) return false;

    DecodeContext ctx;
    ctx.stream = opj_stream_default_create(OPJ_TRUE);
    ctx.codec  = opj_create_decompress(format);
    if (!ctx.stream || !ctx.codec) return false;

    opj_stream_set_user_data(ctx.stream, &memory, nullptr);
    opj_stream_set_user_data_length(ctx.stream, OPJ_UINT64(memory.data.size()));
    opj_stream_set_read_function(ctx.stream, streamRead);
    opj_stream_set_skip_function(ctx.stream, streamSkip);
    opj_stream_set_seek_function(ctx.stream, streamSeek);

    opj_set_error_handler(ctx.codec, logError, nullptr);

    opj_dparameters_t parameters;
    opj_set_default_decoder_parameters(&parameters);
    if (!opj_setup_decoder(ctx.codec, &parameters)) return false;

    if (!opj_read_header(ctx.stream, ctx.codec, &ctx.image) || !ctx.image) return false;
    if (!opj_decode(ctx.codec, ctx.stream, ctx.image)) return false;
    opj_end_decompress(ctx.codec, ctx.stream);

    m_image = toQImage(ctx.image);
    m_decodeOk = !m_image.isNull();
    return m_decodeOk;
}

bool Jpeg2000Handler::read(QImage *image) {
    if (!image || !decode()) return false;
    *image = m_image;
    return true;
}

bool Jpeg2000Handler::supportsOption(ImageOption option) const {
    return option == Size || option == ImageFormat;
}

QVariant Jpeg2000Handler::option(ImageOption option) const {
    switch (option) {
    case Size:
        return decode() ? QVariant(m_image.size()) : QVariant();
    case ImageFormat:
        // QImageReader reads this option back with toInt(), so hand it an int.
        return decode() ? QVariant(int(m_image.format())) : QVariant();
    default:
        return {};
    }
}

QImageIOPlugin::Capabilities Jpeg2000Plugin::capabilities(QIODevice *device,
                                                          const QByteArray &format) const {
    if (format == "jpf" || format == "jpx" || format == "jp2"
        || format == "j2k" || format == "j2c")
        return Capabilities(CanRead);
    if (!format.isEmpty() || !device || !device->isOpen() || !device->isReadable())
        return {};
    return Jpeg2000Handler::canRead(device) ? Capabilities(CanRead) : Capabilities();
}

QImageIOHandler *Jpeg2000Plugin::create(QIODevice *device, const QByteArray &format) const {
    auto *handler = new Jpeg2000Handler;
    handler->setDevice(device);
    handler->setFormat(format.isEmpty() ? QByteArrayLiteral("jpf") : format);
    return handler;
}
