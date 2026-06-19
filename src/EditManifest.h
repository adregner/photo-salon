#pragma once
#include "ImageEdit.h"
#include <QJsonObject>
#include <QString>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// EditManifest — the single, canonical record of every modification currently
// applied to an image, and the order they apply in. It is the source of truth:
// every editing module reads its settings from (its slice of) the manifest, and
// the on-screen image is produced by applying the manifest's edits in order.
//
// The manifest holds an edit of a given kind only while that edit is active, so
// its contents are exactly "what is applied right now". A manifest is persisted
// per image path, so reopening the same file re-applies the same edits.
// ---------------------------------------------------------------------------
class EditManifest {
public:
    EditManifest() = default;
    EditManifest(const EditManifest &other) { *this = other; }
    EditManifest &operator=(const EditManifest &other);
    EditManifest(EditManifest &&) noexcept = default;
    EditManifest &operator=(EditManifest &&) noexcept = default;

    bool isEmpty() const { return m_edits.empty(); }
    const std::vector<std::unique_ptr<ImageEdit>> &edits() const { return m_edits; }

    // Typed accessors. The const/non-const pair returns nullptr when the edit of
    // that kind is not currently applied.
    OrientationEdit *orientation();
    CropEdit        *crop();
    BwEdit          *bw();
    const OrientationEdit *orientation() const;
    const CropEdit        *crop() const;
    const BwEdit          *bw() const;

    // Get-or-create the edit of a kind, inserted at its canonical pipeline
    // position. Use these to mutate an edit's settings.
    OrientationEdit &ensureOrientation();
    CropEdit        &ensureCrop();
    BwEdit          &ensureBw();

    // Drop an edit (so it is no longer applied).
    void removeOrientation();
    void removeCrop();
    void removeBw();

    // Apply every edit, in order, to the buffer — the canonical render path used
    // when reconstructing an image from disk.
    QImage render(const QImage &in) const;

    // "90° rotation · crop · B&W" — the edits' summaries joined in order.
    QString summary() const;

    // JSON (de)serialization for the settings store.
    QJsonObject toJson() const;
    static EditManifest fromJson(const QJsonObject &obj);

    // Persistence, keyed by the image's absolute path. Saving an empty manifest
    // clears any stored entry. A path that is empty is ignored.
    void saveFor(const QString &imagePath) const;
    static EditManifest loadFor(const QString &imagePath);

private:
    ImageEdit *find(const QString &type);
    const ImageEdit *find(const QString &type) const;
    ImageEdit &ensure(const QString &type);
    void remove(const QString &type);

    std::vector<std::unique_ptr<ImageEdit>> m_edits;  // kept in canonical order
};
