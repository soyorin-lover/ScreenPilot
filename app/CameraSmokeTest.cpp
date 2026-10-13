#include "CameraSmokeTest.h"
#include "CameraController.h"
#include "CameraOverlay.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QPainter>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

int runCameraSmokeTest(QGuiApplication &app, const QString &directory) {
  if (!QDir().mkpath(directory))
    return 2;
  QImage fixture(80, 80, QImage::Format_RGB32);
  fixture.fill(Qt::red);
  {
    QPainter p(&fixture);
    p.fillRect(40, 0, 40, 80, Qt::blue);
  }
  QImage canvas(400, 300, QImage::Format_RGB32);
  canvas.fill(Qt::white);
  auto settings = CameraOverlay::defaults();
  settings["shape"] = "circle";
  settings["shadow"] = false;
  settings["x"] = .1;
  settings["y"] = .1;
  settings["width"] = .3;
  {
    QPainter p(&canvas);
    CameraOverlay::paint(p, canvas.size(), fixture, settings);
  }
  const bool synthetic = canvas.pixelColor(75, 90) == QColor(Qt::blue) &&
                         canvas.pixelColor(125, 90) == QColor(Qt::red) &&
                         canvas.pixelColor(40, 30) == QColor(Qt::white);
  canvas.save(QDir(directory).filePath("camera-overlay-fixture.png"));
  CameraController controller;
  QMediaPlayer player;
  QVideoSink sink;
  player.setVideoSink(&sink);
  QTimer timeout;
  timeout.setSingleShot(true);
  bool finished = false, started = false;
  auto complete = [&](const QString &hardware, const QString &detail,
                      int code) {
    if (finished)
      return;
    finished = true;
    timeout.stop();
    QFile report(QDir(directory).filePath("camera-smoke.json"));
    if (report.open(QIODevice::WriteOnly))
      report.write(
          QJsonDocument(
              QJsonObject{{"syntheticOverlay", synthetic ? "passed" : "failed"},
                          {"hardware", hardware},
                          {"detail", detail},
                          {"offsetSeconds", controller.offset()}})
              .toJson());
    app.exit(code);
  };
  QObject::connect(&timeout, &QTimer::timeout, &app, [&] {
    controller.stopRecording();
    complete("failed", "Camera capture or decode timed out", 1);
  });
  QObject::connect(
      &controller, &CameraController::recordingFailed, &app,
      [&](const QString &) {
        complete(
            "failed",
            "Camera capture failed; inspect application camera status locally",
            1);
      });
  QObject::connect(&controller, &CameraController::recordingFinished, &app,
                   [&](QUrl source, double) {
                     player.setSource(source);
                     player.play();
                   });
  QObject::connect(
      &sink, &QVideoSink::videoFrameChanged, &app,
      [&](const QVideoFrame &frame) {
        if (frame.isValid() && !frame.toImage().isNull()) {
          player.stop();
          complete("passed", "Three-second camera sidecar recorded and decoded",
                   synthetic ? 0 : 1);
        }
      });
  QObject::connect(&controller, &CameraController::changed, &app, [&] {
    if (finished || started || !controller.enabled() ||
        controller.status() != QStringLiteral("摄像头已就绪"))
      return;
    started = true;
    if (controller.startRecording(directory))
      QTimer::singleShot(3000, &controller, &CameraController::stopRecording);
  });
  QTimer::singleShot(0, &app, [&] {
    if (controller.devices().isEmpty()) {
      complete("skipped", "No camera available; synthetic overlay checked",
               synthetic ? 0 : 1);
      return;
    }
    timeout.start(20000);
    controller.setEnabled(true);
  });
  return app.exec();
}
