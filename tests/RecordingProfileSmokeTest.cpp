#include "RecordingProfileSmokeTest.h"
#include "RecordingController.h"
#include "RecordingProfile.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QMap>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSettings>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>

namespace {
QJsonObject dimensions(QSize size) {
  return {{"width", size.width()}, {"height", size.height()}};
}
struct RestoreSettings {
  QMap<QString, QVariant> values;
  RestoreSettings() {
    QSettings settings;
    for (const auto &key : {"recording/resolutionMode", "recording/videoCodec", "recording/systemAudio", "recording/frameRate"})
      values.insert(QString::fromLatin1(key), settings.value(key));
  }
  ~RestoreSettings() {
    QSettings settings;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
      if (it.value().isValid()) settings.setValue(it.key(), it.value());
      else settings.remove(it.key());
    settings.sync();
  }
};
}

int runRecordingProfileSmokeTest(QGuiApplication &app, const QString &directory) {
  QDir dir(directory);
  if ((dir.exists() && !dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) ||
      !QDir().mkpath(directory)) return 2;
  RestoreSettings restore;
  const bool regionTest=qEnvironmentVariableIntValue("SCREENPILOT_TEST_REGION")==1;
  const int durationMs = qBound(500, qEnvironmentVariableIntValue("SCREENPILOT_TEST_RECORDING_DURATION_MS") > 0
      ? qEnvironmentVariableIntValue("SCREENPILOT_TEST_RECORDING_DURATION_MS") : 1800, 120000);
  const int testWidth = qBound(320, qEnvironmentVariableIntValue("SCREENPILOT_TEST_RECORDING_WIDTH") > 0
      ? qEnvironmentVariableIntValue("SCREENPILOT_TEST_RECORDING_WIDTH") : 1480, 7680);
  const int testHeight = qBound(240, qEnvironmentVariableIntValue("SCREENPILOT_TEST_RECORDING_HEIGHT") > 0
      ? qEnvironmentVariableIntValue("SCREENPILOT_TEST_RECORDING_HEIGHT") : 840, 4320);
  QQuickView view;
  view.setTitle(QStringLiteral("Recording profile synthetic fixture"));
  QQmlComponent component(view.engine());
  component.setData(R"(import QtQuick
Rectangle {
  width: 1480; height: 840; color: "#13283e"
  Repeater { model: 148; Rectangle { x: index * 10; width: 1; height: 840; color: "#5080a0" } }
  Repeater { model: 84; Rectangle { y: index * 10; height: 1; width: 1480; color: "#5080a0" } }
  Rectangle { width: 130; height: 130; y: 300; color: "#ff6b32"
    SequentialAnimation on x { loops: Animation.Infinite
      NumberAnimation { from: 20; to: 1300; duration: 1000 }
      NumberAnimation { from: 1300; to: 20; duration: 1000 }
    }
  }
})", QUrl());
  auto *fixture = qobject_cast<QQuickItem *>(component.create());
  if (!fixture) return 2;
  fixture->setWidth(testWidth);
  fixture->setHeight(testHeight);
  view.setContent(QUrl(), &component, fixture);
  view.resize(testWidth, testHeight);
  view.show();
  RecordingController recorder;
  recorder.setOutputDirectory(directory);
  recorder.setMicrophoneEnabled(false);
  recorder.setSystemAudioEnabled(false);
  recorder.setResolutionMode("source");
  recorder.setVideoCodec("h264");
  if(regionTest) recorder.setFrameRate(30);
  const int requestedFps = qEnvironmentVariableIntValue("SCREENPILOT_TEST_RECORDING_FPS");
  if (requestedFps == 30 || requestedFps == 60) recorder.setFrameRate(requestedFps);
  QVideoSink capture, decoded;
  recorder.setPreviewSink(&capture);
  QMediaPlayer player;
  player.setVideoSink(&decoded);
  QJsonArray phases;
  QSize sourceSize, effective, decodedSize;
  int phase = 0, decodedFrames = 0;
  qint64 firstPts = -1, lastPts = -1, lastEnd = -1;
  QList<qint64> intervals;
  QVariantMap recordingMetrics;
  bool done = false, stopping = false, decoding = false, started = false;
  bool busyLocked = true;
  QString mode = "source";
  auto finish = [&](const QString &error) {
    if (done) return;
    done = true;
    const bool highResolution = !phases.isEmpty() &&
        (phases[0].toObject()["captureSize"].toObject()["width"].toInt() > 1920 ||
         phases[0].toObject()["captureSize"].toObject()["height"].toInt() > 1080);
    QJsonObject report{{"success", error.isEmpty()}, {"error", error},
                       {"busySettingsLocked", busyLocked}, {"phases", phases},
                       {"regionTest",regionTest},
                       {"highResolutionCoverage", highResolution ? "verified" : "unverified: actual capture did not exceed 1080p"}};
    QFile file(dir.filePath("recording-profile-smoke.json"));
    const bool written = file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(report).toJson()) > 0;
    player.stop();
    recorder.stop();
    app.exit(error.isEmpty() && written ? 0 : 1);
  };
  const bool helpers = RecordingProfile::outputSize({7680,4320}, "4k") == QSize(3840,2160) &&
      RecordingProfile::outputSize({3840,2160}, "1080p") == QSize(1920,1080) &&
      RecordingProfile::outputSize({1279,719}, "source") == QSize(1278,718) &&
      RecordingProfile::outputSize({1440,2560}, "1080p") == QSize(1080,1920) &&
      RecordingProfile::outputSize({640,480}, "4k") == QSize(640,480) &&
      RecordingProfile::outputSize({}, "source").isEmpty() &&
      RecordingProfile::videoBitRate({3840,2160}) > RecordingProfile::videoBitRate({1920,1080});
  recorder.setResolutionMode("invalid");
  recorder.setVideoCodec("invalid");
  bool setters = recorder.resolutionMode() == "source" && recorder.videoCodec() == "h264";
  recorder.setResolutionMode("1080p");
  recorder.setVideoCodec("hevc");
  { RecordingController restored; setters = setters && restored.resolutionMode() == "1080p" && restored.videoCodec() == "hevc"; }
  recorder.setResolutionMode("source");
  recorder.setVideoCodec("h264");
  for (const auto &entry : recorder.availableVideoCodecs()) {
    const auto codec = entry.toMap();
    setters = setters && !codec.value("label").toString().isEmpty() &&
        (codec.value("value") == "h264" || codec.value("value") == "hevc");
  }
  QObject::connect(&capture, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame &frame) {
    if (frame.isValid() && !decoding) sourceSize = frame.size();
  });
  QObject::connect(&decoded, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame &frame) {
    if (!frame.isValid() || !decoding) return;
    decodedSize = frame.size();
    if (firstPts < 0) firstPts = frame.startTime();
    if (lastPts >= 0) intervals.append(frame.startTime() - lastPts);
    lastPts = frame.startTime();
    lastEnd = frame.endTime();
    ++decodedFrames;
  });
  QObject::connect(&player, &QMediaPlayer::errorOccurred, &app,
                   [&](QMediaPlayer::Error, const QString &message) { finish(message); });
  QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &app, [&](QMediaPlayer::MediaStatus status) {
    if (done || !decoding || status != QMediaPlayer::EndOfMedia) return;
    const QSize expected = RecordingProfile::outputSize(sourceSize, mode);
    std::sort(intervals.begin(), intervals.end());
    qint64 intervalSum = 0;
    int gaps = 0;
    for (qint64 interval : intervals) { intervalSum += interval; if (interval > 1000000 / recorder.frameRate() + 1) ++gaps; }
    const double outputSeconds = (lastEnd - firstPts) / 1000000.0;
    phases.append(QJsonObject{{"mode", mode}, {"captureSize", dimensions(sourceSize)},
                              {"effectiveSize", dimensions(effective)}, {"decodedSize", dimensions(decodedSize)},
                              {"decodedFrames", decodedFrames}, {"requestedFps", recorder.frameRate()},
                              {"requestedDurationMs", durationMs}, {"outputDurationSeconds", outputSeconds},
                              {"actualAverageFps", outputSeconds > 0 ? decodedFrames / outputSeconds : 0},
                              {"frameIntervalMeanUs", intervals.isEmpty() ? 0.0 : double(intervalSum) / intervals.size()},
                              {"frameIntervalP95Us", intervals.isEmpty() ? 0.0 : double(intervals[qMin(intervals.size()-1, qsizetype(intervals.size()*.95))])},
                              {"frameIntervalMaxUs", intervals.isEmpty() ? 0.0 : double(intervals.last())},
                              {"frameIntervalGapCount", gaps},
                              {"recordingMetrics", QJsonObject::fromVariantMap(recordingMetrics)}});
    if (sourceSize.isEmpty() || !decodedFrames || decodedSize != expected || effective != expected) {
      finish("Decoded recording dimensions disagree with captured frame/profile/effective size"); return;
    }
    if (!intervals.isEmpty() && intervals.first() <= 0) {
      finish("Recorded video timestamps are not strictly increasing"); return;
    }
    if(regionTest && phase==1) {
      const auto full=phases[0].toObject()["decodedSize"].toObject();
      if(qAbs(decodedSize.width()*2-full["width"].toInt())>4 || qAbs(decodedSize.height()*2-full["height"].toInt())>4) {
        finish("Half-size region disagrees with full-frame recording");return;
      }
    }
    if (++phase == 2) { finish(busyLocked ? QString() : QString("Settings changed while recorder busy")); return; }
    player.stop();
    mode = regionTest ? "source" : "1080p";
    recorder.setResolutionMode(mode);
    if(regionTest) recorder.setCaptureRegion(QRectF(.25,.25,.5,.5));
    sourceSize = {}; decodedSize = {}; decodedFrames = 0;
    firstPts = lastPts = lastEnd = -1; intervals.clear();
    decoding = false; stopping = false; started = false;
    QTimer::singleShot(100, &app, [&] { recorder.start(); });
  });
  QObject::connect(&recorder, &RecordingController::changed, &app, [&] {
    if (done) return;
    if (!recorder.error().isEmpty()) { finish(recorder.error()); return; }
    if (recorder.recording() && !started) {
      started = true;
      const QString codec = recorder.videoCodec();
      recorder.setResolutionMode(mode == "source" ? "4k" : "source");
      recorder.setVideoCodec("hevc");
      busyLocked = busyLocked && recorder.resolutionMode() == mode && recorder.videoCodec() == codec;
      QTimer::singleShot(durationMs, &app, [&] {
        if (done) return;
        effective = recorder.effectiveVideoSize();
        stopping = true;
        recorder.stop();
      });
    }
    if (stopping && !recorder.busy() && !decoding && !recorder.lastRecording().isEmpty()) {
      recordingMetrics = recorder.performanceMetrics();
      decoding = true;
      player.setSource(recorder.lastRecording());
      player.play();
    }
  });
  QTimer::singleShot(700, &app, [&] {
    if (!helpers || !setters) { finish("Profile helper or invalid/persistent setter check failed"); return; }
    recorder.refreshSources();
    const auto sources = recorder.sources();
    for (int i = 0; i < sources.size(); ++i) {
      if (sources[i].toMap().value("label").toString() != view.title()) continue;
      recorder.setSelectedSource(i);
      recorder.start();
      return;
    }
    finish("Synthetic fixture window unavailable for capture");
  });
  QTimer::singleShot(qMax(45000, durationMs * 5 + 30000), &app, [&] { finish("Recording profile smoke timeout"); });
  return app.exec();
}
