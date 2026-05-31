#pragma once
#include "ExifReader.h"
#include <QNetworkAccessManager>
#include <QStringList>
#include <QWidget>
#include <optional>

class QPaintEvent;

// Displays EXIF metadata as a semi-transparent overlay.
//
// Layout is driven by a template: a QStringList where each entry is either an
// empty string (→ blank separator line) or a format string containing
// {FieldName} placeholders that are substituted from the ExifData map.
//
// Rendering rules:
//   • Literal text with no placeholders is always rendered as-is.
//   • A line whose placeholders ALL resolve to empty strings is silently
//     omitted, so the overlay stays clean when fields are absent.
//   • The result of each line is trimmed before display, so stray spaces
//     from missing middle fields are removed.
//
// Call setTemplate() to fully replace the layout; defaultTemplate() returns
// the built-in layout as a starting point for customization.
class ExifOverlay : public QWidget {
public:
    explicit ExifOverlay(QWidget *parent = nullptr);

    void setData(const ExifReader::ExifData &data);
    void setTemplate(const QStringList &lines);

    static QStringList defaultTemplate();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    // Returns the rendered string for one template line, or nullopt to skip it.
    std::optional<QString> renderLine(const QString &tmpl) const;

    // Returns all rendered, non-skipped lines (blank separators included).
    QStringList renderLines() const;

    void resolveLocation(double lat, double lon);

    ExifReader::ExifData    m_data;
    QStringList             m_template;
    QNetworkAccessManager  *m_nam = nullptr;
    QString                 m_pendingGeoKey;
};
