#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("gnumon");
    app.setOrganizationName("gnumon");
    app.setDesktopFileName("gnumon");
    app.setApplicationDisplayName("gnumon");

    gnumon::gui::MainWindow window;
    window.show();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--metrics" || arg == "--full" || arg == "--inspector") {
            window.OnOpenFullMetrics();
            break;
        }
    }

    return app.exec();
}
