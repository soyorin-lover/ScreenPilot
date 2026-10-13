#include "VisualProjectSmokeTest.h"
#include "ProjectController.h"
#include "ScenePresetController.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

int runVisualProjectSmokeTest(QGuiApplication &, const QString &directory, const QString &sourcePath) {
  QDir().mkpath(directory);
  QTemporaryDir temp(QDir(directory).filePath("visual-project-XXXXXX"));
  if (!temp.isValid()) return 2;
  const QString fixture = sourcePath;
  if (fixture.isEmpty() || !QFileInfo(fixture).isFile()) return 2;
  const QString video = QDir(temp.path()).filePath("background.mp4");
  if (!QFile::copy(fixture, video)) return 2;
  const QString imagePath = QDir(temp.path()).filePath("background.png");
  QImage image(32,32,QImage::Format_RGB32); image.fill(Qt::red); image.save(imagePath);
  ProjectController project;
  bool ok = project.newRecording(QUrl::fromLocalFile(video), 4);
  ok = project.setBackgroundImage(QUrl::fromLocalFile(imagePath)) && ok;
  const auto imageSettings = project.canvasSettings();
  ok = project.setBackgroundVideo(QUrl::fromLocalFile(video)) && ok;
  const auto videoSettings = project.canvasSettings();
  auto dualScene = project.sceneSettings();
  auto dualCanvas = dualScene.value("canvas").toMap();
  dualCanvas["backgroundImage"] = imagePath;
  dualScene["canvas"] = dualCanvas;
  ok = !project.applySceneSettings(dualScene) && project.canvasSettings() == videoSettings && ok;
  const bool exclusive = videoSettings.value("backgroundImage").toString().isEmpty();
  project.undo(); const bool undo = project.canvasSettings() == imageSettings;
  project.redo(); const bool redo = project.canvasSettings() == videoSettings;
  const bool missingRejected = !project.setBackgroundVideo(QUrl::fromLocalFile(video + ".missing")) && project.canvasSettings() == videoSettings;
  project.setCanvasOption("screenFrame", "browserDark");
  project.setCanvasOption("introEnabled", true);
  ScenePresetController presets(&project, QDir(temp.path()).filePath("presets"));
  ok = presets.savePreset("Visual") && ok;
  const QString id = presets.presets().value(0).toMap().value("id").toString();
  project.clearBackgroundVideo();
  ok = presets.applyPreset(id) && ok;
  const QString retained = project.canvasSettings().value("backgroundVideo").toString();
  ok = presets.updatePreset(id, "Visual updated") && presets.deletePreset(id) && ok;
  const bool survived = QFile::exists(retained);
  auto oldSettings = project.sceneSettings();
  auto oldCanvas = oldSettings.value("canvas").toMap();
  oldCanvas.remove("backgroundVideo"); oldCanvas.remove("screenFrame"); oldCanvas.remove("introEnabled");
  oldSettings["canvas"] = oldCanvas;
  const QString oldStore = QDir(temp.path()).filePath("legacy-presets");
  QDir().mkpath(oldStore);
  QFile oldFile(QDir(oldStore).filePath("presets.json"));
  oldFile.open(QIODevice::WriteOnly);
  oldFile.write(QJsonDocument(QJsonObject::fromVariantMap({{"version",1},{"presets",QVariantList{QVariantMap{{"id",id},{"name","Old scene"},{"settings",oldSettings}}}}})).toJson()); oldFile.close();
  ScenePresetController oldPreset(&project, oldStore);
  const bool oldPresetDefaults = oldPreset.applyPreset(id) && project.canvasSettings().value("backgroundVideo").toString().isEmpty() && !project.canvasSettings().value("introEnabled").toBool();
  project.undo();
  const QString folder = QDir(temp.path()).filePath("saved.dogscproject");
  ok = project.saveAs(QUrl::fromLocalFile(folder)) && ok;
  QFile json(QDir(folder).filePath("project.json")); json.open(QIODevice::ReadOnly);
  auto root = QJsonDocument::fromJson(json.readAll()).object(); json.close();
  const QString relative = root.value("screenPilot").toObject().value("canvasSettings").toObject().value("backgroundVideo").toString();
  const bool portable = !relative.isEmpty() && !QDir::isAbsolutePath(relative) && QFile::exists(QDir(folder).filePath(relative));
  ProjectController reopened;
  ok = reopened.openProject(QUrl::fromLocalFile(folder)) && ok;
  const bool effects = reopened.canvasSettings().value("screenFrame").toString() == "browserDark" && reopened.canvasSettings().value("introEnabled").toBool();
  const auto beforeMissing = reopened.canvasSettings();
  QFile::remove(QDir(folder).filePath(relative));
  const bool protectedProject = !reopened.openProject(QUrl::fromLocalFile(folder)) && reopened.canvasSettings() == beforeMissing;
  root["formatVersion"] = 6;
  auto settings = root.value("screenPilot").toObject();
  auto canvas = settings.value("canvasSettings").toObject();
  for (const auto &key : {"backgroundVideo", "screenFrame", "screenFrameTitle", "screenFrameBrowserAddress", "introEnabled", "introStyle", "introDuration", "introTempo", "introOrder", "introElementGap"}) canvas.remove(key);
  settings["canvasSettings"] = canvas; root["screenPilot"] = settings;
  json.open(QIODevice::WriteOnly); json.write(QJsonDocument(root).toJson()); json.close();
  ProjectController legacy;
  const bool legacyDefaults = legacy.openProject(QUrl::fromLocalFile(folder)) && legacy.canvasSettings().value("backgroundVideo").toString().isEmpty() && !legacy.canvasSettings().value("introEnabled").toBool();
  ok = ok && exclusive && undo && redo && missingRejected && survived && portable && effects && protectedProject && legacyDefaults && oldPresetDefaults;
  QFile report(QDir(directory).filePath("visual-project-smoke.json"));
  if (report.open(QIODevice::WriteOnly)) report.write(QJsonDocument(QJsonObject{{"passed",ok},{"exclusive",exclusive},{"undo",undo},{"redo",redo},{"missingRejected",missingRejected},{"presetAssetSurvives",survived},{"portableAsset",portable},{"effectsPersist",effects},{"missingAssetProtectsProject",protectedProject},{"legacy6Defaults",legacyDefaults},{"oldPresetDefaults",oldPresetDefaults}}).toJson());
  return ok ? 0 : 1;
}
