#include <QApplication>
#include <QTextStream>
#include "ImageFormats.h"
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        QTextStream err(stderr);
        err << "Usage: photo-salon <image.jpg|folder>\n";
        return 1;
    }

    QApplication app(argc, argv);

    QString error;
    QString path = resolveImagePath(QString::fromLocal8Bit(argv[1]), &error);
    if (path.isEmpty()) {
        QTextStream err(stderr);
        err << error << "\n";
        return 1;
    }

    MainWindow window(path);
    window.show();
    return app.exec();
}
