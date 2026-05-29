#pragma once
#include <QString>
#include <QStringList>

// Returns glob patterns for all image formats Qt6 supports, e.g. "*.png", "*.jpg".
// Suitable for use as QDir::entryList() nameFilters.
QStringList supportedExtensions();

// Returns a QFileDialog-compatible filter string for all supported image formats.
QString supportedFileFilter();

// Resolves a CLI argument to an absolute image file path.
// If arg is a directory, returns the first image file (sorted by name).
// If arg is a file, returns its absolute path.
// Returns an empty string and sets *error on failure (non-null error pointer only).
QString resolveImagePath(const QString &arg, QString *error = nullptr);
