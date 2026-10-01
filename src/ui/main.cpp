#include <QApplication>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VGO"));

    MainWindow window;
    window.show();
    return app.exec();
}
