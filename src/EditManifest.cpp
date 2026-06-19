#include "EditManifest.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStringList>

EditManifest &EditManifest::operator=(const EditManifest &other) {
    if (this == &other) return *this;
    m_edits.clear();
    m_edits.reserve(other.m_edits.size());
    for (const auto &e : other.m_edits)
        m_edits.push_back(e->clone());
    return *this;
}

// ---------------------------------------------------------------------------
// Lookup / mutation (keeping the list in canonical pipeline order)
// ---------------------------------------------------------------------------
ImageEdit *EditManifest::find(const QString &type) {
    for (auto &e : m_edits)
        if (e->type() == type) return e.get();
    return nullptr;
}

const ImageEdit *EditManifest::find(const QString &type) const {
    for (const auto &e : m_edits)
        if (e->type() == type) return e.get();
    return nullptr;
}

ImageEdit &EditManifest::ensure(const QString &type) {
    if (ImageEdit *existing = find(type)) return *existing;

    auto edit = makeEdit(type);
    Q_ASSERT(edit);
    const int order = editOrderIndex(type);
    // Insert before the first edit that sorts after this one, so the vector stays
    // ordered disk → orientation → crop → B&W.
    auto pos = m_edits.begin();
    while (pos != m_edits.end() && editOrderIndex((*pos)->type()) <= order)
        ++pos;
    pos = m_edits.insert(pos, std::move(edit));
    return **pos;
}

void EditManifest::remove(const QString &type) {
    for (auto it = m_edits.begin(); it != m_edits.end(); ++it) {
        if ((*it)->type() == type) { m_edits.erase(it); return; }
    }
}

OrientationEdit *EditManifest::orientation() { return static_cast<OrientationEdit *>(find(QStringLiteral("orientation"))); }
CropEdit        *EditManifest::crop()        { return static_cast<CropEdit *>(find(QStringLiteral("crop"))); }
BwEdit          *EditManifest::bw()          { return static_cast<BwEdit *>(find(QStringLiteral("bw"))); }
const OrientationEdit *EditManifest::orientation() const { return static_cast<const OrientationEdit *>(find(QStringLiteral("orientation"))); }
const CropEdit        *EditManifest::crop() const        { return static_cast<const CropEdit *>(find(QStringLiteral("crop"))); }
const BwEdit          *EditManifest::bw() const          { return static_cast<const BwEdit *>(find(QStringLiteral("bw"))); }

OrientationEdit &EditManifest::ensureOrientation() { return static_cast<OrientationEdit &>(ensure(QStringLiteral("orientation"))); }
CropEdit        &EditManifest::ensureCrop()        { return static_cast<CropEdit &>(ensure(QStringLiteral("crop"))); }
BwEdit          &EditManifest::ensureBw()          { return static_cast<BwEdit &>(ensure(QStringLiteral("bw"))); }

void EditManifest::removeOrientation() { remove(QStringLiteral("orientation")); }
void EditManifest::removeCrop()        { remove(QStringLiteral("crop")); }
void EditManifest::removeBw()          { remove(QStringLiteral("bw")); }

// ---------------------------------------------------------------------------
// Render / summary
// ---------------------------------------------------------------------------
QImage EditManifest::render(const QImage &in) const {
    QImage out = in;
    for (const auto &e : m_edits)
        out = e->apply(out);
    return out;
}

QString EditManifest::summary() const {
    QStringList parts;
    for (const auto &e : m_edits) {
        QString s = e->summary();
        if (!s.isEmpty()) parts << s;
    }
    return parts.join(QStringLiteral(" · "));
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------
QJsonObject EditManifest::toJson() const {
    QJsonArray arr;
    for (const auto &e : m_edits)
        arr.append(e->toJson());
    QJsonObject o;
    o["version"] = 1;
    o["edits"]   = arr;
    return o;
}

EditManifest EditManifest::fromJson(const QJsonObject &obj) {
    EditManifest m;
    const QJsonArray arr = obj.value("edits").toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject eo = v.toObject();
        auto edit = makeEdit(eo.value("type").toString());
        if (!edit) continue;
        edit->fromJson(eo);
        m.m_edits.push_back(std::move(edit));
    }
    return m;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
namespace {
// QSettings keys can't safely contain a file path ('/' is a group separator and
// paths vary in case/encoding), so key each manifest by a hash of the absolute
// path under a dedicated group.
QString manifestKey(const QString &imagePath) {
    const QString abs = QFileInfo(imagePath).absoluteFilePath();
    const QByteArray h = QCryptographicHash::hash(abs.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("manifests/") + QString::fromLatin1(h);
}
}  // namespace

void EditManifest::saveFor(const QString &imagePath) const {
    if (imagePath.isEmpty()) return;
    QSettings settings;
    const QString key = manifestKey(imagePath);
    if (isEmpty()) {
        settings.remove(key);
        return;
    }
    const QByteArray json = QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
    settings.setValue(key, QString::fromUtf8(json));
}

EditManifest EditManifest::loadFor(const QString &imagePath) {
    if (imagePath.isEmpty()) return {};
    QSettings settings;
    const QString stored = settings.value(manifestKey(imagePath)).toString();
    if (stored.isEmpty()) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(stored.toUtf8());
    if (!doc.isObject()) return {};
    return fromJson(doc.object());
}
