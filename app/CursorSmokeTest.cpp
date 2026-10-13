#include "CursorSmokeTest.h"
#include "CanvasRenderer.h"
#include "CursorEffects.h"
#include "ExportController.h"
#include "ProjectController.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
int runCursorSmokeTest(const QString &directory) {
  if (!QDir().mkpath(directory))
    return 2;
  const QString media = QDir(directory).filePath("cursor-fixture.mp4");
  QFile fixture(media);
  if (!fixture.open(QIODevice::WriteOnly))
    return 2;
  fixture.write("synthetic project persistence fixture");
  fixture.close();
  QVariantList samples;
  auto sample = [](double t, double x, bool click, bool inside) {
    return QVariantMap{
        {"time", t}, {"x", x}, {"y", .5}, {"click", click}, {"inside", inside}};
  };
  samples << sample(0, .2, false, true) << sample(1, .5, true, true)
          << sample(1.1, .5, false, true) << sample(1.2, 1, false, false);
  ProjectController project;
  bool history = project.newRecording(QUrl::fromLocalFile(media), 2, samples) &&
                 !project.canvasSettings().value("cursorEnabled").toBool();
  project.setCanvasOption("cursorEnabled", true);
  project.undo();
  history &= !project.canvasSettings().value("cursorEnabled").toBool();
  project.redo();
  history &= project.canvasSettings().value("cursorEnabled").toBool();
  project.setCanvasOption("cursorStyle", "crosshair");
  project.setCanvasOption("cursorSize", 48);
  const bool mapped =
      project.setClipRate(0, 2) &&
      CursorEffects::evaluate(samples, project.sourceTime(.55))
              .value("clickAge")
              .toDouble() > .09 &&
      !CursorEffects::evaluate(samples, 1.2).value("inside").toBool();
  const QString saved = QDir(directory).filePath("cursor-project.dogscproject");
  const bool savedOk = project.saveAs(QUrl::fromLocalFile(saved));
  ProjectController reopened;
  const bool persistence =
      savedOk && reopened.openProject(QUrl::fromLocalFile(saved)) &&
      reopened.canvasSettings() == project.canvasSettings();
  QImage source(640, 360, QImage::Format_RGB32);
  source.fill(Qt::black);
  auto settings = project.canvasSettings();
  const auto pointer = CursorEffects::evaluate(samples, 1.1);
  const QImage enabled =
      CanvasRenderer::render(source, settings, {}, {}, pointer);
  settings["cursorEnabled"] = false;
  const QImage disabled =
      CanvasRenderer::render(source, settings, {}, {}, pointer);
  const bool rendering =
      enabled != disabled &&
      disabled == CanvasRenderer::render(source, settings) &&
      CursorEffects::evaluate(samples, .9).value("clickAge").isNull() &&
      CursorEffects::evaluate(samples, 1.8).value("clickAge").isNull();
  settings["cursorSize"] = 0;
  const bool validation = !CanvasRenderer::validate(settings);
  enabled.save(QDir(directory).filePath("cursor-overlay-fixture.png"));
  QFile report(QDir(directory).filePath("cursor-smoke.json"));
  if (report.open(QIODevice::WriteOnly))
    report.write(QJsonDocument(QJsonObject{{"history", history},
                                           {"source_time_mapping", mapped},
                                           {"persistence", persistence},
                                           {"rendering", rendering},
                                           {"validation", validation}})
                     .toJson());
  return history && mapped && persistence && rendering && validation ? 0 : 1;
}

int runCursorExportSmokeTest(QGuiApplication &, const QString &source,
                             const QString &directory) {
  QDir dir(directory);
  if (directory.isEmpty() ||
      (dir.exists() &&
       !dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) ||
      !QDir().mkpath(directory))
    return 2;
  bool passed = true;
  QJsonObject report;
  const QVariantList samples{QVariantMap{{"time", 2.0},
                                         {"x", .5},
                                         {"y", .5},
                                         {"inside", true},
                                         {"click", true}},
                             QVariantMap{{"time", 2.3},
                                         {"x", .5},
                                         {"y", .5},
                                         {"inside", true},
                                         {"click", false}},
                             QVariantMap{{"time", 2.6},
                                         {"x", .5},
                                         {"y", .5},
                                         {"inside", true},
                                         {"click", false}},
                             QVariantMap{{"time", 2.9},
                                         {"x", .5},
                                         {"y", .5},
                                         {"inside", true},
                                         {"click", false}}};
  for (bool enabled : {false, true}) {
    ExportController exporter;
    const QString path =
        dir.filePath(enabled ? "cursor-on.mp4" : "cursor-off.mp4");
    QEventLoop exporting;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &exporting, &QEventLoop::quit);
    QObject::connect(&exporter, &ExportController::changed, &exporting, [&] {
      if (!exporter.busy())
        exporting.quit();
    });
    exporter.start(QUrl::fromLocalFile(source),
                   {QVariantMap{{"sourceStart", 2.0},
                                {"sourceDuration", 1.0},
                                {"playbackRate", 2.0}}},
                   QUrl::fromLocalFile(path), {},
                   {{"cursorEnabled", enabled},
                    {"cursorVisible", true},
                    {"cursorSize", 96},
                    {"cursorStyle", "crosshair"}},
                   {{"audioMode", "muted"}, {"pointerSamples", samples}});
    timeout.start(15000);
    if (exporter.busy())
      exporting.exec();
    if (exporter.busy() || !exporter.error().isEmpty())
      return 1;
    QMediaPlayer player;
    QVideoSink sink;
    QEventLoop decoding;
    player.setVideoSink(&sink);
    int white = 0, frames = 0;
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &decoding,
                     [&](const QVideoFrame &frame) {
                       if (!frame.isValid())
                         return;
                       const auto image = frame.toImage();
                       if (image.isNull())
                         return;
                       ++frames;
                       const auto c = image.pixelColor(image.width() / 2,
                                                       image.height() / 2);
                       if (c.red() > 150 && c.green() > 150)
                         ++white;
                     });
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &decoding,
                     [&](QMediaPlayer::MediaStatus status) {
                       if (status == QMediaPlayer::LoadedMedia)
                         player.play();
                       if (status == QMediaPlayer::EndOfMedia ||
                           status == QMediaPlayer::InvalidMedia)
                         decoding.quit();
                     });
    QObject::disconnect(&timeout, nullptr, &exporting, nullptr);
    QObject::connect(&timeout, &QTimer::timeout, &decoding, &QEventLoop::quit);
    player.setSource(QUrl::fromLocalFile(path));
    timeout.start(8000);
    decoding.exec();
    const bool ok = frames > 0 && player.error() == QMediaPlayer::NoError &&
                    (enabled ? white > 0 : white == 0);
    report[enabled ? "enabledPassed" : "disabledPassed"] = ok;
    report[enabled ? "enabledMarkerFrames" : "disabledMarkerFrames"] = white;
    passed &= ok;
  }
  report["success"] = passed;
  QFile file(dir.filePath("cursor-export-result.json"));
  if (!file.open(QIODevice::WriteOnly))
    return 2;
  file.write(QJsonDocument(report).toJson());
  return passed ? 0 : 1;
}
