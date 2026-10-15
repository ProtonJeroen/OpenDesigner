#include <QApplication>
#include <QCoreApplication>
#include <QByteArray>
#include "MainWindow.h"

int main(int argc, char* argv[]) {
#if defined(Q_OS_LINUX)
    // OCCT's Aspect_NeutralWindow currently embeds a native X11 drawable.
    // Qt Wayland winId() is not an X11 window, resulting in X11 BadWindow.
    // Prefer XWayland when available, without overriding an explicit choice.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
        !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
        !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
    }
#endif
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("OpenDesigner");
    QCoreApplication::setApplicationVersion("0.1.0");
    MainWindow window;
    window.show();
    return app.exec();
}
