#include "OpenDialog.h"
#include "ImageFormats.h"
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <QWidget>

// macOS: NSOpenPanel with canChooseFiles and canChooseDirectories both enabled,
// giving native look-and-feel while accepting either a file or a folder.
QString showOpenDialog(QWidget *parent, const QString &startDir) {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"Open Image or Folder";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = NO;
    panel.treatsFilePackagesAsDirectories = NO;

    // Build allowed content types from supported extensions.
    NSMutableArray<UTType *> *types = [NSMutableArray array];
    for (const QString &ext : supportedExtensions()) {
        NSString *nsExt = ext.mid(2).toNSString(); // strip leading "*."
        UTType *type = [UTType typeWithFilenameExtension:nsExt];
        if (type)
            [types addObject:type];
    }
    if (types.count > 0)
        panel.allowedContentTypes = types;

    if (!startDir.isEmpty())
        panel.directoryURL = [NSURL fileURLWithPath:startDir.toNSString()];

    if ([panel runModal] != NSModalResponseOK)
        return {};

    NSURL *url = panel.URLs.firstObject;
    return url ? QString::fromNSString(url.path) : QString{};
}
