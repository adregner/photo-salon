#include <QApplication>
#include <QDir>
#include <QTextStream>
#include "ImageFormats.h"
#include "MainWindow.h"
#include "OpenDialog.h"

int main(int argc, char *argv[]) {
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
