#pragma once
#include <QString>
#include <QStringList>

// Number of megabytes a single decoded image is allowed to occupy. See
// raiseImageAllocationLimit().
constexpr int kImageAllocationLimitMb = 2048;

// Raises Qt's cap on the size of a decoded image. Call once at startup, before
// anything reads an image. Without it, Qt refuses full-resolution files from
// current cameras — see the comment on the definition.
void raiseImageAllocationLimit();

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
