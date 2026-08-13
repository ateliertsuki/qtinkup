#include <QApplication>

#include "mainwindow.h"

// Single source of truth for the application version
// can be read elsewhere through QApplication::applicationVersion()
static const QString kVersion = QStringLiteral("1.0");

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QTinkup"));
    QApplication::setApplicationVersion(kVersion);

    MainWindow window;
    window.show();

    return app.exec();
}
