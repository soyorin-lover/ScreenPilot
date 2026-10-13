#pragma once
#include "EffectController.h"
#include "ProjectController.h"

// Run after newRecording() with a source at least three seconds long.
inline bool effectControllerSmoke(ProjectController &project) {
  EffectController effects(&project);
  const QString id = effects.add("blurRegions", 0, 1);
  if (id.isEmpty() || effects.selectedId("blurRegions") != id) return false;
  if (!effects.updateSelected("blurRegions", {{"strength", .4}})) return false;
  if (effects.updateSelected("blurRegions", {{"imagePath", "invalid"}})) return false;
  const auto before = project.canvasSettings();
  if (effects.updateSelected("blurRegions", {{"sourceDuration", project.sourceDuration()+1}}) || project.canvasSettings()!=before) return false;
  if (!effects.removeSelected("blurRegions") || !effects.selectedId("blurRegions").isEmpty()) return false;
  project.undo();
  bool restored=false;
  for (const auto &entry : effects.blurRegions()) if (entry.toMap().value("id").toString()==id) restored=true;
  if (!restored || !effects.remove("blurRegions",id)) return false;
  // Adapt old persisted entries without adding history or rewriting settings on reads.
  const QVariantList legacy{QVariantMap{{"sourceStart",0.0},{"sourceDuration",1.0}}};
  project.setCanvasOption("blurRegions", legacy);
  const auto persisted=project.canvasSettings();
  const auto first=effects.blurRegions(), second=effects.blurRegions();
  if (first!=second || first.first().toMap().value("id").toString().isEmpty() || project.canvasSettings()!=persisted) return false;
  const QString legacyId=first.first().toMap().value("id").toString();
  if (!effects.select("blurRegions",legacyId) || !effects.updateSelected("blurRegions",{{"strength",.2}}) || effects.selectedId("blurRegions")!=legacyId) return false;
  return effects.removeSelected("blurRegions");
}
