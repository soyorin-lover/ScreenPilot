#include "SceneEffects.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
void SceneEffects::apply(QPainter &p, const QRectF &bounds, const QVariantMap &s, double seconds, bool camera) {
  if (!s.value("introEnabled", false).toBool()) return;
  const double duration = s.value("introDuration", 2.2).toDouble();
  const double gap = std::min(s.value("introElementGap", .16).toDouble(), std::max(0., duration - .18));
  const QString order = s.value("introOrder", "screenFirst").toString();
  const bool delayed = (camera && order == "screenFirst") || (!camera && order == "cameraFirst");
  const double delay = delayed ? gap : 0.;
  const double linear = std::clamp((std::max(0., seconds) - delay) / std::max(.001, duration - delay), 0., 1.);
  const double tempo = s.value("introTempo", 1.).toDouble();
  const double progress = 1. - std::pow(1. - linear, 3. * tempo);
  p.setOpacity(p.opacity() * progress);
  const QString style = s.value("introStyle", "scale").toString();
  if (style == "scale") {
    const double scale = .84 + .16 * progress;
    p.translate(bounds.center()); p.scale(scale, scale); p.translate(-bounds.center());
  } else if (style == "slide") p.translate(0, (1. - progress) * bounds.height() * .18);
}
