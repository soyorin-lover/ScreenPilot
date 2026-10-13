#include "CropSmokeTest.h"
#include "CanvasRenderer.h"
#include "ExportController.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QEventLoop>
#include <QTimer>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QVideoFrameInput>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <functional>
#include <cmath>
namespace {
bool waitFor(const std::function<bool()> &done) {
  QEventLoop loop; QTimer poll, timeout;
  poll.setInterval(10); timeout.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start(); timeout.start(30000); if (!done()) loop.exec(); return done();
}
QImage fixture() {
  QImage image(320, 240, QImage::Format_RGB32); image.fill(Qt::red);
  QPainter p(&image); p.fillRect(160, 0, 160, 240, Qt::green);
  // A blue strip lies inside the crop but outside its 2x zoom viewport.
  p.fillRect(160, 0, 32, 120, Qt::blue);
  p.fillRect(0, 120, 160, 120, Qt::blue); p.fillRect(160, 120, 160, 120, Qt::yellow);
  return image;
}
QVariantMap settings() {
  auto s = CanvasRenderer::defaults(); s["cropX"] = .5; s["cropY"] = 0.;
  s["cropWidth"] = .5; s["cropHeight"] = .5;
  s["padding"] = 0.; s["radius"] = 0.; s["shadow"] = 0.; return s;
}
int report(const QString &directory, const QString &name, const QJsonObject &result) {
  QDir().mkpath(directory); QFile f(QDir(directory).filePath(name));
  if (!f.open(QIODevice::WriteOnly)) return 2;
  f.write(QJsonDocument(result).toJson()); return result.value("passed").toBool() ? 0 : 1;
}
}
int runCropSmokeTest(const QString &directory) {
  auto s = settings(); const QImage image = fixture();
  const auto crop = CanvasRenderer::render(image, s, {}, {}, {}, QSize(160,120));
  const auto zoom = CanvasRenderer::sourceRect(image.size(), s, {2, .75, .25});
  auto invalid = s; invalid["cropWidth"] = .6;
  const bool bounds = !CanvasRenderer::validate(invalid);
  invalid = s; invalid["cropHeight"] = .001;
  const bool minimum = !CanvasRenderer::validate(invalid);
  const bool pixels = crop.pixelColor(80,60) == QColor(Qt::green);
  const bool transform = zoom == QRectF(200,30,80,60);
  const bool size = CanvasRenderer::croppedSize(image.size(), s) == QSize(160,120) &&
                    CanvasRenderer::outputSize(image.size(), s) == QSize(1440,1080);
  s["cursorEnabled"] = true; s["cursorStyle"] = "crosshair"; s["cursorSize"] = 96.;
  const auto pointer = QVariantMap{{"inside", true}, {"x", .75}, {"y", .25}};
  const auto cursor = CanvasRenderer::render(image, s, {2,.75,.25}, {}, pointer, QSize(160,120));
  const bool cursorMapped = cursor.pixelColor(80,60) != QColor(Qt::green) && cursor.pixelColor(20,20) == QColor(Qt::green);
  auto edgeSettings = settings();
  edgeSettings["cropX"] = .8; edgeSettings["cropWidth"] = .2;
  edgeSettings["cropY"] = .8; edgeSettings["cropHeight"] = .2;
  const QVariantList edgeTrack{QVariantMap{{"manual",true},{"start",0.0},{"end",1.0},
    {"scale",2.0},{"centerX",.9},{"centerY",.9},{"enter",0.0},{"exit",0.0}}};
  const auto edgeCamera = AutoFraming::evaluate(edgeTrack,.5);
  const auto edgeRect = CanvasRenderer::sourceRect(QSize(1000,1000),edgeSettings,edgeCamera);
  const bool edgeFocus = std::abs(edgeRect.center().x() - 900) < .001 &&
                         std::abs(edgeRect.center().y() - 900) < .001;
  s["ratio"] = "1:1";
  const auto fixed = CanvasRenderer::render(image, s, {}, {}, {}, QSize(160,160));
  const bool fixedFit = fixed.pixelColor(80,2) == QColor("#20242c") && fixed.pixelColor(80,80) == QColor(Qt::green);
  QDir().mkpath(directory); crop.save(QDir(directory).filePath("crop.png")); cursor.save(QDir(directory).filePath("crop-zoom-cursor.png"));
  return report(directory, "crop-report.json", {{"bounds", bounds}, {"minimum", minimum}, {"pixels", pixels}, {"transform", transform}, {"size", size}, {"cursorMapped", cursorMapped}, {"fixedFit", fixedFit}, {"edgeFocus",edgeFocus}, {"passed", bounds && minimum && pixels && transform && size && cursorMapped && fixedFit && edgeFocus}});
}
int runCropExportSmokeTest(const QString &directory) {
  QDir().mkpath(directory);
  const QString source = QDir(directory).filePath("crop-quadrants.mp4");
  QMediaCaptureSession session; QMediaRecorder recorder; QVideoFrameInput input;
  session.setRecorder(&recorder); session.setVideoFrameInput(&input);
  QMediaFormat format(QMediaFormat::MPEG4); format.setVideoCodec(QMediaFormat::VideoCodec::H264);
  recorder.setMediaFormat(format); recorder.setVideoResolution(QSize(320,240)); recorder.setVideoFrameRate(30);
  recorder.setOutputLocation(QUrl::fromLocalFile(source)); recorder.setAutoStop(true);
  int sent = 0; bool ended = false; bool pumping = false;
  auto pump = [&] {
    if (pumping) return; pumping = true;
    while (sent < 30) {
      QVideoFrame frame(fixture()); frame.setStartTime(qint64(sent)*1000000/30); frame.setEndTime(qint64(sent+1)*1000000/30);
      if (!input.sendVideoFrame(frame)) break; ++sent;
    }
    if (sent == 30 && !ended) ended = input.sendVideoFrame({});
    pumping = false;
  };
  QObject::connect(&input, &QVideoFrameInput::readyToSendVideoFrame, &recorder, pump);
  recorder.record(); pump();
  const bool generated = waitFor([&] { return recorder.error() != QMediaRecorder::NoError || (ended && recorder.recorderState() == QMediaRecorder::StoppedState); }) && recorder.error() == QMediaRecorder::NoError;
  const QString path = QDir(directory).filePath("cropped-export.mp4");
  ExportController exporter;
  if (generated) exporter.start(QUrl::fromLocalFile(source), {QVariantMap{{"sourceStart",0.0},{"sourceDuration",1.0},{"playbackRate",1.0}}}, QUrl::fromLocalFile(path),
      {QVariantMap{{"start",0.0},{"end",1.0},{"scale",2.0},{"centerX",.75},{"centerY",.25},{"enter",0.0},{"exit",0.0}}}, settings(), {{"audioMode","muted"}});
  const bool exported = generated && waitFor([&] { return !exporter.busy(); }) && exporter.error().isEmpty();
  QMediaPlayer player; QVideoSink sink; player.setVideoSink(&sink);
  int decoded = 0; bool content = true, dimensions = true, finished = false;
  QObject::connect(&sink, &QVideoSink::videoFrameChanged, &player, [&](const QVideoFrame &frame) {
    if (!frame.isValid()) return; const QImage image = frame.toImage(); if (image.isNull()) return;
    ++decoded; dimensions &= image.size() == QSize(160,120);
    for (const auto &point : {QPoint(20,20),QPoint(80,60),QPoint(140,100)}) {
      const QColor c = image.pixelColor(point); content &= c.green() > 90 && c.red() < 70 && c.blue() < 70;
    }
  });
  QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &player, [&](QMediaPlayer::MediaStatus status) { finished = status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia; });
  if (exported) { player.setSource(QUrl::fromLocalFile(path)); player.play(); waitFor([&] { return finished; }); }
  return report(directory,"crop-export-report.json",{{"generated",generated},{"exported",exported},{"decodedFrames",decoded},{"dimensions",dimensions},{"content",content},{"error",exporter.error()},{"passed",generated && exported && decoded > 0 && dimensions && content}});
}
