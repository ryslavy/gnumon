#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("gnumon");
    app.setOrganizationName("gnumon");

    gnumon::gui::MainWindow window;
    window.show();

    return app.exec();
}
