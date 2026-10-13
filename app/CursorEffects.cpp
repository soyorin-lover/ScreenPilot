// Reference: DogSC fa24059a9ba7e769152b3c1de047d96aca873e1c,
// CursorRenderGeometry.swift. Top-left normalized pointer coordinates and sizing
// relative to a 1080-pixel short edge are shared by preview and export.
#include "CursorEffects.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
QVariantMap CursorEffects::evaluate(const QVariantList &samples, double seconds) {
  if (samples.isEmpty() || !std::isfinite(seconds) || seconds < 0)
    return {};
  // Sorted source-time samples: logarithmic lookup, then only the click window.
  auto end = std::upper_bound(samples.cbegin(), samples.cend(), seconds,
      [](double t, const QVariant &v) { return t < v.toMap().value("time").toDouble(); });
  if (end == samples.cbegin()) return {};
  const auto last = (end - 1)->toMap();
  QVariantMap state{{"inside", last.value("inside").toBool() &&
                                seconds - last.value("time").toDouble() <= .3},
                    {"x", last.value("x")}, {"y", last.value("y")}};
  while (end != samples.cbegin()) {
    const auto sample = (--end)->toMap();
    const double age = seconds - sample.value("time").toDouble();
    if (age > .6) break;
    if (sample.value("click").toBool() && sample.value("inside").toBool()) {
      state["clickAge"] = age;
      state["clickX"] = sample.value("x");
      state["clickY"] = sample.value("y");
      break;
    }
  }
  return state;
}
void CursorEffects::paint(QPainter &p, QRectF video, QRectF crop, QSize source,
                          const QVariantMap &s, const QVariantMap &state) {
  if (!s.value("cursorEnabled").toBool() || state.isEmpty() || crop.isEmpty()) return;
  auto mapped = [&](double x, double y) {
    return QPointF(video.left() + (x * source.width() - crop.left()) / crop.width() * video.width(),
                   video.top() + (y * source.height() - crop.top()) / crop.height() * video.height());
  };
  const double size = s.value("cursorSize", 24).toDouble() *
                      std::min(video.width(), video.height()) / 1080.0;
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setClipRect(video, Qt::IntersectClip);
  if (s.value("clickRing", true).toBool() && state.contains("clickAge")) {
    const double progress = state.value("clickAge").toDouble() / .6;
    const QPointF click = mapped(state.value("clickX").toDouble(), state.value("clickY").toDouble());
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 185, 55, int(255 * (1 - progress))), std::max(1.0, size * .13)));
    const double radius = size * (.6 + progress * 1.6);
    p.drawEllipse(click, radius, radius);
  }
  if (s.value("cursorVisible", true).toBool() && state.value("inside").toBool()) {
    const QPointF point = mapped(state.value("x").toDouble(), state.value("y").toDouble());
    p.setPen(QPen(QColor("#fff5d6"), std::max(1.0, size * .08)));
    const auto style = s.value("cursorStyle", "halo").toString();
    if (style == "arrow") {
      QPainterPath arrow;
      arrow.moveTo(point); arrow.lineTo(point + QPointF(size * .15, size));
      arrow.lineTo(point + QPointF(size * .4, size * .68));
      arrow.lineTo(point + QPointF(size * .8, size * .65)); arrow.closeSubpath();
      p.setBrush(QColor("#262b33")); p.drawPath(arrow);
    } else if (style == "crosshair") {
      p.drawLine(point + QPointF(-size, 0), point + QPointF(size, 0));
      p.drawLine(point + QPointF(0, -size), point + QPointF(0, size));
    } else {
      p.setBrush(QColor(255, 210, 60, 65)); p.drawEllipse(point, size, size);
    }
  }
  p.restore();
}
