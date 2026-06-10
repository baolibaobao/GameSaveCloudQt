#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "SteamManager.h"
#include "tray/TrayController.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/assets/icons/GameSaveCloudIcon.png")));

    QQmlApplicationEngine engine;
    SteamManager steamManager;
    TrayController trayController;

    engine.rootContext()->setContextProperty(QStringLiteral("steamManager"), &steamManager);
    engine.rootContext()->setContextProperty(QStringLiteral("trayController"), &trayController);
    engine.loadFromModule(QStringLiteral("GameSaveCloud"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
