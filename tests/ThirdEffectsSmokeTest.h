#pragma once
#include "EffectController.h"
#include "ProjectController.h"
#include <QImage>
#include <QTemporaryDir>
#include <cmath>

// Independent model fixture: no GUI or capture device required.
inline bool thirdEffectsSmoke(const QUrl &source) {
  ProjectController project;
  if (!project.newRecording(source, 3) || !project.setCameraMedia(source, .2)) return false;
  EffectController effects(&project);
  for (const auto &track : {"screenMotionSegments", "blurRegions", "cameraMotionSegments", "syncPoints"}) {
    if (!effects.selectedItem(track).isEmpty()) return false;
    const auto id = effects.add(track, .5, QString(track)=="syncPoints" ? 0 : .5);
    if (id.isEmpty() || effects.selectedItem(track).value("id").toString()!=id) return false;
    if (QString(track)=="screenMotionSegments") {
      const auto before=effects.selectedItem(track).value("from").toMap();
      if (!effects.updateSelected(track, {{"from", QVariantMap{{"screenRotationY", 12.0}}}})) return false;
      auto expected=before; expected["screenRotationY"]=12.0;
      if (effects.selectedItem(track).value("from").toMap()!=expected) return false;
    }
    if (!effects.remove(track,id) || !effects.selectedItem(track).isEmpty()) return false;
    project.undo();
    if (effects.items(track).size()!=1 || !effects.remove(track,id)) return false;
  }
  const auto first=effects.addSyncAtOutput(.5);
  const double mapping=project.cameraTimeAt(1.5);
  const auto second=effects.addSyncAtOutput(1.5);
  if (first.isEmpty() || second.isEmpty() || std::abs(project.cameraTimeAt(1.5)-mapping)>1e-8) return false;
  const auto cameraBefore=project.cameraSettings();
  if (!effects.addSyncAtOutput(.5).isEmpty() || project.cameraSettings()!=cameraBefore) return false;
  if (effects.update("syncPoints",second,{{"cameraTime", .1}}) || project.cameraSettings()!=cameraBefore) return false;
  if (!effects.remove("syncPoints",first) || !effects.remove("syncPoints",second)) return false;
  QTemporaryDir temp;
  if (!temp.isValid()) return false;
  const QString imagePath=temp.filePath(QString::fromUtf8("贴图 space # %.png"));
  QImage image(16,16,QImage::Format_ARGB32); image.fill(Qt::yellow);
  if (!image.save(imagePath)) return false;
  if (effects.localFileUrl(imagePath).toLocalFile()!=imagePath) return false;
  const auto sticker=effects.addSticker(effects.localFileUrl(imagePath),0,.5);
  if (sticker.isEmpty() || !effects.remove("stickers",sticker)) return false;
  project.undo();
  return effects.stickers().size()==1 && effects.remove("stickers",sticker);
}
