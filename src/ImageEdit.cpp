#include "ImageEdit.h"
#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include <cmath>

// ---------------------------------------------------------------------------
// Registry / ordering
// ---------------------------------------------------------------------------
int editOrderIndex(const QString &type) {
    if (type == QLatin1String("orientation")) return 0;
    if (type == QLatin1String("crop"))        return 1;
    if (type == QLatin1String("adjust"))      return 2;
    if (type == QLatin1String("color"))       return 3;
    if (type == QLatin1String("bw"))          return 4;
    return 99;  // unknown types sort last
}

std::unique_ptr<ImageEdit> makeEdit(const QString &type) {
    if (type == QLatin1String("orientation")) return std::make_unique<OrientationEdit>();
    if (type == QLatin1String("crop"))        return std::make_unique<CropEdit>();
    if (type == QLatin1String("adjust"))      return std::make_unique<AdjustEdit>();
    if (type == QLatin1String("color"))       return std::make_unique<ColorEdit>();
    if (type == QLatin1String("bw"))          return std::make_unique<BwEdit>();
    return nullptr;
}

// ---------------------------------------------------------------------------
// OrientationEdit
// ---------------------------------------------------------------------------
namespace {
// Round a dihedral matrix coefficient (always -1, 0 or 1 up to FP noise).
int roundCoeff(qreal v) { return static_cast<int>(std::lround(v)); }
}  // namespace

void OrientationEdit::compose(const QTransform &step) {
    // (A * B).map(p) == B.map(A.map(p)), i.e. apply the current orientation
    // first and the new step second — exactly what the user saw on screen.
    QTransform t = m_transform * step;
    // Keep only the linear part; transformed() recomputes any translation.
    m_transform = QTransform(t.m11(), t.m12(), t.m21(), t.m22(), 0.0, 0.0);
}

void OrientationEdit::rotateClockwise() {
    compose(QTransform().rotate(90));
    m_rotation = (m_rotation + 90) % 360;
}

void OrientationEdit::flipHorizontal() {
    compose(QTransform().scale(-1, 1));
    m_flippedH = !m_flippedH;
}

void OrientationEdit::flipVertical() {
    compose(QTransform().scale(1, -1));
    m_flippedV = !m_flippedV;
}

bool OrientationEdit::isIdentity() const {
    return roundCoeff(m_transform.m11()) == 1 && roundCoeff(m_transform.m12()) == 0
        && roundCoeff(m_transform.m21()) == 0 && roundCoeff(m_transform.m22()) == 1;
}

QImage OrientationEdit::apply(const QImage &in) const {
    if (in.isNull() || isIdentity()) return in;
    return in.transformed(m_transform, Qt::SmoothTransformation);
}

std::unique_ptr<ImageEdit> OrientationEdit::clone() const {
    return std::make_unique<OrientationEdit>(*this);
}

QJsonObject OrientationEdit::toJson() const {
    QJsonObject o;
    o["type"] = type();
    // The transform is canonical for pixels; the counters are canonical for the
    // human-readable summary (orientation order is otherwise unrecoverable).
    o["m11"] = roundCoeff(m_transform.m11());
    o["m12"] = roundCoeff(m_transform.m12());
    o["m21"] = roundCoeff(m_transform.m21());
    o["m22"] = roundCoeff(m_transform.m22());
    o["rotation"] = m_rotation;
    o["flipH"] = m_flippedH;
    o["flipV"] = m_flippedV;
    return o;
}

void OrientationEdit::fromJson(const QJsonObject &obj) {
    const qreal m11 = obj.value("m11").toDouble(1.0);
    const qreal m12 = obj.value("m12").toDouble(0.0);
    const qreal m21 = obj.value("m21").toDouble(0.0);
    const qreal m22 = obj.value("m22").toDouble(1.0);
    m_transform = QTransform(m11, m12, m21, m22, 0.0, 0.0);
    m_rotation  = ((obj.value("rotation").toInt(0) % 360) + 360) % 360;
    m_flippedH  = obj.value("flipH").toBool(false);
    m_flippedV  = obj.value("flipV").toBool(false);
}

QString OrientationEdit::summary() const {
    QStringList parts;
    if (m_rotation != 0) parts << QStringLiteral("%1° rotation").arg(m_rotation);
    if (m_flippedH)      parts << QStringLiteral("H flip");
    if (m_flippedV)      parts << QStringLiteral("V flip");
    return parts.join(QStringLiteral(" · "));
}

// ---------------------------------------------------------------------------
// CropEdit
// ---------------------------------------------------------------------------
QRect CropEdit::toPixels(const QRectF &n, const QSize &size) {
    return QRect(qRound(n.x()      * size.width()),
                 qRound(n.y()      * size.height()),
                 qRound(n.width()  * size.width()),
                 qRound(n.height() * size.height()));
}

QRectF CropEdit::toNormalized(const QRectF &px, const QSize &size) {
    if (size.width() <= 0 || size.height() <= 0) return QRectF(0, 0, 1, 1);
    return QRectF(px.x()      / size.width(),
                  px.y()      / size.height(),
                  px.width()  / size.width(),
                  px.height() / size.height());
}

bool CropEdit::isFull() const {
    return m_rect.x() <= 0.0005 && m_rect.y() <= 0.0005
        && m_rect.width() >= 0.9995 && m_rect.height() >= 0.9995;
}

QImage CropEdit::apply(const QImage &in) const {
    if (in.isNull() || isFull()) return in;
    QRect px = toPixels(m_rect, in.size()).intersected(in.rect());
    if (px.isEmpty()) return in;
    return in.copy(px);
}

std::unique_ptr<ImageEdit> CropEdit::clone() const {
    return std::make_unique<CropEdit>(*this);
}

QJsonObject CropEdit::toJson() const {
    QJsonObject o;
    o["type"] = type();
    o["x"] = m_rect.x();
    o["y"] = m_rect.y();
    o["w"] = m_rect.width();
    o["h"] = m_rect.height();
    return o;
}

void CropEdit::fromJson(const QJsonObject &obj) {
    m_rect = QRectF(obj.value("x").toDouble(0.0),
                    obj.value("y").toDouble(0.0),
                    obj.value("w").toDouble(1.0),
                    obj.value("h").toDouble(1.0));
}

QString CropEdit::summary() const {
    return isFull() ? QString() : QStringLiteral("crop");
}

// ---------------------------------------------------------------------------
// AdjustEdit
// ---------------------------------------------------------------------------
std::unique_ptr<ImageEdit> AdjustEdit::clone() const {
    return std::make_unique<AdjustEdit>(*this);
}

QJsonObject AdjustEdit::toJson() const {
    QJsonObject o;
    o["type"]       = type();
    o["brightness"] = m_params.brightness;
    o["contrast"]   = m_params.contrast;
    o["exposure"]   = m_params.exposure;
    o["saturation"] = m_params.saturation;
    o["blacks"]     = m_params.blacks;
    o["whites"]     = m_params.whites;
    return o;
}

void AdjustEdit::fromJson(const QJsonObject &obj) {
    auto v = [&](const char *k) { return qBound(-100, obj.value(QLatin1String(k)).toInt(0), 100); };
    m_params.brightness = v("brightness");
    m_params.contrast   = v("contrast");
    m_params.exposure   = v("exposure");
    m_params.saturation = v("saturation");
    m_params.blacks     = v("blacks");
    m_params.whites     = v("whites");
}

QString AdjustEdit::summary() const {
    if (ImageAdjust::isNeutral(m_params)) return QString();
    QStringList parts;
    auto add = [&](const char *label, int v) {
        if (v != 0) parts << QStringLiteral("%1 %2%3").arg(QLatin1String(label),
                                                           v > 0 ? QStringLiteral("+") : QString(),
                                                           QString::number(v));
    };
    add("brightness", m_params.brightness);
    add("contrast",   m_params.contrast);
    add("exposure",   m_params.exposure);
    add("saturation", m_params.saturation);
    add("blacks",     m_params.blacks);
    add("whites",     m_params.whites);
    return parts.join(QStringLiteral(", "));
}

// ---------------------------------------------------------------------------
// ColorEdit
// ---------------------------------------------------------------------------
std::unique_ptr<ImageEdit> ColorEdit::clone() const {
    return std::make_unique<ColorEdit>(*this);
}

QJsonObject ColorEdit::toJson() const {
    QJsonObject o;
    o["type"]        = type();
    o["temperature"] = m_params.temperature;
    o["tint"]        = m_params.tint;
    o["red"]         = m_params.red;
    o["green"]       = m_params.green;
    o["blue"]        = m_params.blue;
    QJsonArray hues;
    for (int v : m_params.hues)
        hues.append(v);
    o["hues"] = hues;
    return o;
}

void ColorEdit::fromJson(const QJsonObject &obj) {
    auto v = [&](const char *k) { return qBound(-100, obj.value(QLatin1String(k)).toInt(0), 100); };
    m_params.temperature = v("temperature");
    m_params.tint        = v("tint");
    m_params.red         = v("red");
    m_params.green       = v("green");
    m_params.blue        = v("blue");
    const QJsonArray hues = obj.value(QLatin1String("hues")).toArray();
    for (int i = 0; i < 8; ++i)
        m_params.hues[i] = qBound(-100, hues.at(i).toInt(0), 100);
}

QString ColorEdit::summary() const {
    if (ImageAdjust::isNeutral(m_params)) return QString();
    QStringList parts;
    auto add = [&](const QString &label, int v) {
        if (v != 0) parts << QStringLiteral("%1 %2%3").arg(label,
                                                           v > 0 ? QStringLiteral("+") : QString(),
                                                           QString::number(v));
    };
    add(QStringLiteral("temp"),  m_params.temperature);
    add(QStringLiteral("tint"),  m_params.tint);
    add(QStringLiteral("red"),   m_params.red);
    add(QStringLiteral("green"), m_params.green);
    add(QStringLiteral("blue"),  m_params.blue);
    // Per-hue saturation bands, named and suffixed so they don't read like the
    // red/green/blue channel gains above.
    for (int i = 0; i < 8; ++i)
        add(QString::fromLatin1(ImageAdjust::hueBand(i).name) + QStringLiteral(" sat"), m_params.hues[i]);
    return parts.join(QStringLiteral(", "));
}

// ---------------------------------------------------------------------------
// BwEdit
// ---------------------------------------------------------------------------
std::unique_ptr<ImageEdit> BwEdit::clone() const {
    return std::make_unique<BwEdit>(*this);
}

QJsonObject BwEdit::toJson() const {
    QJsonObject o;
    o["type"]     = type();
    o["look"]     = static_cast<int>(m_params.look);
    o["reds"]     = m_params.reds;
    o["yellows"]  = m_params.yellows;
    o["greens"]   = m_params.greens;
    o["cyans"]    = m_params.cyans;
    o["blues"]    = m_params.blues;
    o["magentas"] = m_params.magentas;
    o["contrast"] = m_params.contrast;
    return o;
}

void BwEdit::fromJson(const QJsonObject &obj) {
    auto band = [&](const char *k) { return qBound(-100, obj.value(QLatin1String(k)).toInt(0), 100); };
    int look = obj.value("look").toInt(static_cast<int>(BwLook::Neutral));
    if (look < static_cast<int>(BwLook::Neutral) || look > static_cast<int>(BwLook::HighContrast))
        look = static_cast<int>(BwLook::Neutral);
    m_params.look     = static_cast<BwLook>(look);
    m_params.reds     = band("reds");
    m_params.yellows  = band("yellows");
    m_params.greens   = band("greens");
    m_params.cyans    = band("cyans");
    m_params.blues    = band("blues");
    m_params.magentas = band("magentas");
    m_params.contrast = band("contrast");
}
