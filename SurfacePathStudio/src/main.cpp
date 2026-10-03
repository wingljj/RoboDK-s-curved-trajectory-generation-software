#include "app/MainWindow.h"
#include "app/Branding.h"
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>
#include <iostream>

int main(int argc, char** argv) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    app.setApplicationName("SurfacePathStudio");
    app.setApplicationDisplayName(sps::branding::productName());
    app.setOrganizationName(sps::branding::organizationName());
    app.setApplicationVersion(QString::fromLatin1(SPS_APP_VERSION));
    app.setWindowIcon(QIcon(sps::branding::iconPath()));
    try {
        sps::MainWindow window;
        window.show();
        const auto args = app.arguments();
        if (args.size() > 1 && args[1] == "--smoke-test") {
            const QString output = args.size() > 2 ? args[2] : "smoke";
            QTimer::singleShot(800, &window, [&window, output] { window.runSmokeTest(output); });
        } else if (args.size() > 1) {
            QTimer::singleShot(200, &window, [&window, args] { window.openStepFile(args[1]); });
        }
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
