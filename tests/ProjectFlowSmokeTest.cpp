#include "ProjectFlowSmokeTest.h"
#include "AutoFraming.h"
#include "ProjectController.h"
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
int runProjectFlowSmokeTest() {
  QTemporaryDir directory;
  if (!directory.isValid())
    return 2;
  const QString source = directory.filePath("source.mp4");
  QFile media(source);
  if (!media.open(QIODevice::WriteOnly))
    return 2;
  media.write("model test fixture");
  media.close();
  ProjectController project;
  auto near = [](double a, double b) { return std::abs(a - b) < 1e-8; };
  if (!project.newRecording(QUrl::fromLocalFile(source), 12) ||
      !project.split(4) || !project.moveClip(1, 0) ||
      !project.setClipRate(0, 2) || !near(project.duration(), 8) ||
      !near(project.sourceTime(1), 6) || !near(project.sourceTime(5), 1))
    return 3;
  project.undo();
  if (!near(project.duration(), 12))
    return 4;
  project.redo();
  if (!near(project.duration(), 8))
    return 5;
  project.setFramingOption("scale", 2.5);
  const QUrl location =
      QUrl::fromLocalFile(directory.filePath("saved.spproj"));
  if (!project.saveAs(location) || project.dirty())
    return 6;
  ProjectController loaded;
  if (!loaded.openProject(location) || loaded.dirty() ||
      !near(loaded.duration(), 8) || !near(loaded.sourceTime(5), 1) ||
      loaded.framingSettings().value("scale").toDouble() != 2.5)
    return 7;
  if (!loaded.setClipRate(0, 100) || !loaded.dirty())
    return 8;
  loaded.undo();
  if (loaded.dirty())
    return 9;
  if (loaded.setClipRate(0, 0) || loaded.moveClip(-1, 0))
    return 10;
  // Repeated source intervals are valid; chronology is the array order.
  QFile json(directory.filePath("saved.spproj/project.json"));
  if (!json.open(QIODevice::ReadOnly))
    return 11;
  auto root = QJsonDocument::fromJson(json.readAll()).object();
  json.close();
  auto timeline = root["timeline"].toObject();
  auto sequence = timeline["sourceSequence"].toObject();
  auto segments = sequence["segments"].toArray();
  auto last = segments[1].toObject();
  last["sourceStart"] = 4;
  segments[1] = last;
  sequence["segments"] = segments;
  timeline["sourceSequence"] = sequence;
  root["timeline"] = timeline;
  if (!json.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return 11;
  json.write(QJsonDocument(root).toJson());
  json.close();
  ProjectController repeated;
  if (!repeated.openProject(location) || !near(repeated.sourceTime(5), 5))
    return 12;
  QVariantList samples;
  for (int i = 0; i <= 120; ++i) {
    const double t = i / 30.;
    samples.append(
        QVariantMap{{"time", t},
                    {"x", std::min(.9, .35 + std::max(0.0, t - .5) * .6)},
                    {"y", .5},
                    {"inside", true},
                    {"click", i == 15}});
  }
  const auto following =
      AutoFraming::generate(samples, 6, {{"follow", true}, {"scale", 2.0}});
  const auto fixed =
      AutoFraming::generate(samples, 6, {{"follow", false}, {"scale", 2.0}});
  const auto moved = AutoFraming::evaluate(following, 2),
             stationary = AutoFraming::evaluate(fixed, 2);
  if (following.isEmpty() || moved.centerX < stationary.centerX + .1 ||
      AutoFraming::evaluate(following, 5.9).scale != 1)
    return 13;
  for (int i = 1; i < 360; ++i) {
    const auto a = AutoFraming::evaluate(following, (i - 1) / 60.),
               b = AutoFraming::evaluate(following, i / 60.);
    if (!std::isfinite(b.scale) || std::abs(b.centerX - a.centerX) > .08 ||
        std::abs(b.scale - a.scale) > .15)
      return 14;
  }
  ProjectController cameraProject;
  if (!cameraProject.newRecording(QUrl::fromLocalFile(source), 12) ||
      !cameraProject.setCameraMedia(QUrl::fromLocalFile(source), .12, false))
    return 15;
  cameraProject.setCameraOption("shape", "circle");
  cameraProject.setCameraOption("width", .4);
  const auto cameraLocation =
      QUrl::fromLocalFile(directory.filePath("camera.dogscproject"));
  if (!cameraProject.saveAs(cameraLocation))
    return 16;
  ProjectController reopenedCamera;
  if (!reopenedCamera.openProject(cameraLocation) ||
      reopenedCamera.cameraSource().isEmpty() ||
      reopenedCamera.cameraSettings().value("shape") != "circle" ||
      reopenedCamera.cameraSettings().value("offset").toDouble() != .12)
    return 17;
  reopenedCamera.setCameraOption("width", .5);
  reopenedCamera.undo();
  if (reopenedCamera.dirty())
    return 18;
  qInfo() << "Project flow smoke passed: reorder, rate, mapping, history, "
             "save/open, repeated source intervals";
  return 0;
}
