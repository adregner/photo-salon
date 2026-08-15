#pragma once
#include <QString>
#include <QStringList>

// Returns glob patterns for all image formats Qt6 supports, e.g. "*.png", "*.jpg".
// Suitable for use as QDir::entryList() nameFilters.
QStringList supportedExtensions();

// Returns a QFileDialog-compatible filter string for all supported image formats.
QString supportedFileFilter();

// Returns a QFileDialog filter string covering every format Qt can *write*
// (QImageWriter::supportedImageFormats()), for the Save dialog: a combined
// "All Images" entry first, then one entry per format, then "All Files (*)".
QString supportedSaveFilter();

// Resolves a CLI argument to an absolute image file path.
// If arg is a directory, returns the first image file (sorted by name).
// If arg is a file, returns its absolute path.
// Returns an empty string and sets *error on failure (non-null error pointer only).
QString resolveImagePath(const QString &arg, QString *error = nullptr);

// If path's base file name ends with "_pair" and exactly one other image in the
// same folder also has a base name ending with "_pair", returns that other
// image's absolute path (the auto-pair partner). Otherwise (not a "_pair" file,
// no partner, or more than two "_pair" images in the folder — ambiguous) returns
// an empty string.
QString findPairPartner(const QString &path);
