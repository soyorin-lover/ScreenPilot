#include "NativeExportSmokeTest.h"
#include "CanvasRenderer.h"
#include "ExportController.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QQueue>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoFrameInput>
#include <QVideoSink>
#include <functional>

namespace {
bool waitUntil(const std::function<bool()> &done) {
  QEventLoop loop;
  QTimer poll, deadline;
  poll.setInterval(10);
  deadline.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (done()) loop.quit();
  });
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  deadline.start(30000);
  if (!done()) loop.exec();
  return done();
}
bool generate(const QString &path) {
  QMediaCaptureSession session;
  QMediaRecorder recorder;
  QVideoFrameInput video;
  session.setRecorder(&recorder);
  session.setVideoFrameInput(&video);
  QMediaFormat format(QMediaFormat::MPEG4);
  format.setVideoCodec(QMediaFormat::VideoCodec::H264);
  recorder.setMediaFormat(format);
  recorder.setVideoResolution(QSize(2560, 1440));
  recorder.setVideoFrameRate(30);
  recorder.setOutputLocation(QUrl::fromLocalFile(path));
  recorder.setAutoStop(true);
  QQueue<QVideoFrame> frames;
  for (int i = 0; i < 9; ++i) {
    QImage image(2560, 1440, QImage::Format_RGBA8888);
    image.fill(i % 2 ? Qt::green : Qt::blue);
    QVideoFrame frame(image);
    frame.setStartTime(qint64(i) * 1000000 / 30);
    frame.setEndTime(qint64(i + 1) * 1000000 / 30);
    frames.enqueue(frame);
  }
  bool eof = false, pumping = false;
  auto pump = [&] {
    if (pumping) return;
    pumping = true;
    while (!frames.isEmpty() && video.sendVideoFrame(frames.head()))
      frames.dequeue();
    if (frames.isEmpty() && !eof) eof = video.sendVideoFrame({});
    pumping = false;
  };
  QObject::connect(&video, &QVideoFrameInput::readyToSendVideoFrame, &recorder, pump);
  recorder.record();
  pump();
  return waitUntil([&] {
    return recorder.error() != QMediaRecorder::NoError ||
           (eof && recorder.recorderState() == QMediaRecorder::StoppedState);
  }) && recorder.error() == QMediaRecorder::NoError;
}
}

int runNativeExportSmokeTest(QGuiApplication &app, const QString &directory) {
  Q_UNUSED(app);
  QDir dir(directory);
  if ((dir.exists() && !dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) ||
      !QDir().mkpath(directory)) return 2;
  const QVariantMap square{{"ratio", "1:1"}};
  const bool sizing =
      CanvasRenderer::nativeOutputSize({3840, 2160}, {}) == QSize(3840, 2160) &&
      CanvasRenderer::nativeOutputSize({3840, 2160}, square) == QSize(2160, 2160) &&
      CanvasRenderer::nativeOutputSize({2160, 3840}, {{"ratio", "9:16"}}) == QSize(2160, 3840) &&
      CanvasRenderer::nativeOutputSize({3840, 2160}, {{"ratio", "1:1"}, {"cropWidth", .5}, {"cropHeight", .5}}) == QSize(1080, 1080) &&
      CanvasRenderer::nativeOutputSize({640, 360}, square) == QSize(360, 360) &&
      CanvasRenderer::nativeOutputSize({641, 361}, {}) == QSize(640, 360) &&
      CanvasRenderer::nativeOutputSize({}, square).isEmpty() &&
      CanvasRenderer::outputSize({3840, 2160}, square) == QSize(1080, 1080);
  const QString source = dir.filePath("generated-native-source.mp4");
  if (!generate(source)) return 3;
  const QString output = dir.filePath("native-square.mp4");
  ExportController exporter;
  exporter.start(QUrl::fromLocalFile(source),
                 {QVariantMap{{"sourceStart", 0.0}, {"sourceDuration", .3}, {"playbackRate", 1.0}}},
                 QUrl::fromLocalFile(output), {}, square,
                 {{"resolution", "source"}, {"audioMode", "muted"}});
  const bool exported = waitUntil([&] { return !exporter.busy(); }) && exporter.error().isEmpty();
  QMediaPlayer player;
  QVideoSink sink;
  player.setVideoSink(&sink);
  QSize decoded;
  QObject::connect(&sink, &QVideoSink::videoFrameChanged, &player, [&](const QVideoFrame &frame) {
    if (frame.isValid()) decoded = frame.size();
  });
  bool inspected = false;
  if (exported) {
    player.setSource(QUrl::fromLocalFile(output));
    player.play();
    inspected = waitUntil([&] { return !decoded.isEmpty() || player.error() != QMediaPlayer::NoError; });
    player.stop();
  }
  const bool passed = sizing && exported && inspected && decoded == QSize(1440, 1440);
  QFile report(dir.filePath("native-export-smoke-result.json"));
  if (!report.open(QIODevice::WriteOnly)) return 4;
  report.write(QJsonDocument(QJsonObject{{"passed", passed}, {"helperSizingPassed", sizing},
               {"exported", exported}, {"decodedWidth", decoded.width()}, {"decodedHeight", decoded.height()},
               {"error", exporter.error()}}).toJson());
  return passed ? 0 : 1;
}
