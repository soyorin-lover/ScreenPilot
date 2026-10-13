#include "FrameConversionSmokeTest.h"
#include "FrameConversion.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoSink>
#include <algorithm>
#include <cstdio>
#include <vector>

// Decodes a local fixture, then compares FrameConversion against
// QVideoFrame::toImage() per channel and times both on the same frames.
int runFrameConversionSmokeTest(QGuiApplication &app, const QString &source) {
  QMediaPlayer player;
  QVideoSink sink;
  player.setVideoSink(&sink);
  std::vector<QVideoFrame> frames;
  QObject::connect(&sink, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame &frame) {
    if (frame.isValid() && frames.size() < 60) frames.push_back(frame);
    if (frames.size() >= 60) player.pause();
  });
  QTimer::singleShot(20000, &app, [&] { app.exit(3); });
  QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &app, [&](QMediaPlayer::MediaStatus s) {
    if (s == QMediaPlayer::EndOfMedia) player.pause();
  });
  player.setSource(QUrl::fromLocalFile(source));
  player.play();
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &app, [&] {
    if (frames.size() >= 60 || (player.mediaStatus() == QMediaPlayer::EndOfMedia && !frames.empty())) app.exit(0);
  });
  poll.start(50);
  if (app.exec() != 0 || frames.empty()) {
    std::fprintf(stderr, "frame-conversion: no decoded frames\n");
    return 2;
  }
  player.stop();
  qint64 qtNs = 0, fastNs = 0, diffSum = 0, samples = 0, maxDiff = 0, over8 = 0;
  QElapsedTimer clock;
  for (const QVideoFrame &frame : frames) {
    clock.start();
    const QImage reference = frame.toImage().convertToFormat(QImage::Format_RGB32);
    qtNs += clock.nsecsElapsed();
    clock.start();
    const QImage fast = FrameConversion::toImage(frame);
    fastNs += clock.nsecsElapsed();
    if (&frame == &frames.front() && qEnvironmentVariableIsSet("FRAMECONV_DUMP")) {
      reference.save(qEnvironmentVariable("FRAMECONV_DUMP") + "/qt.png");
      fast.save(qEnvironmentVariable("FRAMECONV_DUMP") + "/fast.png");
    }
    if (reference.size() != fast.size()) {
      std::fprintf(stderr, "frame-conversion: size mismatch\n");
      return 1;
    }
    for (int y = 0; y < fast.height(); y += 3) {
      const auto *a = reinterpret_cast<const QRgb *>(reference.constScanLine(y));
      const auto *b = reinterpret_cast<const QRgb *>(fast.constScanLine(y));
      for (int x = 0; x < fast.width(); x += 3) {
        const int d = std::max({std::abs(qRed(a[x]) - qRed(b[x])), std::abs(qGreen(a[x]) - qGreen(b[x])),
                                std::abs(qBlue(a[x]) - qBlue(b[x]))});
        diffSum += d; ++samples; maxDiff = std::max<qint64>(maxDiff, d); over8 += d > 8;
      }
    }
  }
  const auto format = frames.front().surfaceFormat();
  const double meanDiff = double(diffSum) / samples, over8Ratio = double(over8) / samples;
  const bool fast = FrameConversion::isFastPath(format);
  // Chroma edge filtering may differ from Qt's converter; the body must match.
  const bool pass = !fast || (meanDiff < 1.0 && over8Ratio < .01);
  const QJsonObject report{{"frames", int(frames.size())},
      {"width", frames.front().width()}, {"height", frames.front().height()},
      {"pixelFormat", QVideoFrameFormat::pixelFormatToString(format.pixelFormat())},
      {"colorSpace", int(format.colorSpace())}, {"colorRange", int(format.colorRange())},
      {"fastPath", fast}, {"qtMeanMs", qtNs / 1e6 / frames.size()}, {"fastMeanMs", fastNs / 1e6 / frames.size()},
      {"meanChannelDiff", meanDiff}, {"maxChannelDiff", maxDiff}, {"over8Ratio", over8Ratio}, {"pass", pass}};
  std::fprintf(stdout, "%s\n", QJsonDocument(report).toJson(QJsonDocument::Compact).constData());
  return pass ? 0 : 1;
}
