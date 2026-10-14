#include "AppRuntime.h"
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(ScreenPilotAppPlugin)

int main(int argc, char **argv) {
  AppRuntime::prepareProcess();
  QGuiApplication app(argc, argv);
  // SCREENPILOT_DEMO=actor|camera: filming the app with itself. Both use
  // separate settings, recent projects and recovery data ("ScreenPilot-Demo");
  // the actor's recording bar stays capturable (see RecordingController) and
  // the camera instance is titled apart so it can pick the actor's window.
  const QString demo = qEnvironmentVariable("SCREENPILOT_DEMO");
  app.setApplicationName(demo.isEmpty() ? "ScreenPilot" : "ScreenPilot-Demo");
  app.setOrganizationName("ScreenPilot");
  app.setApplicationVersion("0.1.0-dev");
  QCommandLineParser cli;
  cli.addHelpOption();
  cli.addVersionOption();
  cli.process(app);
  AppRuntime runtime({});
  if (!runtime.load())
    return 1;
  if (demo == "camera")
    runtime.root()->setProperty("title", QStringLiteral("ScreenPilot · 录制机位"));
  if (!demo.isEmpty() && qEnvironmentVariableIsSet("SCREENPILOT_DEMO_OUTPUT"))
    runtime.recorder.setOutputDirectory(qEnvironmentVariable("SCREENPILOT_DEMO_OUTPUT"));
  return app.exec();
}
