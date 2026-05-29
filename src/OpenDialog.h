#pragma once
#include <QString>

class QWidget;

// Shows the system open dialog allowing the user to pick an image file or a folder.
// Returns the selected path (file or directory), or an empty string if cancelled.
QString showOpenDialog(QWidget *parent, const QString &startDir);
