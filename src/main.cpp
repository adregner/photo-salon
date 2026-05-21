#include <QApplication>
#include <QTextStream>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        QTextStream err(stderr);
        err << "Usage: photo-salon <image.jpg>\n";
        return 1;
    }

    QApplication app(argc, argv);
    MainWindow window(argv[1]);
    window.show();
    return app.exec();
}
