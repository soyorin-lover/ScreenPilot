// Automatic zoom planning adapted from DogSC AutoZoomPlanner.swift at
// fa24059a9ba7e769152b3c1de047d96aca873e1c. Copyright (c) 2026 神烦老狗.
// MIT license: third_party/DogSC-LICENSE. Spring response follows
// ZoomAnimation.swift; the bounded follower is a Windows implementation.
#include "AutoFraming.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
double number(const QVariantMap &map, const char *key, double fallback) {
  bool ok = false;
  const double value = map.value(QLatin1String(key)).toDouble(&ok);
  return ok && std::isfinite(value) ? value : fallback;
}
double spring(double value) {
  const double t = std::clamp(value, 0.0, 1.0);
  if (t <= 0 || t >= 1)
    return t;
  const double omega = std::sqrt(210.0 / 2.4);
  const double zeta = std::min(42.0 / (2 * std::sqrt(210.0 * 2.4)), .9999);
  const double damped = omega * std::sqrt(1 - zeta * zeta);
  auto response = [&](double seconds) {
    return 1 - std::exp(-zeta * omega * seconds) *
                   (std::cos(damped * seconds) +
                    zeta * omega / damped * std::sin(damped * seconds));
  };
  return std::clamp(response(t * .72) / response(.72), 0.0, 1.0);
}
QPointF focusAt(const QVariantMap &segment, double time) {
  const auto path = segment.value("followPath").toList();
  if (path.isEmpty())
    return {number(segment, "centerX", .5), number(segment, "centerY", .5)};
  int lo = 0, hi = path.size() - 1;
  while (lo < hi) {
    const int mid = (lo + hi + 1) / 2;
    if (number(path[mid].toMap(), "time", 0) <= time)
      lo = mid;
    else
      hi = mid - 1;
  }
  const auto a = path[lo].toMap(),
             b = path[std::min(lo + 1, int(path.size()) - 1)].toMap();
  const double span = number(b, "time", 0) - number(a, "time", 0);
  const double alpha =
      span > 0 ? std::clamp((time - number(a, "time", 0)) / span, 0.0, 1.0) : 0;
  return {
      number(a, "x", .5) + (number(b, "x", .5) - number(a, "x", .5)) * alpha,
      number(a, "y", .5) + (number(b, "y", .5) - number(a, "y", .5)) * alpha};
}
// Preserve authored source-normalized focus until the renderer knows its crop.
AutoFraming::State sanitized(AutoFraming::State state) {
  state.scale = std::isfinite(state.scale) ? std::clamp(state.scale, 1.0, 6.0) : 1;
  state.centerX = std::isfinite(state.centerX) ? std::clamp(state.centerX, 0.0, 1.0) : .5;
  state.centerY = std::isfinite(state.centerY) ? std::clamp(state.centerY, 0.0, 1.0) : .5;
  return state;
}
AutoFraming::State constrained(AutoFraming::State state) {
  state.scale =
      std::isfinite(state.scale) ? std::clamp(state.scale, 1.0, 6.0) : 1;
  const double half = .5 / state.scale;
  state.centerX = std::isfinite(state.centerX)
                      ? std::clamp(state.centerX, half, 1 - half)
                      : .5;
  state.centerY = std::isfinite(state.centerY)
                      ? std::clamp(state.centerY, half, 1 - half)
                      : .5;
  return state;
}
} // namespace

QVariantList AutoFraming::generate(const QVariantList &samples, double duration,
                                   QVariantMap options) {
  if (!std::isfinite(duration) || duration <= 0)
    return {};
  const double zoom = std::clamp(number(options, "scale", 1.6), 1.0, 6.0);
  const double transition =
      std::clamp(number(options, "transition", .7), 0.0, 5.0);
  struct Sample {
    double time, x, y;
    bool click, inside;
  };
  std::vector<Sample> ordered, events, clicks;
  for (const QVariant &value : samples) {
    const auto map = value.toMap();
    const double time = number(map, "time", -1);
    const double x = number(map, "x", -1), y = number(map, "y", -1);
    if (time < 0 || time >= duration)
      continue;
    const bool inside = x >= 0 && x <= 1 && y >= 0 && y <= 1 &&
                        map.value(QStringLiteral("inside"), true).toBool();
    ordered.push_back(
        {time, x, y, map.value(QStringLiteral("click")).toBool(), inside});
  }
  std::stable_sort(
      ordered.begin(), ordered.end(),
      [](const Sample &a, const Sample &b) { return a.time < b.time; });
  Sample previous{};
  // Recording samples are periodic; only real motion, entry, or clicks extend
  // activity. A stationary pointer must still allow the camera to return.
  for (const auto &sample : ordered) {
    if (sample.inside &&
        (sample.click || !previous.inside ||
         std::hypot(sample.x - previous.x, sample.y - previous.y) > .001))
      events.push_back(sample);
    previous = sample;
  }
  for (const auto &event : events)
    if (event.click)
      clicks.push_back(event);
  QVariantList result;
  double earliest = 0;
  // Reference AutoZoomPlanner defaults: 1.6x, .7s enter/exit, .9s hold,
  // 1.4s click/activity gap, .55s settle, .12 safe inset.
  for (size_t first = 0; first < clicks.size();) {
    size_t last = first;
    while (last + 1 < clicks.size() &&
           clicks[last + 1].time - clicks[last].time <= 1.4)
      ++last;
    struct Region {
      double firstTime, minX, maxX, minY, maxY;
    };
    std::vector<Region> regions;
    for (size_t i = first; i <= last; ++i) {
      const auto &click = clicks[i];
      if (!regions.empty() &&
          std::max(regions.back().maxX, click.x) -
                  std::min(regions.back().minX, click.x) <=
              .5 / zoom &&
          std::max(regions.back().maxY, click.y) -
                  std::min(regions.back().minY, click.y) <=
              .7 / zoom) {
        auto &region = regions.back();
        region.minX = std::min(region.minX, click.x);
        region.maxX = std::max(region.maxX, click.x);
        region.minY = std::min(region.minY, click.y);
        region.maxY = std::max(region.maxY, click.y);
      } else
        regions.push_back({click.time, click.x, click.x, click.y, click.y});
    }
    std::vector<double> starts{
        std::max({clicks[first].time - transition * .18, earliest, 0.0})};
    for (size_t i = 1; i < regions.size(); ++i)
      starts.push_back(
          std::max(regions[i].firstTime - transition, starts.back() + .16));
    double holdEnd =
        std::max(clicks[last].time + .9, starts.back() + transition);
    double activityEnd = clicks[last].time;
    const double nextStart =
        last + 1 < clicks.size()
            ? std::max(clicks[last + 1].time - transition * .18,
                       holdEnd + transition)
            : duration;
    for (const auto &event : events) {
      if (event.time <= clicks[last].time)
        continue;
      if (event.time >= nextStart || event.time - activityEnd > 1.4)
        break;
      activityEnd = event.time;
    }
    if (activityEnd > clicks[last].time)
      holdEnd = std::max(holdEnd, activityEnd + .55);
    holdEnd = std::min(holdEnd, nextStart);
    const double exit =
        std::min({transition, std::max(0.0, nextStart - holdEnd),
                  std::max(0.0, duration - holdEnd)});
    for (size_t i = 0; i < regions.size(); ++i) {
      const double end =
          std::min(duration, i + 1 < regions.size() ? starts[i + 1] : holdEnd);
      if (end - starts[i] <= .01)
        continue;
      const auto &region = regions[i];
      result.append(QVariantMap{
          {QStringLiteral("start"), starts[i]},
          {QStringLiteral("end"), end},
          {QStringLiteral("scale"), zoom},
          {QStringLiteral("spring"), options.value("spring", true)},
          {QStringLiteral("centerX"),
           std::clamp((region.minX + region.maxX) / 2, .12, .88)},
          {QStringLiteral("centerY"),
           std::clamp((region.minY + region.maxY) / 2, .12, .88)},
          {QStringLiteral("enter"), transition},
          {QStringLiteral("exit"), i + 1 == regions.size() ? exit : 0.0}});
    }
    earliest = holdEnd + exit;
    first = last + 1;
  }
  // Precompute a deterministic 60Hz focus track once, not on each preview
  // frame. Only leaving the inset safe area moves the target; small pointer
  // jitter is ignored.
  if (options.value("follow", true).toBool()) {
    size_t pointer = 0;
    Sample latest{};
    QPointF previousFocus(.5, .5);
    double previousEnd = -1;
    for (int i = 0; i < result.size(); ++i) {
      auto segment = result[i].toMap();
      const double start = number(segment, "start", 0),
                   end = number(segment, "end", start);
      const auto initial = constrained({zoom, number(segment, "centerX", .5),
                                        number(segment, "centerY", .5)});
      double x = initial.centerX, y = initial.centerY, vx = 0, vy = 0;
      if (std::abs(previousEnd - start) < 1e-6) {
        x = previousFocus.x();
        y = previousFocus.y();
      }
      double targetX = x, targetY = y;
      const double safeHalf = .38 / zoom;
      QVariantList path;
      const int steps = std::max(1, int(std::ceil((end - start) * 120)));
      for (int step = 0; step <= steps; ++step) {
        const double t = std::min(end, start + step / 120.0);
        while (pointer < ordered.size() && ordered[pointer].time <= t)
          latest = ordered[pointer++];
        if (latest.inside && latest.time >= start) {
          if (latest.x < targetX - safeHalf)
            targetX = latest.x + safeHalf;
          if (latest.x > targetX + safeHalf)
            targetX = latest.x - safeHalf;
          if (latest.y < targetY - safeHalf)
            targetY = latest.y + safeHalf;
          if (latest.y > targetY + safeHalf)
            targetY = latest.y - safeHalf;
        }
        const auto target = constrained({zoom, targetX, targetY});
        if (step) {
          constexpr double dt = 1.0 / 120;
          vx += ((target.centerX - x) * 210 / 2.4 - vx * 42 / 2.4) * dt;
          vy += ((target.centerY - y) * 210 / 2.4 - vy * 42 / 2.4) * dt;
          x += vx * dt;
          y += vy * dt;
        }
        if (step % 2 == 0 || step == steps)
          path.append(QVariantMap{{"time", t}, {"x", x}, {"y", y}});
      }
      segment["followPath"] = path;
      previousFocus = {x, y};
      previousEnd = number(segment, "exit", 0) <= 0 ? end : -1;
      result[i] = segment;
    }
  }
  return result;
}

AutoFraming::State AutoFraming::evaluate(const QVariantList &segments,
                                         double time) {
  // Manual clips override automatic framing over their full enter/hold/exit window.
  QVariantList automatic;
  QVariantMap activeManual;
  for (const auto &value : segments) {
    const auto m = value.toMap();
    if (m.value("manual").toBool()) {
      const double start = number(m, "start", -1), end = number(m, "end", -1);
      if (time >= start && time < end + number(m, "exit", .7)) {
        // Match the reference sorted track's last-active-wins fallback.
        // Equal source starts are ordered by stable UUID, independent of insertion.
        if (activeManual.isEmpty() || start > number(activeManual,"start",-1) ||
            (start == number(activeManual,"start",-1) && m.value("id").toString() > activeManual.value("id").toString()))
          activeManual = m;
      }
    } else automatic.append(value);
  }
  if (!activeManual.isEmpty()) {
    // Preserve touching manual camera handoffs without changing overlap priority.
    // Build only the predecessor chain of the winning active manual segment.
    QVariantList chain;
    auto current=activeManual;
    for (int count=0; count<segments.size(); ++count) {
      auto entry=current; entry.remove("manual"); chain.prepend(entry);
      QVariantMap predecessor;
      const double start=number(current,"start",-1);
      for (const auto &value:segments) {
        const auto candidate=value.toMap();
        if (!candidate.value("manual").toBool() ||
            number(candidate,"start",-1)>=start ||
            std::abs(number(candidate,"end",-1)-start)>.000001 ||
            number(candidate,"exit",.7)>.000001) continue;
        if (predecessor.isEmpty() || number(candidate,"start",-1)>number(predecessor,"start",-1) ||
            (number(candidate,"start",-1)==number(predecessor,"start",-1) && candidate.value("id").toString()>predecessor.value("id").toString()))
          predecessor=candidate;
      }
      if(predecessor.isEmpty()) break;
      current=predecessor;
    }
    return evaluate(chain,time);
  }
  State previous;
  double previousEnd = -1, previousExit = 0;
  if (!std::isfinite(time) || time < 0)
    return previous;
  for (const QVariant &value : automatic) {
    const auto map = value.toMap();
    const double start = number(map, "start", -1), end = number(map, "end", -1);
    if (start < 0 || end <= start)
      continue;
    const auto focus = focusAt(map, std::min(time, end));
    State target = sanitized({number(map, "scale", 1), focus.x(), focus.y()});
    const double enter = std::clamp(number(map, "enter", .7), 0.0, 5.0);
    const double exit = std::clamp(number(map, "exit", .7), 0.0, 5.0);
    const State from =
        previousExit <= .000001 && std::abs(previousEnd - start) <= .000001
            ? previous
            : State{};
    const auto easing = [&](double value) {
      const double t = std::clamp(value, 0.0, 1.0);
      const QString curve = map.value("curve").toString();
      if (curve.isEmpty()) return map.value("spring", true).toBool() ? spring(t) : t*t*(3-2*t);
      if (curve == "spring") return spring(t);
      if (curve == "linear") return t;
      double x1 = .65, y1 = 0, x2 = .35, y2 = 1;
      if (curve == "quadratic") { x1 = .45; x2 = .55; }
      if (curve == "quintic") { x1 = .82; x2 = .18; }
      if (curve == "custom") {
        x1 = number(map,"x1",.65); y1 = number(map,"y1",0);
        x2 = number(map,"x2",.35); y2 = number(map,"y2",1);
      }
      auto bezier = [](double p, double a, double b) { return 3*(1-p)*(1-p)*p*a + 3*(1-p)*p*p*b + p*p*p; };
      double lo = 0, hi = 1;
      for (int i=0;i<30;++i) { const double p=(lo+hi)/2; if(bezier(p,x1,x2)<t) lo=p; else hi=p; }
      return bezier((lo+hi)/2,y1,y2);
    };
    const auto interpolate = [&](double alpha) {
      return sanitized(
          {from.scale + (target.scale - from.scale) * alpha,
           from.centerX + (target.centerX - from.centerX) * alpha,
           from.centerY + (target.centerY - from.centerY) * alpha});
    };
    const State reached =
        interpolate(enter > 0 ? easing((end - start) / enter) : 1);
    if (time < start)
      return {};
    if (time < end) {
      const double alpha = enter > 0 ? easing((time - start) / enter) : 1;
      return interpolate(alpha);
    }
    if (exit > 0 && time < end + exit) {
      const double alpha = easing((time - end) / exit);
      return sanitized({reached.scale + (1 - reached.scale) * alpha,
                          reached.centerX + (.5 - reached.centerX) * alpha,
                          reached.centerY + (.5 - reached.centerY) * alpha});
    }
    previous = reached;
    previousEnd = end;
    previousExit = exit;
  }
  return {};
}

QRectF AutoFraming::sourceRect(QSize size, State state) {
  state = constrained(state);
  const double width = size.width() / state.scale,
               height = size.height() / state.scale;
  return {state.centerX * size.width() - width / 2,
          state.centerY * size.height() - height / 2, width, height};
}

QImage AutoFraming::render(const QImage &image, State state) {
  state = constrained(state);
  if (image.isNull() || state.scale <= 1.000001)
    return image;
  const QRectF crop = sourceRect(image.size(), state);
  QImage output(image.size(), QImage::Format_RGBA8888);
  QPainter painter(&output);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.drawImage(QRectF(output.rect()), image, crop);
  return output;
}

bool AutoFraming::validateManual(const QVariantMap &m, double duration) {
  const QMap<QString, QPair<double,double>> ranges{
    {"start",{0,duration}}, {"end",{0,duration}}, {"scale",{1,6}},
    {"centerX",{0,1}}, {"centerY",{0,1}}, {"enter",{0,5}}, {"exit",{0,5}},
    {"x1",{0,1}}, {"y1",{0,1}}, {"x2",{0,1}}, {"y2",{0,1}}};
  for (auto it=ranges.cbegin();it!=ranges.cend();++it) {
    bool ok=false; const double v=m.value(it.key()).toDouble(&ok);
    if (!ok || !std::isfinite(v) || v<it.value().first || v>it.value().second) return false;
  }
  return m.value("end").toDouble() > m.value("start").toDouble() &&
      QStringList{"linear","quadratic","cubic","quintic","spring","custom"}.contains(m.value("curve").toString());
}
