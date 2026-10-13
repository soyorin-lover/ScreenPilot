#include "VisualUiSmokeTest.h"
#include "CanvasPreview.h"
#include "CanvasRenderer.h"
#include "ProjectController.h"
#include "ScenePresetController.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTimer>
int runVisualUiSmokeTest(QGuiApplication &app, QObject *root,
                         ProjectController &project,
                         ScenePresetController &presets, const QString &source,
                         const QString &directory) {
  auto editor = root->findChild<QObject *>("editorView");
  auto preview = root->findChild<CanvasPreview *>("canvasPreview");
  if (!editor || !preview ||
      !project.newRecording(QUrl::fromLocalFile(source), 3))
    return 2;
  root->setProperty("editing", true);
  QTimer timer;
  QElapsedTimer elapsed;
  elapsed.start();
  int stage = 0;
  int redPlayback = 0, greenPlayback = 0;
  qint64 readyAt = -1;
  bool done = false;
  QJsonObject results;
  QJsonArray playbackSamples;
  auto finish = [&](QString error) {
    if (done)
      return;
    done = true;
    timer.stop();
    results["success"] = error.isEmpty();
    results["playbackSamples"] = playbackSamples;
    results["error"] = error;
    if (auto window = qobject_cast<QQuickWindow *>(root))
      window->grabWindow().save(QDir(directory).filePath("visual-editor.png"));
    QFile report(QDir(directory).filePath("visual-ui-result.json"));
    if (!report.open(QIODevice::WriteOnly) ||
        report.write(QJsonDocument(results).toJson()) < 0)
      error = "Report write failed";
    app.exit(error.isEmpty() ? 0 : 1);
  };
  QObject::connect(&timer, &QTimer::timeout, &app, [&] {
    if (elapsed.elapsed() > 20000) {
      finish("Visual UI timeout: " + preview->error());
      return;
    }
    if (!preview->error().isEmpty()) {
      finish(preview->error());
      return;
    }
    if (stage == 0) {
      if (!preview->hasFrame() || editor->property("primingPreview").toBool())
        return;
      if (!project.setBackgroundVideo(QUrl::fromLocalFile(source))) {
        finish(project.error());
        return;
      }
      project.setCanvasOption("padding", 200);
      project.setCanvasOption("screenFrame", "browserDark");
      project.setCanvasOption("screenFrameTitle", "演示窗口");
      project.setCanvasOption("screenFrameBrowserAddress",
                              "https://example.invalid");
      project.setCanvasOption("introEnabled", true);
      project.setCanvasOption("introStyle", "scale");
      project.setCanvasOption("introDuration", .8);
      if (!presets.savePreset("Generated visual scene")) {
        finish(presets.error());
        return;
      }
      project.setCanvasOption("screenFrame", "none");
      const bool applied =
          presets.applyPreset(
              presets.presets().first().toMap().value("id").toString()) &&
          project.canvasSettings().value("screenFrame").toString() ==
              "browserDark" &&
          project.canvasSettings().value("introEnabled").toBool();
      results["presetEffects"] = applied;
      if (!applied) {
        finish("Visual preset settings lost");
        return;
      }
      const auto saved = QDir(directory).filePath("visuals.spproj");
      ProjectController reopened;
      const bool roundtrip =
          project.saveAs(QUrl::fromLocalFile(saved)) &&
          reopened.openProject(QUrl::fromLocalFile(saved)) &&
          reopened.canvasSettings() == project.canvasSettings();
      results["projectRoundtrip"] = roundtrip;
      if (!roundtrip) {
        finish("Visual project roundtrip failed");
        return;
      }
      editor->setProperty("inspectorTab", 4);
      QMetaObject::invokeMethod(editor, "seek", Q_ARG(QVariant, QVariant(1.5)));
      stage = 1;
    } else {
      if (!preview->hasFrame() || editor->property("primingPreview").toBool() ||
          (stage == 1 && preview->frameTime() < 1.1)) {
        readyAt = -1;
        return;
      }
      if (stage == 1 && readyAt < 0) {
        readyAt = elapsed.elapsed();
        return;
      }
      if (stage == 1 && elapsed.elapsed() - readyAt < 600)
        return;
      auto window = qobject_cast<QQuickWindow *>(root);
      if (!window) {
        finish("Editor window missing");
        return;
      }
      // Inspect the actual painted frame without forcing a GPU readback on
      // every playback tick; grabWindow can itself stall the media clock.
      const QImage snapshot = preview->renderedImage();
      const QPoint pixel(qRound(snapshot.width()*.02), qRound(snapshot.height()*.02));
      const QColor color = snapshot.rect().contains(pixel)
                               ? snapshot.pixelColor(pixel)
                               : QColor{};
      const bool backgroundVisible =
          color.green() > 100 && color.red() < 90 && color.blue() < 90;
      if (stage == 1) {
        results["backgroundPreviewAfterSave"] = backgroundVisible;
        if (!backgroundVisible) {
          finish("Dynamic background not visible after save/seek");
          return;
        }
        QMetaObject::invokeMethod(editor, "seek",
                                  Q_ARG(QVariant, QVariant(0.0)));
        QMetaObject::invokeMethod(editor, "playPause");
        stage = 2;
      } else {
        const double time = editor->property("positionSeconds").toDouble();
        playbackSamples.append(QJsonObject{{"time", time}, {"sourceTime", preview->frameTime()},
                                           {"red", color.red()}, {"green", color.green()}, {"blue", color.blue()}});
        if (time > .2 && time < .9 && color.red() > 120 && color.green() < 80)
          ++redPlayback;
        if (time > 1.2 && time < 1.9 && backgroundVisible)
          ++greenPlayback;
        if (time > 2.05) {
          QMetaObject::invokeMethod(editor, "playPause");
          results["redBackgroundPlaybackSamples"] = redPlayback;
          results["greenBackgroundPlaybackSamples"] = greenPlayback;
          finish(redPlayback > 0 && greenPlayback > 0
                     ? QString{}
                     : QStringLiteral("Background stalled during playback"));
        }
      }
    }
  });
  timer.start(100);
  return app.exec();
}
