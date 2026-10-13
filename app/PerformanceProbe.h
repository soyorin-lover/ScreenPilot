#pragma once
#include "CanvasPreview.h"
#include "CanvasRenderer.h"
#include "ExportController.h"
#include "ProjectController.h"
#include "PreviewAudioProcessor.h"
#include <QAudioOutput>
#include <QCommandLineParser>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QQuickWindow>
#include <QSaveFile>
#include <QScreen>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

inline int runMediaPerformanceProbe(QGuiApplication &app, QObject *root,
                                   ProjectController &project, ExportController &exporter,
                                   PreviewAudioProcessor &audio,
                                   const QCommandLineParser &cli) {
  qputenv("SCREENPILOT_EXPORT_METRICS", "1");
  const QDir output(cli.value("performance-result-directory"));
  if (cli.value("performance-result-directory").isEmpty() ||
      QFileInfo::exists(output.filePath("performance.json")) || !QDir().mkpath(output.absolutePath())) return 2;
  auto player = root->findChild<QMediaPlayer *>("editorPlayer");
  auto preview = root->findChild<CanvasPreview *>("canvasPreview");
  auto view = root->findChild<QQuickItem *>("editorView");
  if (!player || !preview || !view) return 3;
  auto number = [&](const char *option, double fallback) {
    bool ok = false; const double value = cli.value(option).toDouble(&ok);
    return ok && std::isfinite(value) && value > 0 ? value : fallback;
  };
  const double rate = std::clamp(number("performance-rate", 1), 1.0, 100.0);
  const double playbackSeconds = std::clamp(number("performance-seconds", 15), 1.0, 120.0);
  const bool exportOnly = cli.isSet("performance-export-only");
  const double minimumPresentedFps = number("performance-min-presented-fps", 0);
  const double maximumPresentedGap = number("performance-max-presented-gap-ms", 0);
  const bool performanceGated = minimumPresentedFps > 0 || maximumPresentedGap > 0;
  if (exportOnly && performanceGated) return 2;
  const double exportSeconds = std::clamp(number("performance-export-seconds", 20), 1.0, exportOnly ? 600.0 : 120.0);
  const qint64 timeoutMs = exportOnly ? 600000 : 180000;
  const int fps = int(number("performance-fps", 60));
  const QString resolution = cli.value("performance-resolution").isEmpty() ? "1440p" : cli.value("performance-resolution");
  if ((fps != 30 && fps != 60) || !QStringList{"source", "1080p", "1440p", "4k"}.contains(resolution)) return 2;
  const QUrl source = QUrl::fromLocalFile(QFileInfo(cli.value("media-performance-test")).absoluteFilePath());
  if (!QFileInfo::exists(source.toLocalFile())) return 2;
  if (auto window = qobject_cast<QQuickWindow *>(root)) { window->resize(1280, 860); window->show(); }
  root->setProperty("editing", !exportOnly);
  view->setProperty("monitorMuted", cli.isSet("performance-muted-preview"));
  audio.setVolume(0);
  if (player->audioOutput()) player->audioOutput()->setVolume(0);
  QMediaPlayer metadata;
  metadata.setSource(source);
  QElapsedTimer total, phase, gap;
  total.start(); gap.start();
  QTimer timer;
  timer.setInterval(16);
  QVector<double> gaps, exportGaps;
  QVector<double> swapIntervals, presentedIntervals;
  quint64 lastPresentedSerial = 0;
  struct SwapStamp {
    std::atomic<quint64> serial{0};
    std::atomic<double> frameTime{-1};
    QElapsedTimer clock;
  };
  const auto swapStamp = std::make_shared<SwapStamp>();
  swapStamp->clock.start();
  int presentedImages = 0;
  double playbackStartMs = -1, previousSwapMs = -1, previousPresentedMs = -1;
  double longestNoNewImageMs = 0, presentedFirstTime = -1, presentedLastTime = -1;
  QString latchedAudioError;
  auto latchAudioError = [&] { if (latchedAudioError.isEmpty() && !audio.error().isEmpty()) latchedAudioError = audio.error(); };
  QObject::connect(&audio, &PreviewAudioProcessor::changed, &timer, latchAudioError);
  int stage = 0, frames = 0, uniqueFrames = 0, frameSwaps = 0;
  double lastTimestamp = -1, firstTimestamp = -1;
  bool done = false;
  qint64 lastDiagnosticMs = -5000;
  QJsonObject report{{"source", source.toLocalFile()}, {"sourceBytes", double(QFileInfo(source.toLocalFile()).size())},
      {"rate", rate}, {"playbackSecondsRequested", playbackSeconds}, {"exportSecondsRequested", exportSeconds},
      {"resolution", resolution}, {"fps", fps}, {"windowWidth", 1280}, {"windowHeight", 860},
      {"monitorMuted", cli.isSet("performance-muted-preview")}, {"monitorVolume", 0},
      {"exportOnly", exportOnly}, {"timeoutSeconds", timeoutMs / 1000},
      {"exportAudioMode", cli.isSet("performance-muted-export") ? "muted" : "videoAudio"}};
  if (auto window = qobject_cast<QQuickWindow *>(root)) {
    if (auto screen = window->screen()) report["windowScreenRefreshRate"] = screen->refreshRate();
    report["windowDevicePixelRatio"] = window->devicePixelRatio();
  }
  if (auto screen = app.primaryScreen()) {
    report["primaryScreenRefreshRate"] = screen->refreshRate();
    report["primaryScreenDevicePixelRatio"] = screen->devicePixelRatio();
  }
  auto finish = [&](bool success, const QString &error) {
    latchAudioError();
    if (done) return; done = true; timer.stop(); player->pause(); metadata.stop();
    if (exporter.busy()) exporter.cancel();
    std::sort(gaps.begin(), gaps.end());
    const double playbackMs = report.value("playbackElapsedMs").toDouble(stage == 2 ? phase.elapsed() : 0);
    if (playbackMs > 0) longestNoNewImageMs = std::max(longestNoNewImageMs, playbackStartMs + playbackMs - (previousPresentedMs < 0 ? playbackStartMs : previousPresentedMs));
    const double presentedFps = playbackMs > 0 ? presentedImages * 1000.0 / playbackMs : 0;
    const bool gatePassed = (!minimumPresentedFps || presentedFps >= minimumPresentedFps) &&
        (!maximumPresentedGap || longestNoNewImageMs <= maximumPresentedGap);
    report["functionalSuccess"] = success;
    report["performanceGateEnabled"] = performanceGated;
    report["performanceGatePassed"] = performanceGated ? QJsonValue(gatePassed) : QJsonValue(QJsonValue::Null);
    report["minimumPresentedFps"] = minimumPresentedFps;
    report["maximumPresentedGapMs"] = maximumPresentedGap;
    report["success"] = success && (!performanceGated || gatePassed); report["error"] = error;
    report["scenegraphSubmittedUniqueImages"] = presentedImages;
    report["scenegraphSubmittedUniqueFps"] = presentedFps;
    report["longestNoNewSubmittedImageMs"] = longestNoNewImageMs;
    report["presentationMeasurement"] = "Unique rendered serial captured before scenegraph synchronization, sampled at frame swap; proxy for scenegraph submission, not physical display";
    report["submittedFirstSourceSeconds"] = presentedFirstTime;
    report["submittedLastSourceSeconds"] = presentedLastTime;
    report["submittedSourceProgressSeconds"] = presentedFirstTime >= 0 ? presentedLastTime - presentedFirstTime : 0;
    report["observedSourceRate"] = playbackMs > 0 ? (player->position()/1000.0 - report.value("playbackStartSourceSeconds").toDouble()) * 1000.0 / playbackMs : 0;
    auto intervals = [](QVector<double> values) {
      std::sort(values.begin(), values.end());
      return QJsonObject{{"count",values.size()}, {"p95Ms",values.isEmpty() ? 0 : values[std::min(values.size()-1,qsizetype(values.size()*.95))]}, {"maxMs",values.isEmpty() ? 0 : values.last()}};
    };
    report["swapIntervals"] = intervals(swapIntervals);
    report["newSubmittedImageIntervals"] = intervals(presentedIntervals);
    report["elapsedMs"] = double(total.elapsed()); report["frameReadyCount"] = frames;
    report["uniqueFrameTimestampCount"] = uniqueFrames;
    report["playbackFrameSwapCount"] = frameSwaps;
    report["exportMetrics"] = QJsonObject::fromVariantMap(exporter.performanceMetrics());
    report["firstFrameTimestamp"] = firstTimestamp; report["lastFrameTimestamp"] = lastTimestamp;
    report["guiTimerGapP95Ms"] = gaps.isEmpty() ? 0 : gaps[std::min(gaps.size()-1, qsizetype(gaps.size()*.95))];
    report["guiTimerGapMaxMs"] = gaps.isEmpty() ? 0 : gaps.last();
    std::sort(exportGaps.begin(), exportGaps.end());
    report["exportGuiTimerGapP95Ms"] = exportGaps.isEmpty() ? 0 : exportGaps[std::min(exportGaps.size()-1, qsizetype(exportGaps.size()*.95))];
    report["exportGuiTimerGapMaxMs"] = exportGaps.isEmpty() ? 0 : exportGaps.last();
    report["previewMetricsFinal"] = QJsonObject::fromVariantMap(preview->performanceMetrics());
    report["previewAudioError"] = latchedAudioError;
    report["previewAudioActive"] = audio.active();
    QSaveFile file(output.filePath("performance.json"));
    const bool saved = file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(report).toJson()) > 0 && file.commit();
    app.exit(success && (!performanceGated || gatePassed) && saved ? 0 : 1);
  };
  auto startExport = [&] {
    latchAudioError();
    player->pause(); stage = 3; phase.restart();
    const double rangeEnd = std::min(exportSeconds, project.duration());
    report["exportRangeStartSeconds"] = 0;
    report["exportRangeEndSeconds"] = rangeEnd;
    report["exportSourceSeconds"] = rangeEnd * rate;
    report["exportStartedElapsedMs"] = double(total.elapsed());
    if (!exporter.startProject(QUrl::fromLocalFile(output.filePath("export.mp4")),
        {{"resolution",resolution},{"fps",fps},{"audioMode", cli.isSet("performance-muted-export") ? "muted" : "videoAudio"}}, 0, rangeEnd)) finish(false, exporter.error());
  };
  QObject::connect(preview, &CanvasPreview::frameReady, &timer, [&] {
    if (stage != 2) return; ++frames;
    const double timestamp = preview->frameTime();
    if (firstTimestamp < 0) firstTimestamp = timestamp;
    if (timestamp != lastTimestamp) ++uniqueFrames;
    lastTimestamp = timestamp;
  });
  if (auto window = qobject_cast<QQuickWindow *>(root)) {
    QObject::connect(window, &QQuickWindow::beforeSynchronizing, &timer, [swapStamp, preview] {
      swapStamp->serial.store(preview->renderedSerial());
      swapStamp->frameTime.store(preview->renderedFrameTime());
    }, Qt::DirectConnection);
    const QPointer<QTimer> timerGuard(&timer);
    auto acceptSwap = [&](quint64 serial, double sourceSeconds, double swappedMs) {
        if (stage != 2 || swappedMs < playbackStartMs) return;
        ++frameSwaps;
        if (previousSwapMs >= 0) swapIntervals.append(swappedMs-previousSwapMs);
        previousSwapMs = swappedMs;
        if (serial == lastPresentedSerial || sourceSeconds == presentedLastTime) return;
        lastPresentedSerial = serial; ++presentedImages;
        const double sincePrevious = swappedMs-(previousPresentedMs < 0 ? playbackStartMs : previousPresentedMs);
        longestNoNewImageMs = std::max(longestNoNewImageMs, sincePrevious);
        if (previousPresentedMs >= 0) presentedIntervals.append(sincePrevious);
        previousPresentedMs = swappedMs;
        if (presentedFirstTime < 0) presentedFirstTime = sourceSeconds;
        presentedLastTime = sourceSeconds;
    };
    QObject::connect(window, &QQuickWindow::frameSwapped, &timer, [swapStamp, timerGuard, acceptSwap] {
      const quint64 serial = swapStamp->serial.load();
      const double sourceSeconds = swapStamp->frameTime.load();
      const double swappedMs = swapStamp->clock.nsecsElapsed()/1e6;
      if (timerGuard) QMetaObject::invokeMethod(timerGuard, [acceptSwap, serial, sourceSeconds, swappedMs] {
        acceptSwap(serial, sourceSeconds, swappedMs);
      }, Qt::QueuedConnection);
    }, Qt::DirectConnection);
  }
  QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
    latchAudioError();
    if (total.elapsed() - lastDiagnosticMs >= 5000) {
      lastDiagnosticMs = total.elapsed();
      qInfo() << "performance-probe" << "stage" << stage << "elapsedMs" << lastDiagnosticMs
              << "metadataStatus" << metadata.mediaStatus() << "mediaStatus" << player->mediaStatus()
              << "playbackState" << player->playbackState() << "positionMs" << player->position()
              << "hasFrame" << preview->hasFrame() << "primingPreview" << view->property("primingPreview")
              << "awaitingSourceFrame" << view->property("awaitingSourceFrame") << "visible" << view->isVisible();
    }
    const double interval = gap.nsecsElapsed()/1e6; gap.restart();
    if (stage == 2) gaps.append(interval);
    else if (stage == 3) exportGaps.append(interval);
    if (total.elapsed() > timeoutMs) { finish(false, QString("Probe exceeded %1 second timeout").arg(timeoutMs / 1000)); return; }
    if (stage == 0 && metadata.duration() > 0) {
      const QSize dimensions = metadata.metaData().value(QMediaMetaData::Resolution).toSize();
      report["sourceWidth"] = dimensions.width(); report["sourceHeight"] = dimensions.height();
      report["sourceFps"] = metadata.metaData().value(QMediaMetaData::VideoFrameRate).toDouble();
      report["sourceDurationSeconds"] = metadata.duration()/1000.0;
      if (!project.newRecording(source, metadata.duration()/1000.0) || !project.setClipRate(0, rate)) { finish(false, project.error()); return; }
      const QString systemSidecar = source.toLocalFile() + ".system.wav";
      if (QFileInfo::exists(systemSidecar) && !project.setAudioMedia(QUrl::fromLocalFile(systemSidecar), {})) {
        finish(false, project.error()); return;
      }
      report["systemAudioAttached"] = !project.systemAudioSource().isEmpty();
      report["systemAudioBytes"] = double(QFileInfo(systemSidecar).size());
      report["projectSourceDurationSeconds"] = project.sourceDuration();
      report["projectOutputDurationSeconds"] = project.duration();
      report["initializationElapsedMs"] = double(total.elapsed());
      if (exportOnly) startExport();
      else stage = 1;
    } else if (stage == 1 && preview->renderedSerial() > 0 && !preview->renderedImage().isNull() && !view->property("primingPreview").toBool()) {
      if (cli.isSet("performance-render-baseline")) {
        const QImage image = preview->sourceImage(); QElapsedTimer rendering;
        qint64 sum = 0, maximum = 0;
        for (int i = 0; i < 30; ++i) {
          rendering.start(); const QImage result = CanvasRenderer::render(image, project.canvasSettings(), {}, {}, {}, QSize(2560,1440));
          const qint64 ns = rendering.nsecsElapsed(); sum += ns; maximum = std::max(maximum, ns);
          if (result.isNull()) { finish(false, "Render baseline failed"); return; }
        }
        report["renderBaseline"] = QJsonObject{{"iterations",30},{"width",2560},{"height",1440},{"meanMs",sum/30.0/1e6},{"maxMs",maximum/1e6}};
      }
      report["previewMetricsBefore"] = QJsonObject::fromVariantMap(preview->performanceMetrics());
      phase.start(); gap.restart(); stage = 2;
      playbackStartMs = swapStamp->clock.nsecsElapsed()/1e6;
      lastPresentedSerial = preview->renderedSerial();
      report["playbackStartSourceSeconds"] = player->position()/1000.0;
      QMetaObject::invokeMethod(view, "playPause");
    } else if (stage == 2 && phase.elapsed() >= playbackSeconds*1000) {
      report["playbackElapsedMs"] = double(phase.elapsed());
      latchAudioError();
      report["playbackEndSourcePositionSeconds"] = player->position()/1000.0;
      if (cli.isSet("performance-preview-image")) preview->renderedImage().save(output.filePath("preview.png"));
      report["previewMetricsAfter"] = QJsonObject::fromVariantMap(preview->performanceMetrics());
      player->pause();
      if (uniqueFrames < 2) { finish(false, "No advancing editor frames"); return; }
      if (!report.value("previewAudioError").toString().isEmpty()) { finish(false, report.value("previewAudioError").toString()); return; }
      if (cli.isSet("performance-no-export")) { finish(true, {}); return; }
      startExport();
    } else if (stage == 3) {
      if (cli.isSet("performance-cancel-export") && phase.elapsed() >= 1000 && exporter.busy()) { exporter.cancel(); stage = 4; }
      else if (!exporter.busy()) {
        report["exportElapsedMs"] = double(phase.elapsed()); report["exportError"] = exporter.error();
        report["exportBytes"] = double(QFileInfo(output.filePath("export.mp4")).size());
        finish(exporter.error().isEmpty() && QFileInfo(output.filePath("export.mp4")).size() > 0, exporter.error());
      }
    } else if (stage == 4 && !exporter.busy()) {
      report["cancellationElapsedMs"] = double(phase.elapsed()); report["exportError"] = exporter.error(); finish(exporter.error().isEmpty() && !QFileInfo::exists(output.filePath("export.mp4")), exporter.error());
    }
  });
  timer.start();
  const int result = app.exec();
  if (auto window = qobject_cast<QQuickWindow *>(root)) QObject::disconnect(window, nullptr, &timer, nullptr);
  return result;
}
