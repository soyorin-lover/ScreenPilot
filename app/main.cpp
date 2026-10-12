#include "RecordingController.h"
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSaveFile>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <memory>

int main(int argc, char **argv) {
  qputenv("QT_MEDIA_BACKEND", "ffmpeg");
  QQuickStyle::setStyle("Basic");
  QGuiApplication app(argc, argv);
  app.setApplicationName("ScreenPilot");
  app.setOrganizationName("ScreenPilot");
  app.setApplicationVersion("0.1.0-dev");
  QCommandLineParser cli;
  cli.addHelpOption();
  cli.addVersionOption();
  cli.addOption({"smoke-test",
                 "Record only a generated test window, decode, then exit.",
                 "directory"});
  cli.process(app);
  RecordingController recorder;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("recorder", &recorder);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("ScreenPilotApp", "Main");
  if (engine.rootObjects().isEmpty())
    return 1;

  // One optional end-to-end check exercises the actual product controller.
  // It captures only this generated window, never the user's desktop.
  const QString smokeDirectory = cli.value("smoke-test");
  std::unique_ptr<QObject> target;
  QMediaPlayer decoder;
  QVideoSink decoded;
  int decodedFrames = 0;
  bool finished = false;
  if (!smokeDirectory.isEmpty()) {
    QDir output(smokeDirectory);
    if (output.exists() &&
        !output
             .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
             .isEmpty())
      return 2;
    if (!QDir().mkpath(smokeDirectory))
      return 2;
    recorder.setOutputDirectory(output.absolutePath());
    QQmlComponent pattern(&engine);
    pattern.setData(R"(
            import QtQuick
            Window {
                visible: true; width: 640; height: 360; title: "Recording check"
                color: "#244836"
                Rectangle {
                    width: 80; height: 80; y: 120; color: "#f1b55a"
                    SequentialAnimation on x {
                        loops: Animation.Infinite
                        NumberAnimation { from: 20; to: 540; duration: 900 }
                        NumberAnimation { from: 540; to: 20; duration: 900 }
                    }
                }
                Text { anchors.centerIn: parent; text: "ScreenPilot recording check"; color: "white"; font.pixelSize: 24 }
            }
        )",
                    QUrl("qrc:/SmokeTarget.qml"));
    target.reset(pattern.create());
    if (!target)
      return 1;
    decoder.setVideoSink(&decoded);
    auto finish = [&, output](bool success, const QString &reason) {
      if (finished)
        return;
      finished = true;
      QJsonObject result{
          {"success", success},
          {"reason", reason},
          {"file", recorder.lastRecording().toLocalFile()},
          {"file_bytes",
           double(QFileInfo(recorder.lastRecording().toLocalFile()).size())},
          {"decoded_frames", decodedFrames},
          {"decoded_duration_ms", double(decoder.duration())},
          {"scope", "Generated own window only; no microphone/system audio, "
                    "editing or frame-rate acceptance"}};
      QSaveFile json(output.absoluteFilePath("smoke-result.json"));
      bool saved = json.open(QIODevice::WriteOnly);
      const auto bytes = QJsonDocument(result).toJson();
      saved = saved && json.write(bytes) == bytes.size() && json.commit();
      QCoreApplication::exit(success && saved ? 0 : 1);
    };
    // Value captures retain the completion handler after this block exits.
    QObject::connect(&decoded, &QVideoSink::videoFrameChanged, &app,
                     [&, finish](const QVideoFrame &frame) {
                       if (frame.isValid())
                         ++decodedFrames;
                       if (decodedFrames >= 12 && decoder.duration() >= 1000)
                         finish(true, "Saved MP4 opened and decoded");
                     });
    QObject::connect(
        &recorder, &RecordingController::changed, &app, [&, finish] {
          if (!recorder.busy() && !recorder.lastRecording().isEmpty() &&
              decoder.source().isEmpty()) {
            decoder.setSource(recorder.lastRecording());
            decoder.play();
          }
        });
    QTimer::singleShot(1000, &app, [&, finish, output] {
      auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
      if (window)
        window->grabWindow().save(output.absoluteFilePath("application.png"));
      recorder.refreshSources();
      const auto sources = recorder.sources();
      int selected = -1;
      for (int i = 0; i < sources.size(); ++i)
        if (sources[i].toMap().value("label").toString().contains(
                "Recording check"))
          selected = i;
      if (selected < 0) {
        finish(false, "Generated target not found");
        return;
      }
      recorder.setSelectedSource(selected);
      recorder.start();
      QTimer::singleShot(3500, &app, [&] { recorder.stop(); });
    });
    QTimer::singleShot(18000, &app, [&, finish] {
      finish(false, "Recording/decode timeout: " + recorder.error());
    });
  }
  return app.exec();
}
