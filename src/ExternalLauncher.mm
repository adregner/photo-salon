// src/ExternalLauncher.mm
#include "ExternalLauncher.h"
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <QFileInfo>
#include <QSettings>
#include <QWidget>

QString findPhotoshop() {
    NSURL *url = [[NSWorkspace sharedWorkspace]
        URLForApplicationWithBundleIdentifier:@"com.adobe.Photoshop"];
    return url ? QString::fromNSString(url.path) : QString{};
}

static QString pickApp(QWidget *) {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"Choose Application";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.treatsFilePackagesAsDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedContentTypes = @[UTTypeApplicationBundle];
    panel.directoryURL = [NSURL fileURLWithPath:@"/Applications"];
    if ([panel runModal] != NSModalResponseOK) return {};
    NSURL *url = panel.URLs.firstObject;
    return url ? QString::fromNSString(url.path) : QString{};
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

    NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
    NSURL *appURL  = [NSURL fileURLWithPath:appPath.toNSString()];
    NSURL *fileURL = [NSURL fileURLWithPath:filePath.toNSString()];
    [[NSWorkspace sharedWorkspace]
        openURLs:@[fileURL]
        withApplicationAtURL:appURL
        configuration:config
        completionHandler:nil];
    return true;
}
