#include "RecordingControlSmokeTest.h"
#include "RecordingController.h"
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QColor>
#include <QImage>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSettings>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

int runRecordingControlSmokeTest(QGuiApplication &app, const QString &directory) {
  QDir dir(directory);
  if ((dir.exists() && !dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) ||
      !QDir().mkpath(directory))
    return 2;
  QSettings settings;
  const QVariant oldAudio = settings.value("recording/systemAudio");
  struct Restore { QVariant value; ~Restore() { QSettings s; if (value.isValid()) s.setValue("recording/systemAudio", value); else s.remove("recording/systemAudio"); } } restore{oldAudio};
  QQuickView view;
  view.setTitle("Recording controls fixture");
  QQmlComponent component(view.engine());
  component.setData("import QtQuick\nRectangle { width: 400; height: 300; color: '#22cc22' }", QUrl());
  auto *fixture = qobject_cast<QQuickItem *>(component.create());
  if (!fixture)
    return 2;
  view.setContent(QUrl(), &component, fixture);
  view.show();
  RecordingController recorder;
  recorder.setOutputDirectory(directory);
  recorder.setMicrophoneEnabled(false);
  recorder.setSystemAudioEnabled(true);
  QMediaPlayer player;
  QVideoSink video;
  QAudioBufferOutput audio;
  player.setVideoSink(&video);
  player.setAudioBufferOutput(&audio);
  bool done = false, scheduled = false, decoding = false;
  int lifecycle = 0, discarded = 0;
  QUrl saved;
  int green = 0, blue = 0, red = 0;
  qint64 audioUsecs = 0, pausedDuration = 0;
  auto finish = [&](QString error) {
    if (done) return;
    done = true;
    QJsonObject report{{"success", error.isEmpty()}, {"error", error},
                       {"durationMs", double(player.duration())},
                       {"audioDurationMs", audioUsecs / 1000.0},
                       {"greenFrames", green}, {"blueFrames", blue}, {"pausedRedFrames", red},
                       {"pointerSamples", recorder.lastPointerSamples().size()},
                       {"recordingMarkers", recorder.lastRecordingMarkers().size()}};
    QFile file(dir.filePath("recording-controls-smoke.json"));
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(report).toJson());
    player.stop();
    recorder.stop();
    app.exit(error.isEmpty() ? 0 : 1);
  };
  QObject::connect(&video, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame &frame) {
    if (!frame.isValid()) return;
    const QImage image = frame.toImage();
    const QColor color = image.pixelColor(image.width() / 2, image.height() / 2);
    if (color.green() > color.red() + 70 && color.green() > color.blue() + 70) ++green;
    if (color.blue() > color.red() + 70 && color.blue() > color.green() + 70) ++blue;
    if (color.red() > color.green() + 70 && color.red() > color.blue() + 70) ++red;
  });
  QObject::connect(&audio, &QAudioBufferOutput::audioBufferReceived, &app,
                   [&](const QAudioBuffer &buffer) { if (buffer.isValid()) audioUsecs += buffer.duration(); });
  QObject::connect(&player, &QMediaPlayer::errorOccurred, &app,
                   [&](QMediaPlayer::Error, const QString &message) { finish(message); });
  QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &app, [&](QMediaPlayer::MediaStatus status) {
    if (status != QMediaPlayer::EndOfMedia) return;
    QString error;
    if (!green || !blue || red) error = "Paused frames leaked or resumed content missing";
    if (player.duration() < 2400 || player.duration() > 3900) error = "Paused wall time was not removed";
    if (qAbs(audioUsecs / 1000 - player.duration()) > 300) error = "Audio/video duration mismatch";
    double lastTime = -1;
    for (const auto &sample : recorder.lastPointerSamples()) {
      const double time = sample.toMap().value("time").toDouble();
      if (time < lastTime || time > 3.9 || (lastTime >= 0 && time - lastTime > .6))
        error = "Pointer pause timeline gap";
      lastTime = time;
    }
    const auto markers = recorder.lastRecordingMarkers();
    if (markers.size() != 2 || markers[1].toMap().value("sourceTime").toDouble() -
        markers[0].toMap().value("sourceTime").toDouble() > .6)
      error = "Recording marker pause timeline gap";
    if (lastTime < 2.3) error = "Pointer track incomplete";
    if (!error.isEmpty()) { finish(error); return; }
    saved = recorder.lastRecording();
    lifecycle = 1;
    recorder.start();
  });
  QObject::connect(&recorder, &RecordingController::changed, &app, [&] {
    if (done) return;
    if (!recorder.error().isEmpty()) { finish(recorder.error()); return; }
    if (lifecycle && recorder.recording()) {
      const int take = lifecycle;
      lifecycle = 0;
      QTimer::singleShot(500, &app, [&, take] {
        if (take == 1) recorder.restart(); else recorder.discard();
      });
      return;
    }
    if (recorder.recording() && !scheduled) {
      scheduled = true;
      QTimer::singleShot(1500, &app, [&] {
        if (!recorder.addRecordingMarker()) { finish("Marker rejected during capture"); return; }
        recorder.pause();
        if (!recorder.paused()) { finish("Pause state missing"); return; }
        if (recorder.addRecordingMarker()) { finish("Paused marker accepted"); return; }
        fixture->setProperty("color", QColor("#dd2222"));
        QTimer::singleShot(300, &app, [&] { pausedDuration = recorder.duration(); });
        QTimer::singleShot(1800, &app, [&] {
          if (qAbs(recorder.duration() - pausedDuration) > 100) { finish("Duration advanced while paused"); return; }
          fixture->setProperty("color", QColor("#2222dd"));
          // Ensure the fixture presents blue before capture is resumed.
          QTimer::singleShot(100, &app, [&] { recorder.resume(); if (!recorder.addRecordingMarker()) { finish("Resumed marker rejected"); return; } QTimer::singleShot(1500, &app, [&] { recorder.stop(); }); });
        });
      });
    }
    if (scheduled && !recorder.busy() && !decoding && !recorder.lastRecording().isEmpty()) {
      decoding = true;
      player.setSource(recorder.lastRecording());
      player.play();
    }
  });
  QObject::connect(&recorder, &RecordingController::restartRequested, &app, [&] {
    if (recorder.busy() || recorder.lastRecording() != saved ||
        !QFileInfo::exists(saved.toLocalFile()) ||
        dir.entryList({"*.mp4"}, QDir::Files).size() != 1) {
      finish("Re-record published or retained the abandoned take");
      return;
    }
    lifecycle = 2;
    QTimer::singleShot(0, &app, [&] { recorder.start(); });
  });
  QObject::connect(&recorder, &RecordingController::discarded, &app, [&] {
    if (++discarded < 2) return;
    const bool safe = !recorder.busy() && recorder.lastRecording() == saved && QFileInfo(saved.toLocalFile()).size() > 0 &&
                      dir.entryList({"*.mp4"}, QDir::Files).size() == 1;
    finish(safe ? QString() : QString("Discard/re-record damaged saved media or retained abandoned take"));
  });
  QTimer::singleShot(600, &app, [&] {
    recorder.refreshSources();
    int selected = -1;
    const auto sources = recorder.sources();
    for (int i = 0; i < sources.size(); ++i)
      if (sources[i].toMap().value("label").toString() == view.title()) selected = i;
    if (selected < 0) { finish("Self window capture unavailable"); return; }
    recorder.setSelectedSource(selected);
    recorder.start();
  });
  QTimer::singleShot(25000, &app, [&] { finish("Recording controls smoke timeout"); });
  return app.exec();
}
