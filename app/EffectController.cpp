#include "EffectController.h"
#include "ProjectController.h"
#include "CameraMotion.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
const QStringList trackNames{"screenMotionSegments", "blurRegions", "cameraMotionSegments", "syncPoints", "stickers"};
bool camera(const QString &track) { return track == "cameraMotionSegments" || track == "syncPoints"; }
QString key(const QString &track) { return track == "cameraMotionSegments" ? QStringLiteral("motionSegments") : track; }
QStringList editable(const QString &track) {
  if (track == "screenMotionSegments") return {"sourceStart", "sourceDuration", "from", "to"};
  if (track == "blurRegions") return {"sourceStart", "sourceDuration", "x", "y", "width", "height", "strength", "style", "enterDuration", "exitDuration"};
  if (track == "cameraMotionSegments") return {"start", "end", "x", "y", "size", "fullscreen", "shape", "enter", "exit"};
  if (track == "syncPoints") return {"sourceTime", "cameraTime"};
  if (track == "stickers") return {"sourceStart", "sourceDuration", "x", "y", "width", "rotation", "opacity", "radius", "borderWidth", "borderColor", "shadow", "enterDuration", "exitDuration"};
  return {};
}
}
EffectController::EffectController(ProjectController *project, QObject *parent)
    : QObject(parent), m_project(project) {
  Q_ASSERT(project);
  connect(project, &ProjectController::changed, this, [this] { reconcile(); emit changed(); });
}
QVariantList EffectController::tracks() const {
  QVariantList result;
  for (const auto &name : trackNames) result.append(name);
  return result;
}
QVariantList EffectController::items(QString track) const {
  if (!trackNames.contains(track)) return {};
  auto list = (camera(track) ? m_project->cameraSettings() : m_project->canvasSettings()).value(key(track)).toList();
  QSet<QString> used;
  for (auto &entry : list) {
    auto item = entry.toMap();
    QString id = item.value("id").toString();
    if (id.isEmpty() || used.contains(id)) {
      const auto hash = QCryptographicHash::hash(QJsonDocument::fromVariant(item).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
      const QString base = "legacy-" + track + "-" + QString::fromLatin1(hash);
      id = base;
      for (int ordinal = 1; used.contains(id); ++ordinal) id = base + "-" + QString::number(ordinal);
      item["id"] = id;
      entry = item;
    }
    used.insert(id);
  }
  return list;
}
QString EffectController::selectedId(QString track) const { return m_selection.value(track); }
QVariantMap EffectController::selectedIds() const {
  QVariantMap result;
  for(const auto &track:trackNames) result[track]=selectedId(track);
  return result;
}
QVariantMap EffectController::selectedItems() const {
  QVariantMap result;
  for(const auto &track:trackNames) result[track]=selectedItem(track);
  return result;
}
QVariantMap EffectController::selectedItem(QString track) const {
  const auto id = selectedId(track);
  if (!id.isEmpty()) for (const auto &entry : items(track)) if (entry.toMap().value("id").toString() == id) return entry.toMap();
  return {};
}
bool EffectController::fail(QString message) { m_error = message; emit changed(); return false; }
bool EffectController::select(QString track, QString id) {
  if (!trackNames.contains(track)) return fail(tr("Unknown effect track"));
  if (!id.isEmpty()) {
    bool found = false;
    for (const auto &entry : items(track)) if (entry.toMap().value("id").toString() == id) found = true;
    if (!found) return fail(tr("Effect not found"));
  }
  m_selection[track] = id; m_error.clear(); emit changed(); return true;
}
void EffectController::reconcile() {
  for (const auto &track : trackNames) if (!selectedId(track).isEmpty() && selectedItem(track).isEmpty()) m_selection.remove(track);
}
bool EffectController::validTimes(QString track, const QVariantMap &item) const {
  bool ok = false, lengthOk = false;
  const double start = item.value(track == "syncPoints" ? "sourceTime" : camera(track) ? "start" : "sourceStart").toDouble(&ok);
  if (!ok || !std::isfinite(start) || start < 0 || start > m_project->sourceDuration()) return false;
  if (track == "syncPoints") {
    const double cameraTime = item.value("cameraTime").toDouble(&ok);
    return ok && std::isfinite(cameraTime) && cameraTime >= 0;
  }
  const double length = camera(track) ? item.value("end").toDouble(&lengthOk) - start : item.value("sourceDuration").toDouble(&lengthOk);
  return lengthOk && std::isfinite(length) && length > 0 && start + length <= m_project->sourceDuration() + 1e-6;
}
bool EffectController::write(QString track, QVariantList list) {
  if (!m_project->hasProject()) return fail(tr("Load a recording first"));
  for (const auto &entry : list) if (!validTimes(track, entry.toMap())) return fail(tr("Effect time is outside the source recording"));
  if (camera(track)) {
    const QString timeKey = track == "syncPoints" ? "sourceTime" : "start";
    std::stable_sort(list.begin(), list.end(), [&](const QVariant &a, const QVariant &b) { return a.toMap().value(timeKey).toDouble() < b.toMap().value(timeKey).toDouble(); });
    if (m_project->cameraSource().isEmpty()) return fail(tr("Load camera media first"));
    m_project->setCameraOption(key(track), list);
  } else m_project->setCanvasOption(key(track), list);
  const auto actual = (camera(track) ? m_project->cameraSettings() : m_project->canvasSettings()).value(key(track)).toList();
  if (actual != list) return fail(m_project->error().isEmpty() ? tr("Effect settings were rejected") : m_project->error());
  m_error.clear(); emit changed(); return true;
}
QString EffectController::add(QString track, double start, double duration) {
  if (!trackNames.contains(track) || track == "stickers") { fail(tr("Choose a supported effect track; stickers require an image")); return {}; }
  QVariantMap item;
  if (track == "syncPoints") {
    const auto settings = m_project->cameraSettings();
    const double mapped = CameraMotion::sourceToCameraTime(settings, start) + settings.value("offset", 0.0).toDouble();
    item = {{"sourceTime", start}, {"cameraTime", mapped}};
  }
  else if (track == "cameraMotionSegments") item = {{"start",start},{"end",start+duration},{"x",.75},{"y",.75},{"size",.29},{"fullscreen",false},{"shape","rounded"},{"enter",0.0},{"exit",0.0}};
  else {
    item = {{"sourceStart",start},{"sourceDuration",duration}};
    if (track == "screenMotionSegments") {
      QVariantMap pose;
      const auto canvas = m_project->canvasSettings();
      for (const auto &name : {"screenX","screenY","screenScale","screenRotationX","screenRotationY","screenRotationZ","screenPerspective"}) pose[name] = canvas.value(name);
      item["from"] = pose; item["to"] = pose;
    } else {
      item.insert({{"x",.25},{"y",.25},{"width",.5},{"height",.5},{"strength",.7},{"style","blur"},{"enterDuration",0.0},{"exitDuration",0.0}});
    }
  }
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces); item["id"] = id;
  auto list = items(track); list.append(item);
  if (!write(track, list)) return {};
  select(track, id); return id;
}
QString EffectController::addAtOutput(QString track, double seconds, double duration) {
  const double source = sourceTime(seconds);
  if (source < 0) { fail(tr("Invalid output time")); return {}; }
  if(track!="syncPoints" && std::isfinite(duration) && duration>0)
    duration=std::min(duration,m_project->sourceDuration()-source);
  return add(track, source, duration);
}
QString EffectController::addSyncAtOutput(double seconds) {
  const double source = sourceTime(seconds);
  if (source < 0) { fail(tr("Invalid output time")); return {}; }
  // add() samples the existing mapping and commits the full point atomically.
  return add("syncPoints", source, 0);
}
QString EffectController::addSticker(QUrl image, double source, double duration) {
  const auto id = m_project->addSticker(image, source, duration);
  if (id.isEmpty()) { fail(m_project->error()); return {}; }
  select("stickers", id); return id;
}
bool EffectController::update(QString track, QString id, QVariantMap patch) {
  if (!trackNames.contains(track)) return fail(tr("Unknown effect track"));
  const auto keys = editable(track);
  for (auto it = patch.cbegin(); it != patch.cend(); ++it) if (!keys.contains(it.key())) return fail(tr("Unknown or immutable effect option: %1").arg(it.key()));
  auto list = items(track);
  for (auto &entry : list) {
    auto item = entry.toMap(); if (item.value("id").toString() != id) continue;
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
      if (track == "screenMotionSegments" && (it.key() == "from" || it.key() == "to")) {
        if (it.value().metaType().id() != QMetaType::QVariantMap) return fail(tr("Screen pose must be an object"));
        auto pose = item.value(it.key()).toMap();
        const QStringList poseKeys{"screenX","screenY","screenScale","screenRotationX","screenRotationY","screenRotationZ","screenPerspective"};
        const auto delta = it.value().toMap();
        for (auto p = delta.cbegin(); p != delta.cend(); ++p) { if (!poseKeys.contains(p.key())) return fail(tr("Unknown screen pose option")); pose[p.key()] = p.value(); }
        item[it.key()] = pose;
      } else item[it.key()] = it.value();
    }
    if (!validTimes(track, item)) return fail(tr("Effect time is outside the source recording"));
    if (track == "stickers") {
      if (!m_project->updateSticker(id, patch)) return fail(m_project->error());
      m_error.clear(); emit changed(); return true;
    }
    entry = item; return write(track, list);
  }
  return fail(tr("Effect not found"));
}
bool EffectController::updateSelected(QString track, QVariantMap patch) { return update(track, selectedId(track), patch); }
bool EffectController::remove(QString track, QString id) {
  if (!trackNames.contains(track)) return fail(tr("Unknown effect track"));
  auto list = items(track);
  for (qsizetype i = 0; i < list.size(); ++i) if (list[i].toMap().value("id").toString() == id) {
    if (track == "stickers") {
      if (!m_project->removeSticker(id)) return fail(tr("Sticker not found"));
      m_error.clear(); emit changed(); return true;
    }
    list.removeAt(i); return write(track, list);
  }
  return fail(tr("Effect not found"));
}
bool EffectController::removeSelected(QString track) { return remove(track, selectedId(track)); }
double EffectController::sourceTime(double seconds) const {
  return std::isfinite(seconds) && seconds >= 0 && seconds <= m_project->duration() && m_project->hasProject() ? m_project->sourceTime(seconds) : -1;
}
QVariantList EffectController::outputTimes(double source) const {
  QVariantList result;
  if (!std::isfinite(source) || source < 0 || source > m_project->sourceDuration()) return result;
  double output = 0;
  for (const auto &entry : m_project->clips()) {
    const auto clip = entry.toMap();
    const double start = clip.value("sourceStart").toDouble(), length = clip.value("sourceDuration").toDouble(), rate = clip.value("playbackRate",1).toDouble();
    if (rate <= 0) continue;
    if (source >= start && (source < start + length || (source == m_project->sourceDuration() && source == start + length))) result.append(output + (source-start)/rate);
    output += length/rate;
  }
  return result;
}

