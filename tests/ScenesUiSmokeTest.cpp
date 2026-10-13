#include "ScenesUiSmokeTest.h"
#include "CanvasPreview.h"
#include "ProjectController.h"
#include "ScenePresetController.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTimer>
#include <cmath>

int runScenesUiSmokeTest(QGuiApplication &app, QObject *root,
                         ProjectController &project,
                         ScenePresetController &presets, const QString &source,
                         const QString &directory) {
  auto editor = root->findChild<QObject *>("editorView");
  auto preview = root->findChild<CanvasPreview *>("canvasPreview");
  auto crop = root->findChild<QObject *>("cropEditor");
  auto panel = root->findChild<QObject *>("scenePresetPanel");
  if (!editor || !preview || !crop || !panel ||
      !project.newRecording(QUrl::fromLocalFile(source), 3))
    return 2;
  root->setProperty("editing", true);
  const QVariantMap before = project.canvasSettings();
  QJsonObject results;
  QTimer timer;
  QElapsedTimer elapsed;
  elapsed.start();
  int stage = 0;
  bool done = false;
  auto finish = [&](QString error) {
    if (done)
      return;
    done = true;
    timer.stop();
    results["success"] = error.isEmpty();
    results["error"] = error;
    if (auto window = qobject_cast<QQuickWindow *>(root))
      window->grabWindow().save(QDir(directory).filePath("scenes-editor.png"));
    QFile report(QDir(directory).filePath("scenes-ui-result.json"));
    if (!report.open(QIODevice::WriteOnly) ||
        report.write(QJsonDocument(results).toJson()) < 0)
      error = "Report write failed";
    app.exit(error.isEmpty() ? 0 : 1);
  };
  auto setDraft = [&] {
    crop->setProperty("cropX", .25);
    crop->setProperty("cropY", .25);
    crop->setProperty("cropWidth", .5);
    crop->setProperty("cropHeight", .5);
  };
  QObject::connect(&timer, &QTimer::timeout, &app, [&] {
    if (elapsed.elapsed() > 12000) {
      finish("Scene UI timeout");
      return;
    }
    if (stage == 0) {
      if (!preview->hasFrame() || editor->property("primingPreview").toBool())
        return;
      if (!QMetaObject::invokeMethod(editor, "openCrop")) {
        finish("Crop opener missing");
        return;
      }
      stage = 1;
    } else if (stage == 1) {
      if (!crop->property("opened").toBool())
        return;
      setDraft();
      QMetaObject::invokeMethod(crop, "reject");
      const bool cancelled = project.canvasSettings() == before;
      results["cropCancel"] = cancelled;
      if (!cancelled) {
        finish("Cancelled crop changed project");
        return;
      }
      stage = 2;
    } else if (stage == 2) {
      if (crop->property("visible").toBool())
        return;
      QMetaObject::invokeMethod(editor, "openCrop");
      stage = 3;
    } else if (stage == 3) {
      if (!crop->property("opened").toBool())
        return;
      setDraft();
      QMetaObject::invokeMethod(crop, "accept");
      const bool committed =
          project.canvasSettings().value("cropWidth").toDouble() == .5;
      results["cropCommit"] = committed;
      if (!committed) {
        finish("Crop confirmation failed");
        return;
      }
      const QString id = project.addManualFraming(.5);
      const bool manual =
          !id.isEmpty() &&
          project.updateManualFraming(id, {{"end", 2.0},
                                           {"scale", 1.7},
                                           {"enter", 0.0},
                                           {"exit", 0.0},
                                           {"curve", "linear"}}) &&
          std::abs(project.framingAt(.75).value("scale").toDouble() - 1.7) <
              .001;
      results["manualZoom"] = manual;
      if (!manual) {
        finish("Manual framing did not reach preview model");
        return;
      }
      const auto savedScene = project.sceneSettings();
      if (!presets.savePreset("Generated scene")) {
        finish(presets.error());
        return;
      }
      const QString presetId =
          presets.presets().first().toMap().value("id").toString();
      project.setCanvasOption("backgroundColor", "#404040");
      const auto changedScene = project.sceneSettings();
      bool atomic = presets.applyPreset(presetId) &&
                    project.sceneSettings() == savedScene;
      project.undo();
      atomic = atomic && project.sceneSettings() == changedScene;
      project.redo();
      results["presetSingleUndo"] = atomic;
      if (!atomic) {
        finish("Preset was not one history operation");
        return;
      }
      const QString saved = QDir(directory).filePath("scenes.dogscproject");
      ProjectController reopened;
      const bool roundtrip =
          project.saveAs(QUrl::fromLocalFile(saved)) &&
          reopened.openProject(QUrl::fromLocalFile(saved)) &&
          reopened.manualFramingSegments() == project.manualFramingSegments() &&
          reopened.canvasSettings() == project.canvasSettings();
      results["projectRoundtrip"] = roundtrip;
      if (!roundtrip) {
        finish("Scene project roundtrip failed");
        return;
      }
      editor->setProperty("inspectorTab", 3);
      QMetaObject::invokeMethod(panel, "open");
      stage = 4;
    } else if (stage == 4) {
      if (!panel->property("opened").toBool())
        return;
      results["presetPanelShown"] = true;
      QMetaObject::invokeMethod(panel, "close");
      stage = 5;
    } else if (!panel->property("visible").toBool() && preview->hasFrame() &&
               !editor->property("primingPreview").toBool()) {
      if (stage == 5) {
        stage = 6;
        return;
      }
      const auto window = qobject_cast<QQuickWindow *>(root);
      const QImage snapshot = window ? window->grabWindow() : QImage{};
      const QPointF center = preview->mapToScene(
          QPointF(preview->width() / 2, preview->height() / 2));
      const double dpr = window ? window->effectiveDevicePixelRatio() : 1;
      const QPoint pixel(qRound(center.x() * dpr), qRound(center.y() * dpr));
      const QColor color = snapshot.rect().contains(pixel)
                               ? snapshot.pixelColor(pixel)
                               : QColor{};
      const bool visible =
          color.red() > 120 && color.green() < 80 && color.blue() < 80;
      results["previewAfterSave"] = visible;
      finish(visible ? QString{}
                     : QStringLiteral("Scene preview missing after save"));
    }
  });
  timer.start(120);
  return app.exec();
}
