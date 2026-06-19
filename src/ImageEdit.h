#pragma once
#include "BwConverter.h"
#include <QImage>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <memory>

// ---------------------------------------------------------------------------
// ImageEdit — the common interface every image-modifying module implements.
//
// An edit owns the *settings* for one kind of modification (its slice of the
// EditManifest) and knows how to apply those settings to an in-memory image
// buffer. apply() is pure: given the same input and settings it always returns
// the same output and never touches shared state, so a manifest can re-render an
// image straight from disk in a deterministic, ordered pass — including off the
// GUI thread (which is why the buffer is a QImage, not a QPixmap).
// ---------------------------------------------------------------------------
class ImageEdit {
public:
    virtual ~ImageEdit() = default;

    // Stable identifier — also the JSON key the edit serializes under and how
    // EditManifest keeps its edits in canonical pipeline order.
    virtual QString type() const = 0;

    // Apply this edit to the buffer and return the result.
    virtual QImage apply(const QImage &in) const = 0;

    // Deep copy (EditManifest is value-semantic).
    virtual std::unique_ptr<ImageEdit> clone() const = 0;

    // Round-trip the edit's parameters through JSON for the settings store.
    // toJson() always writes a "type" field so the manifest array is
    // self-describing.
    virtual QJsonObject toJson() const = 0;
    virtual void fromJson(const QJsonObject &obj) = 0;

    // Short tag for the metadata edit-state line ("90° rotation", "crop",
    // "B&W"); empty to omit.
    virtual QString summary() const = 0;
};

// Canonical pipeline position of an edit type (disk → orientation → crop → B&W).
// EditManifest uses this to keep its ordered list stable; lower applies first.
int editOrderIndex(const QString &type);

// Construct an empty edit of the given type (for deserialization). Null if the
// type is unknown.
std::unique_ptr<ImageEdit> makeEdit(const QString &type);

// ---------------------------------------------------------------------------
// OrientationEdit — lossless rotation/flip (the dihedral group). Stores the net
// linear transform so repeated rotate/flip presses compose exactly the way they
// did on screen, and a reopened image reproduces the same orientation. The
// rotation/flip counters are descriptive only: they drive the human-readable
// summary, matching what the user last saw.
// ---------------------------------------------------------------------------
class OrientationEdit : public ImageEdit {
public:
    QString type() const override { return QStringLiteral("orientation"); }
    QImage  apply(const QImage &in) const override;
    std::unique_ptr<ImageEdit> clone() const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &obj) override;
    QString summary() const override;

    // Compose an incremental step on top of the current orientation, in the same
    // order the user applied it (current first, then the new step).
    void rotateClockwise();
    void flipHorizontal();
    void flipVertical();

    QTransform transform() const { return m_transform; }
    // True when the net transform is the identity (nothing to apply).
    bool isIdentity() const;

private:
    void compose(const QTransform &step);  // m_transform = m_transform * step

    QTransform m_transform;       // net linear transform (no translation)
    int  m_rotation = 0;          // 0/90/180/270, descriptive (summary only)
    bool m_flippedH = false;      // descriptive (summary only)
    bool m_flippedV = false;      // descriptive (summary only)
};

// ---------------------------------------------------------------------------
// CropEdit — a rectangular crop stored in *normalized* coordinates (fractions of
// the incoming buffer, 0..1) so it is resolution-independent and survives a
// reopen even if the decoded size were to differ. apply() copies that region out
// of the buffer it is given (the oriented image).
// ---------------------------------------------------------------------------
class CropEdit : public ImageEdit {
public:
    QString type() const override { return QStringLiteral("crop"); }
    QImage  apply(const QImage &in) const override;
    std::unique_ptr<ImageEdit> clone() const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &obj) override;
    QString summary() const override;

    QRectF rect() const { return m_rect; }            // normalized [0,1]
    void   setRect(const QRectF &r) { m_rect = r; }
    // True when the rect covers (essentially) the whole image — i.e. no crop.
    bool   isFull() const;

    // Map a normalized rect to pixels of a given size, and back.
    static QRect  toPixels(const QRectF &normalized, const QSize &size);
    static QRectF toNormalized(const QRectF &pixels, const QSize &size);

private:
    QRectF m_rect = QRectF(0.0, 0.0, 1.0, 1.0);
};

// ---------------------------------------------------------------------------
// BwEdit — non-destructive black-&-white conversion. Wraps BwParams and defers
// to BwConverter, making B&W conform to the common edit interface.
// ---------------------------------------------------------------------------
class BwEdit : public ImageEdit {
public:
    QString type() const override { return QStringLiteral("bw"); }
    QImage  apply(const QImage &in) const override { return BwConverter::convert(in, m_params); }
    std::unique_ptr<ImageEdit> clone() const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &obj) override;
    QString summary() const override { return QStringLiteral("B&W"); }

    BwParams params() const { return m_params; }
    void     setParams(const BwParams &p) { m_params = p; }

private:
    BwParams m_params;
};
