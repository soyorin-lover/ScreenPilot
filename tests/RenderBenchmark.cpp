#include "RenderBenchmark.h"
#include "CanvasRenderer.h"
#include "FrameConversion.h"
#include "ScreenEffects.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoSink>
#include <cstdio>
#include <functional>

// Times CanvasRenderer::render on one decoded frame with default canvas
// settings at export and preview sizes, plus its per-frame helpers.
int runRenderBenchmark(QGuiApplication &app, const QString &source) {
  QMediaPlayer player;
  QVideoSink sink;
  player.setVideoSink(&sink);
  QImage frame;
  QObject::connect(&sink, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame &f) {
    if (frame.isNull() && f.isValid()) {
      frame = FrameConversion::toImage(f);
      app.exit(0);
    }
  });
  QTimer::singleShot(20000, &app, [&] { app.exit(3); });
  player.setSource(QUrl::fromLocalFile(source));
  player.play();
  if (app.exec() != 0 || frame.isNull()) return 2;
  player.stop();
  const QVariantMap settings = CanvasRenderer::defaults();
  auto measure = [](int iterations, const std::function<void()> &work) {
    work(); // warm caches
    QElapsedTimer clock;
    clock.start();
    for (int i = 0; i < iterations; ++i) work();
    return clock.nsecsElapsed() / 1e6 / iterations;
  };
  const QSize exportSize(2560, 1440), previewBounds(1000, 563);
  QJsonObject report{{"source", QString("%1x%2").arg(frame.width()).arg(frame.height())}};
  report["exportMs"] = measure(30, [&] { CanvasRenderer::render(frame, settings, {}, {}, {}, exportSize, {}, 1, 1); });
  report["previewMs"] = measure(60, [&] { CanvasRenderer::render(frame, settings, {}, previewBounds, {}, {}, {}, 1, 1); });
  report["validateUs"] = measure(1000, [&] { CanvasRenderer::validate(settings); }) * 1000;
  const QTransform projection = ScreenEffects::projection(QRectF(100, 100, 2360, 1240), exportSize, settings, 1);
  report["identityProjectionType"] = int(projection.type());
  std::fprintf(stdout, "%s\n", QJsonDocument(report).toJson(QJsonDocument::Compact).constData());
  return 0;
}
