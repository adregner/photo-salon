#include <ctime>
#include <cstdio>
#include <string>
#include <QApplication>
#include <QDir>
#include <QTextStream>
#include "version.h"
#include "ImageFormats.h"
#include "MainWindow.h"
#include "OpenDialog.h"

static void printVersion() {
    char buf[64];
    time_t epoch = static_cast<time_t>(PHOTO_SALON_BUILD_EPOCH);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", localtime(&epoch));

    const char *tag    = PHOTO_SALON_RELEASE_TAG;
    const char *commit = PHOTO_SALON_GIT_COMMIT;

    if (tag && *tag)
        printf("%s (%s, built %s)\n", tag, commit, buf);
    else if (commit && *commit)
        printf("dev-%s (built %s)\n", commit, buf);
    else
        printf("dev (built %s)\n", buf);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--version") {
            printVersion();
            return 0;
        }
    }

    QApplication app(argc, argv);

    QString path;

    if (argc >= 2) {
        QString error;
        path = resolveImagePath(QString::fromLocal8Bit(argv[1]), &error);
        if (path.isEmpty()) {
            QTextStream err(stderr);
            err << error << "\n";
            return 1;
        }
    } else {
        QString selected = showOpenDialog(nullptr, QDir::homePath());
        if (!selected.isEmpty()) {
            QString error;
            path = resolveImagePath(selected, &error);
            if (path.isEmpty()) {
                QTextStream err(stderr);
                err << error << "\n";
                return 1;
            }
        }
        // If dialog was canceled, path stays empty → open in idle state
    }

    MainWindow window(path);
    window.show();
    return app.exec();
}
