// src/ExternalLauncher.h
#pragma once
#include <QString>

class QWidget;

// Returns the path to Photoshop if detected on this system; empty string if not found.
QString findPhotoshop();

// Opens filePath in Photoshop if available, otherwise uses a remembered or
// user-selected fallback app. Persists the chosen app via QSettings.
// Pass forcePick=true to always show the picker regardless of any saved setting.
// Returns false only if the user cancels the "Open in..." picker dialog.
bool openInExternalApp(const QString &filePath, QWidget *parent, bool forcePick = false);
