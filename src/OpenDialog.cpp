#include "OpenDialog.h"
#include "ImageFormats.h"
#include <QDir>
#include <QFileDialog>
#include <QWidget>

// Linux / Windows: native system file dialog (files only).
// Folder opening is available via CLI argument on these platforms.
QString showOpenDialog(QWidget *parent, const QString &startDir) {
    return QFileDialog::getOpenFileName(
        parent,
        QStringLiteral("Open Image"),
        startDir.isEmpty() ? QDir::homePath() : startDir,
        supportedFileFilter());
}
