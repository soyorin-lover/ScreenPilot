#include "AppRuntime.h"
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(ScreenPilotAppPlugin)

int main(int argc, char **argv) {
  AppRuntime::prepareProcess();
  QGuiApplication app(argc, argv);
  app.setApplicationName("ScreenPilot");
  app.setOrganizationName("ScreenPilot");
  app.setApplicationVersion("0.1.0-dev");
  QCommandLineParser cli;
  cli.addHelpOption();
  cli.addVersionOption();
  cli.process(app);
  AppRuntime runtime({});
  if (!runtime.load())
    return 1;
  return app.exec();
}
