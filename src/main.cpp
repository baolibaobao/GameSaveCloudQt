#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "SteamManager.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    SteamManager steamManager;

    engine.rootContext()->setContextProperty(QStringLiteral("steamManager"), &steamManager);
    engine.loadFromModule(QStringLiteral("GameSaveCloud"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
