// Automatic zoom planning adapted from DogSC AutoZoomPlanner.swift at
// fa24059a9ba7e769152b3c1de047d96aca873e1c. Copyright (c) 2026 神烦老狗.
// MIT license: third_party/DogSC-LICENSE. Smoothstep and fixed focus are
// temporary development differences from the reference spring/follower.
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
double smooth(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return value * value * (3 - 2 * value);
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

QVariantList AutoFraming::generate(const QVariantList &samples,
                                   double duration) {
  if (!std::isfinite(duration) || duration <= 0)
    return {};
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
              .5 / 1.6 &&
          std::max(regions.back().maxY, click.y) -
                  std::min(regions.back().minY, click.y) <=
              .7 / 1.6) {
        auto &region = regions.back();
        region.minX = std::min(region.minX, click.x);
        region.maxX = std::max(region.maxX, click.x);
        region.minY = std::min(region.minY, click.y);
        region.maxY = std::max(region.maxY, click.y);
      } else
        regions.push_back({click.time, click.x, click.x, click.y, click.y});
    }
    std::vector<double> starts{
        std::max({clicks[first].time - .7 * .18, earliest, 0.0})};
    for (size_t i = 1; i < regions.size(); ++i)
      starts.push_back(
          std::max(regions[i].firstTime - .7, starts.back() + .16));
    double holdEnd = std::max(clicks[last].time + .9, starts.back() + .7);
    double activityEnd = clicks[last].time;
    const double nextStart =
        last + 1 < clicks.size()
            ? std::max(clicks[last + 1].time - .7 * .18, holdEnd + .7)
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
    const double exit = std::min({.7, std::max(0.0, nextStart - holdEnd),
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
          {QStringLiteral("scale"), 1.6},
          {QStringLiteral("centerX"),
           std::clamp((region.minX + region.maxX) / 2, .12, .88)},
          {QStringLiteral("centerY"),
           std::clamp((region.minY + region.maxY) / 2, .12, .88)},
          {QStringLiteral("enter"), .7},
          {QStringLiteral("exit"), i + 1 == regions.size() ? exit : 0.0}});
    }
    earliest = holdEnd + exit;
    first = last + 1;
  }
  return result;
}

AutoFraming::State AutoFraming::evaluate(const QVariantList &segments,
                                         double time) {
  State previous;
  double previousEnd = -1, previousExit = 0;
  if (!std::isfinite(time) || time < 0)
    return previous;
  for (const QVariant &value : segments) {
    const auto map = value.toMap();
    const double start = number(map, "start", -1), end = number(map, "end", -1);
    if (start < 0 || end <= start)
      continue;
    State target =
        constrained({number(map, "scale", 1), number(map, "centerX", .5),
                     number(map, "centerY", .5)});
    const double enter = std::clamp(number(map, "enter", .7), 0.0, 5.0);
    const double exit = std::clamp(number(map, "exit", .7), 0.0, 5.0);
    const State from =
        previousExit <= .000001 && std::abs(previousEnd - start) <= .000001
            ? previous
            : State{};
    const auto interpolate = [&](double alpha) {
      return constrained(
          {from.scale + (target.scale - from.scale) * alpha,
           from.centerX + (target.centerX - from.centerX) * alpha,
           from.centerY + (target.centerY - from.centerY) * alpha});
    };
    const State reached =
        interpolate(enter > 0 ? smooth((end - start) / enter) : 1);
    if (time < start)
      return {};
    if (time < end) {
      const double alpha = enter > 0 ? smooth((time - start) / enter) : 1;
      return interpolate(alpha);
    }
    if (exit > 0 && time < end + exit) {
      const double alpha = smooth((time - end) / exit);
      return constrained({reached.scale + (1 - reached.scale) * alpha,
                          reached.centerX + (.5 - reached.centerX) * alpha,
                          reached.centerY + (.5 - reached.centerY) * alpha});
    }
    previous = reached;
    previousEnd = end;
    previousExit = exit;
  }
  return {};
}

QImage AutoFraming::render(const QImage &image, State state) {
  state = constrained(state);
  if (image.isNull() || state.scale <= 1.000001)
    return image;
  const double width = image.width() / state.scale,
               height = image.height() / state.scale;
  const QRectF crop(state.centerX * image.width() - width / 2,
                    state.centerY * image.height() - height / 2, width, height);
  QImage output(image.size(), QImage::Format_RGBA8888);
  QPainter painter(&output);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.drawImage(QRectF(output.rect()), image, crop);
  return output;
}
