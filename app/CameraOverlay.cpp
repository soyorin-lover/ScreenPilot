#include "CameraOverlay.h"
#include "CameraMotion.h"
#include "SceneEffects.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
namespace CameraOverlay {
QVariantMap defaults() {
  return {
      {"enabled", true},     {"x", .68},           {"y", .68},
      {"width", .29},        {"shape", "rounded"}, {"radius", .15},
      {"mirror", true},      {"shadow", true},     {"opacity", 1.0},
      {"fullscreen", false}, {"cropScale", 1.0},   {"cropX", .5},
      {"cropY", .5},         {"borderWidth", 0.0}, {"borderColor", "#ffffff"},
      {"motionSegments", QVariantList{}}, {"syncPoints", QVariantList{}}};
}
bool validate(const QVariantMap &s, QString *error) {
  for (const auto &key :
       {"x", "y", "width", "radius", "opacity", "cropX", "cropY"}) {
    if (!s.contains(key))
      continue;
    bool ok = false;
    const double v = s.value(key).toDouble(&ok);
    if (!ok || !std::isfinite(v) || v < 0 || v > 1 ||
        (QString(key) == "width" && v == 0)) {
      if (error)
        *error = QString("Invalid camera %1").arg(key);
      return false;
    }
  }
  const auto shape = s.value("shape", "circle").toString();
  if (shape != "circle" && shape != "rounded" && shape != "rectangle" &&
      shape != "landscape" && shape != "portrait" && shape != "source") {
    if (error)
      *error = "Invalid camera shape";
    return false;
  }
  bool scaleOk=false, borderOk=false;
  const double scale = s.value("cropScale", 1.0).toDouble(&scaleOk);
  const double border = s.value("borderWidth", 0.0).toDouble(&borderOk);
  if (!scaleOk || !borderOk || !std::isfinite(scale) || scale < 1 || scale > 4 ||
      !std::isfinite(border) || border < 0 || border > 40) {
    if (error)
      *error = "Invalid camera crop or border";
    return false;
  }
  for (const auto &key : {"enabled", "mirror", "shadow", "fullscreen"}) {
    if (s.contains(key) && s.value(key).metaType().id() != QMetaType::Bool) {
      if (error) *error = QString("Invalid camera %1").arg(key);
      return false;
    }
  }
  if (s.contains("fullscreenProgress")) {
    bool ok=false; const double progress=s.value("fullscreenProgress").toDouble(&ok);
    if (!ok || !std::isfinite(progress) || progress<0 || progress>1) {
      if (error) *error="Invalid camera fullscreen transition";
      return false;
    }
  }
  if (!QColor(s.value("borderColor", "#ffffff").toString()).isValid()) {
    if (error) *error="Invalid camera border color";
    return false;
  }
  return CameraMotion::validate(s, error);
}
void paint(QPainter &p, QSize size, const QImage &image,
           const QVariantMap &supplied, const QVariantMap &sceneSettings, double outputSeconds) {
  if (image.isNull() || size.isEmpty() || !validate(supplied))
    return;
  auto s = defaults();
  for (auto it = supplied.cbegin(); it != supplied.cend(); ++it)
    s.insert(it.key(), it.value());
  if (!s.value("enabled").toBool())
    return;
  const auto shape = s.value("shape").toString();
  const double aspect = shape == "landscape"  ? 16. / 9
                        : shape == "portrait" ? 9. / 16
                        : shape == "source"
                            ? double(image.width()) / image.height()
                            : 1.;
  const double side = std::min(size.width() * s.value("width").toDouble(),
                               size.height() * aspect);
  const double height = side / aspect;
  QRectF rect(std::clamp(size.width() * s.value("x").toDouble(), 0.0,
                         std::max(0.0, size.width() - side)),
              std::clamp(size.height() * s.value("y").toDouble(), 0.0,
                         std::max(0.0, size.height() - height)),
              side, height);
  const double fullscreen = s.value("fullscreen").toBool() ? 1.0 : s.value("fullscreenProgress", 0.0).toDouble();
  if (fullscreen > 0) {
    rect = QRectF(rect.x() * (1-fullscreen), rect.y() * (1-fullscreen),
                  rect.width() + (size.width()-rect.width())*fullscreen,
                  rect.height() + (size.height()-rect.height())*fullscreen);
  }
  const auto pathFor = [&](QRectF r) {
    QPainterPath path;
    if (fullscreen >= 1)
      path.addRect(r);
    else if (s.value("shape").toString() == "circle")
      path.addRoundedRect(r, r.width() * .5 * (1-fullscreen),
                          r.height() * .5 * (1-fullscreen));
    else {
      const double radius = s.value("shape").toString() == "rectangle"
                                ? 0
                                : side * s.value("radius").toDouble() * (1-fullscreen);
      path.addRoundedRect(r, radius, radius);
    }
    return path;
  };
  p.save();
  SceneEffects::apply(p, rect, sceneSettings, outputSeconds, true);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.setOpacity(p.opacity() * s.value("opacity").toDouble());
  if (s.value("shadow").toBool()) {
    p.setPen(Qt::NoPen);
    const double blur = std::max(2.0, side * .04);
    for (int i = 8; i >= 1; --i) {
      p.setBrush(QColor(0, 0, 0, 8));
      const double spread = blur * i / 8.;
      p.drawPath(pathFor(rect.translated(0, blur * .5)
                             .adjusted(-spread, -spread, spread, spread)));
    }
  }
  p.save();
  p.setClipPath(pathFor(rect), Qt::IntersectClip);
  const double outputAspect = rect.width() / rect.height();
  double cropWidth = image.width(), cropHeight = cropWidth / outputAspect;
  if (cropHeight > image.height()) {
    cropHeight = image.height();
    cropWidth = cropHeight * outputAspect;
  }
  cropWidth /= s.value("cropScale").toDouble();
  cropHeight /= s.value("cropScale").toDouble();
  const QRectF source((image.width() - cropWidth) * s.value("cropX").toDouble(),
                      (image.height() - cropHeight) *
                          s.value("cropY").toDouble(),
                      cropWidth, cropHeight);
  if (s.value("mirror").toBool()) {
    p.translate(rect.center().x() * 2, 0);
    p.scale(-1, 1);
  }
  p.drawImage(rect, image, source);
  p.restore();
  const double border = s.value("borderWidth").toDouble();
  if (border > 0) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(s.value("borderColor").toString()), border));
    p.setBrush(Qt::NoBrush);
    p.drawPath(pathFor(
        rect.adjusted(border / 2, border / 2, -border / 2, -border / 2)));
    p.restore();
  }
  p.restore();
}
} // namespace CameraOverlay
