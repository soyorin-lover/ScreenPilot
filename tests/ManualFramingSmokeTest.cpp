#include "ManualFramingSmokeTest.h"
#include "ProjectController.h"
#include "AutoFraming.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
int verifyManualFraming(QGuiApplication &, const QString &sourcePath,const QString &outputDirectory) {
  QDir().mkpath(outputDirectory);
  ProjectController p;
  bool passed=p.newRecording(QUrl::fromLocalFile(sourcePath),10);
  const auto id=p.addManualFraming(2);
  passed=passed && !id.isEmpty() && !p.autoZoomEnabled() && p.framingAt(3).value("scale").toDouble()>1.5;
  p.undo(); passed=passed && p.manualFramingSegments().isEmpty() && p.selectedManualFraming().isEmpty();
  p.redo(); passed=passed && p.manualFramingSegments().size()==1 && p.selectedManualFraming().isEmpty();
  p.setSelectedManualFraming(id);
  const auto before=p.manualFramingSegments();
  passed=passed && !p.updateManualFraming(id,{{"start",8},{"end",2}}) && before==p.manualFramingSegments();
  passed=passed && p.split(1) && p.setClipRate(1,2) && std::abs(p.sourceTime(2)-3)<1e-9 && p.framingAt(2).value("scale").toDouble()>1.5;
  for(const auto &curve:{"linear","quadratic","cubic","quintic","spring","custom"}) {
    passed=passed && p.updateManualFraming(id,{{"curve",curve}});
    const double scale=p.framingAt(1.65).value("scale").toDouble();
    passed=passed && std::isfinite(scale) && scale>=1 && scale<=1.6;
  }
  passed=passed && p.removeManualFraming(id) && p.manualFramingSegments().isEmpty();
  p.undo(); passed=passed && p.manualFramingSegments().size()==1;
  auto scene=p.sceneSettings(); auto canvas=scene.value("canvas").toMap(); canvas["padding"]=20; scene["canvas"]=canvas;
  const auto original=p.sceneSettings(); passed=passed && p.applySceneSettings(scene); p.undo(); passed=passed && p.sceneSettings()==original;
  passed=passed && p.setCanvasCrop(.1,.1,.8,.8);
  p.undo(); passed=passed && p.sceneSettings()==original;
  const auto valid=p.sceneSettings(); auto invalid=valid; auto camera=invalid.value("camera").toMap(); camera["enabled"]="bad"; invalid["camera"]=camera;
  passed=passed && !p.applySceneSettings(invalid) && p.sceneSettings()==valid;
  passed=passed && p.moveClip(1,0) && std::abs(p.sourceTime(1)-3)<1e-9 && p.framingAt(1).value("scale").toDouble()>1.5;
  p.undo();
  auto earlier=p.manualFramingSegments().first().toMap(); earlier["manual"]=true;
  auto later=earlier; later["id"]="ffffffff-ffff-ffff-ffff-ffffffffffff"; later["start"]=2.5; later["scale"]=2.0; later["enter"]=0;
  passed=passed && AutoFraming::evaluate({earlier,later},3).scale==2 && AutoFraming::evaluate({later,earlier},3).scale==2;
  const QString path=QDir(outputDirectory).filePath("manual.dogscproject");
  passed=passed && p.saveAs(QUrl::fromLocalFile(path));
  ProjectController reopened; passed=passed && reopened.openProject(QUrl::fromLocalFile(path)) && reopened.manualFramingSegments()==p.manualFramingSegments() && !reopened.dirty();
  reopened.removeManualFraming(id); reopened.undo(); passed=passed && !reopened.dirty();
  QFile report(QDir(outputDirectory).filePath("manual-framing-smoke.json"));
  if(!report.open(QIODevice::WriteOnly)) return 2;
  report.write(QJsonDocument(QJsonObject{{"passed",passed}}).toJson());
  return passed?0:1;
}
