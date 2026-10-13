#include "PresetSmokeTest.h"
#include "ScenePresetController.h"
#include "ProjectController.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

int runPresetSmokeTest(QGuiApplication &, const QString &directory) {
  QDir().mkpath(directory);
  QTemporaryDir isolated(QDir(directory).filePath("presets-XXXXXX"));
  if (!isolated.isValid()) return 2;
  const QString original = QDir(isolated.path()).filePath("original.png");
  QImage image(32, 32, QImage::Format_RGB32); image.fill(Qt::red); image.save(original);
  const QString media = QDir(directory).filePath("fixture.mp4");
  // ProjectController only needs an existing source for this settings-only flow.
  QFile fixture(media); if (!fixture.exists()) { fixture.open(QIODevice::WriteOnly); fixture.write("fixture"); fixture.close(); }
  ProjectController first;
  bool ok = first.newRecording(QUrl::fromLocalFile(media), 10);
  ok = first.setBackgroundImage(QUrl::fromLocalFile(original)) && ok;
  first.setCanvasOption("padding", 40);
  const QString store = QDir(isolated.path()).filePath("store");
  ScenePresetController presets(&first, store);
  ok = presets.savePreset("Studio") && ok;
  const QString id = presets.presets().value(0).toMap().value("id").toString();
  ok = presets.setDefaultPreset(id) && ok;
  ok = presets.resetDefault() && presets.defaultPresetId().isEmpty() && ok;
  ok = presets.setDefaultPreset(id) && ok;
  QFile::remove(original);
  ProjectController second;
  ok = second.newRecording(QUrl::fromLocalFile(media), 12) && ok;
  const auto before = second.sceneSettings();
  ScenePresetController reopened(&second, store);
  ok = reopened.defaultPresetId() == id && reopened.applyDefault() && ok;
  const auto applied = second.sceneSettings();
  const QString retained = applied.value("canvas").toMap().value("backgroundImage").toString();
  const bool copied = QFile::exists(retained) && retained != original;
  second.undo();
  const bool singleUndo = second.sceneSettings() == before;
  second.redo();
  ok = reopened.updatePreset(id, "Updated") && ok;
  ok = reopened.deletePreset(id) && ok;
  const bool survivedDeletion = QFile::exists(retained);
  const bool projectSaved = second.saveAs(QUrl::fromLocalFile(QDir(isolated.path()).filePath("saved.screenpilot")));
  ScenePresetController finalStore(&second, store);
  const bool persisted = finalStore.presets().isEmpty() && finalStore.defaultPresetId().isEmpty();
  ok = ok && copied && singleUndo && survivedDeletion && persisted && projectSaved;
  QFile report(QDir(directory).filePath("preset-smoke.json"));
  if (report.open(QIODevice::WriteOnly)) report.write(QJsonDocument(QJsonObject{{"passed", ok}, {"backgroundCopy", copied}, {"singleUndo", singleUndo}, {"backgroundSurvivesPresetDeletion", survivedDeletion}, {"persistence", persisted}}).toJson());
  return ok ? 0 : 1;
}

