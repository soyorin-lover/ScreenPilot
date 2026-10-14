#include "RecordingBarSmokeTest.h"
#include "AppRuntime.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

// Drives the real recorder page: start from the main window, then the floating
// bar alone (marker, menu, restart confirmation, stop) until the main window
// returns. The main window must stay hidden for the whole take and restart.
int runRecordingBarSmokeTest(QGuiApplication &app, AppRuntime &runtime, const QString &directory) {
  QDir dir(directory);
  if (directory.isEmpty() || (dir.exists() && !dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) ||
      !QDir().mkpath(directory))
    return 2;
  auto *root = qobject_cast<QQuickWindow *>(runtime.root());
  auto *bar = root ? root->findChild<QQuickWindow *>("recordingBar") : nullptr;
  if (!root || !bar) return 3;
  QQuickView fixture;
  fixture.setTitle("Recording bar fixture");
  QQmlComponent component(fixture.engine());
  component.setData("import QtQuick\nRectangle { width: 480; height: 320; color: '#2266cc' }", QUrl());
  auto *item = qobject_cast<QQuickItem *>(component.create());
  if (!item) return 3;
  fixture.setContent(QUrl(), &component, item);
  fixture.show();
  auto &recorder = runtime.recorder;
  recorder.setOutputDirectory(directory);
  recorder.setMicrophoneEnabled(false);
  recorder.setSystemAudioEnabled(false);
  root->show();

  QJsonObject report;
  int stage = 0, takes = 0;
  bool done = false, mainShownDuringTake = false, secondTakeRecording = false;
  QElapsedTimer phase, total;
  phase.start(); total.start();
  auto child = [bar](const char *name) { return bar->findChild<QQuickItem *>(name); };
  auto grab = [&](const QString &name) { report[name] = bar->grabWindow().save(dir.filePath(name + ".png")); };
  auto finish = [&](const QString &error) {
    if (done) return;
    done = true;
    report["success"] = error.isEmpty();
    report["error"] = error;
    report["mainShownDuringTake"] = mainShownDuringTake;
    QFile file(dir.filePath("recording-bar-smoke.json"));
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(report).toJson());
    if (recorder.busy()) recorder.stop();
    app.exit(error.isEmpty() ? 0 : 1);
  };
  QObject::connect(root, &QWindow::visibleChanged, &app, [&](bool visible) {
    if (visible && (stage == 1 || stage == 2)) mainShownDuringTake = true;
  });
  QTimer poll;
  poll.setInterval(100);
  QObject::connect(&poll, &QTimer::timeout, &app, [&] {
    if (done) return;
    if (total.elapsed() > 60000) { finish("Timeout at stage " + QString::number(stage) + ": " + recorder.error()); return; }
    if (stage == 0 && phase.elapsed() > 900) {
      recorder.refreshSources();
      const auto sources = recorder.sources();
      // SCREENPILOT_TEST_BAR_SCREEN=1 records the whole display instead, to
      // check that the bar itself stays out of the captured video.
      const bool screenTake = qEnvironmentVariableIntValue("SCREENPILOT_TEST_BAR_SCREEN") == 1;
      report["screenTake"] = screenTake;
      int selected = -1;
      for (int i = 0; i < sources.size(); ++i) {
        const auto source = sources[i].toMap();
        if (screenTake ? source.value("kind").toString() == "screen" && selected < 0
                       : source.value("label").toString() == fixture.title()) selected = i;
      }
      if (selected < 0) { finish("Fixture window not capturable"); return; }
      recorder.setSelectedSource(selected);
      QMetaObject::invokeMethod(root, "beginRecording");
      report["mainHiddenAtStart"] = !root->isVisible();
      report["barShownAtStart"] = bar->isVisible();
      stage = 1; phase.restart();
    } else if (stage == 1 && recorder.recording()) {
      if (phase.elapsed() < 1500) return;
      report["captureExcluded"] = bar->property("captureExcluded").toBool();
      auto *marker = child("recordingBarMarker");
      if (!marker || !QMetaObject::invokeMethod(marker, "click")) { finish("Marker button missing"); return; }
      report["markersAfterClick"] = int(recorder.recordingMarkers().size());
      grab("bar");
      if (QScreen *screen = bar->screen()) {
        const QRect logical = bar->geometry().translated(-screen->geometry().topLeft());
        const double dpr = screen->devicePixelRatio();
        report["barPhysicalRect"] = QJsonObject{{"x", logical.x() * dpr}, {"y", logical.y() * dpr},
                                                {"width", logical.width() * dpr}, {"height", logical.height() * dpr}};
      }
      QMetaObject::invokeMethod(bar, "setMode", Q_ARG(QVariant, QVariant("menu")));
      QTimer::singleShot(300, &app, [&] {
        grab("bar-menu");
        QMetaObject::invokeMethod(bar, "setMode", Q_ARG(QVariant, QVariant("restart")));
        QTimer::singleShot(300, &app, [&] {
          grab("bar-restart-confirm");
          auto *confirm = child("recordingBarConfirm");
          if (!confirm || !QMetaObject::invokeMethod(confirm, "click")) { finish("Confirm button missing"); return; }
          takes = 1;
          stage = 2; phase.restart();
        });
      });
      stage = 10; // waiting for the queued grabs
    } else if (stage == 2 && recorder.recording()) {
      // Time the second take from when it actually records.
      if (!secondTakeRecording) { secondTakeRecording = true; phase.restart(); return; }
      if (phase.elapsed() < 1500) return;
      report["restartedWithoutMain"] = !root->isVisible() && bar->isVisible();
      QMetaObject::invokeMethod(child("recordingBarMarker"), "click");
      auto *stop = child("recordingBarStop");
      if (!stop || !QMetaObject::invokeMethod(stop, "click")) { finish("Stop button missing"); return; }
      takes = 2;
      stage = 3; phase.restart();
    } else if (stage == 3 && !recorder.busy()) {
      if (!root->isVisible() || bar->isVisible()) { if (phase.elapsed() > 8000) finish("Main window did not return"); return; }
      report["takes"] = takes;
      report["savedMarkers"] = int(recorder.lastRecordingMarkers().size());
      report["saved"] = !recorder.lastRecording().isEmpty();
      // Pointer samples must map into the captured area; a screen take used to
      // mark every sample outside (no clicks for auto zoom).
      int inside = 0;
      for (const auto &sample : recorder.lastPointerSamples()) inside += sample.toMap().value("inside").toBool();
      report["pointerSamples"] = int(recorder.lastPointerSamples().size());
      report["pointerInsideSamples"] = inside;
      const bool ok = report["mainHiddenAtStart"].toBool() && report["barShownAtStart"].toBool() &&
          report["captureExcluded"].toBool() && report["markersAfterClick"].toInt() == 1 &&
          report["restartedWithoutMain"].toBool() && !mainShownDuringTake &&
          report["savedMarkers"].toInt() == 1 && report["saved"].toBool() &&
          (!report["screenTake"].toBool() || report["pointerInsideSamples"].toInt() > 0);
      finish(ok ? QString() : QStringLiteral("Recording bar flow check failed"));
    }
  });
  poll.start();
  return app.exec();
}
