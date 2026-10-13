#include "MarkerSmokeTest.h"
#include "ProjectController.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <cmath>
int verifyRecordingMarkers(QGuiApplication &, const QString &sourcePath,
                           const QString &outputDirectory) {
  if (!QDir().mkpath(outputDirectory)) return 2;
  ProjectController tail;
  const QVariantList tailMarkers{QVariantMap{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"number", 1}, {"sourceTime", 3.7}}};
  const bool tailClamped = tail.newRecording(QUrl::fromLocalFile(sourcePath), 3, {}, tailMarkers) &&
      tail.recordingMarkers().first().toMap().value("sourceTime").toDouble() == 3;
  ProjectController project;
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QVariantList markers{QVariantMap{{"id", id}, {"number", 1}, {"sourceTime", 1.0}}};
  bool passed = tailClamped && project.newRecording(QUrl::fromLocalFile(sourcePath), 3, {}, markers);
  passed = passed && project.markerTime(id) == 1 && project.split(.5);
  passed = passed && project.setClipRate(1, 2) && std::abs(project.markerTime(id) - .75) < 1e-9;
  passed = passed && project.moveClip(1, 0) && std::abs(project.markerTime(id) - .25) < 1e-9;
  passed = passed && project.removeRecordingMarker(id) && project.markerTime(id) < 0;
  project.undo();
  passed = passed && project.recordingMarkers().size() == 1;
  project.setExportOption("resolution", "1440p");
  project.setExportOption("fps", 30);
  const QString path = QDir(outputDirectory).filePath("markers.spproj");
  passed = passed && project.saveAs(QUrl::fromLocalFile(path)) && !project.dirty();
  ProjectController reopened;
  passed = passed && reopened.openProject(QUrl::fromLocalFile(path)) &&
      reopened.recordingMarkers() == project.recordingMarkers() &&
      reopened.exportSettings() == project.exportSettings();
  reopened.setSelectedClip(0);
  passed = passed && reopened.removeSelected() && reopened.markerTime(id) < 0 &&
      reopened.recordingMarkers().size() == 1;
  reopened.undo();
  passed = passed && std::abs(reopened.markerTime(id) - .25) < 1e-9 && !reopened.dirty();
  reopened.removeRecordingMarker(id);
  passed = passed && reopened.dirty();
  reopened.undo();
  passed = passed && !reopened.dirty();
  reopened.setExportOption("fps", 60);
  passed = passed && reopened.dirty();
  reopened.undo();
  passed = passed && !reopened.dirty();
  QFile json(QDir(path).filePath("project.json"));
  if (!json.open(QIODevice::ReadOnly)) return 2;
  auto root = QJsonDocument::fromJson(json.readAll()).object();
  json.close();
  auto settings = root.value("screenPilot").toObject();
  settings["recordingMarkers"] = QJsonArray{QJsonObject{{"id", id}, {"number", 1}, {"sourceTime", -1}}};
  root["screenPilot"] = settings;
  if (!json.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
  json.write(QJsonDocument(root).toJson()); json.close();
  passed = passed && !reopened.openProject(QUrl::fromLocalFile(path)) &&
      reopened.recordingMarkers().size() == 1;
  settings.remove("recordingMarkers"); settings.remove("exportSettings");
  root["screenPilot"] = settings; root["formatVersion"] = 4;
  if (!json.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
  json.write(QJsonDocument(root).toJson()); json.close();
  ProjectController legacy;
  passed = passed && legacy.openProject(QUrl::fromLocalFile(path)) &&
      legacy.recordingMarkers().isEmpty() && legacy.exportSettings().value("fps").toInt() == 60 &&
      legacy.exportSettings().value("resolution").toString() == "source";
  QFile report(QDir(outputDirectory).filePath("marker-smoke.json"));
  if (!report.open(QIODevice::WriteOnly)) return 2;
  report.write(QJsonDocument(QJsonObject{{"passed", passed},
      {"savedMarkers", reopened.recordingMarkers().size()},
      {"mappedTime", reopened.markerTime(id)}}).toJson());
  return passed ? 0 : 1;
}
