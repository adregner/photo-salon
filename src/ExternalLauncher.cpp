// src/ExternalLauncher.cpp
#include "ExternalLauncher.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QWidget>

QString findPhotoshop() {
#ifdef Q_OS_WIN
    {
        QSettings reg(
            R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\Photoshop.exe)",
            QSettings::NativeFormat);
        QString path = reg.value(QStringLiteral("Default")).toString();
        if (!path.isEmpty() && QFileInfo::exists(path))
            return path;
    }
    QDir adobeDir(R"(C:\Program Files\Adobe)");
    for (const QFileInfo &entry : adobeDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString candidate = entry.absoluteFilePath() + "/Photoshop.exe";
        if (QFileInfo::exists(candidate))
            return candidate;
    }
#endif
    return {};
}

static QString pickApp(QWidget *parent) {
#ifdef Q_OS_WIN
    return QFileDialog::getOpenFileName(
        parent,
        QStringLiteral("Choose Application"),
        QStringLiteral(R"(C:\Program Files)"),
        QStringLiteral("Executable Files (*.exe)"));
#else
    return QFileDialog::getOpenFileName(
        parent,
        QStringLiteral("Choose Application"),
        QStringLiteral("/usr/bin"));
#endif
}

bool openInExternalApp(const QString &filePath, QWidget *parent, bool forcePick) {
    QSettings settings;
    QString appPath;

    if (!forcePick)
        appPath = settings.value(QStringLiteral("externalEditor/appPath")).toString();

    if (forcePick || appPath.isEmpty() || !QFileInfo::exists(appPath)) {
        if (!forcePick)
            appPath = findPhotoshop();
        if (appPath.isEmpty()) {
            appPath = pickApp(parent);
            if (appPath.isEmpty()) return false;
        }
        settings.setValue(QStringLiteral("externalEditor/appPath"), appPath);
    }

    QProcess::startDetached(appPath, {filePath});
    return true;
}
