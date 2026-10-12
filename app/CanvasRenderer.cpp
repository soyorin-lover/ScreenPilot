// Canvas layout behavior references DogSC at fa24059a9ba7e769152b3c1de047d96aca873e1c.
// Copyright (c) 2026 神烦老狗. MIT notice: third_party/DogSC-LICENSE.
// QPainter static backgrounds and layered shadows are development approximations.
#include "CanvasRenderer.h"
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace {
QVariantMap resolved(const QVariantMap &settings) {
  auto result = CanvasRenderer::defaults();
  for (auto it = settings.cbegin(); it != settings.cend(); ++it)
    result.insert(it.key(), it.value());
  return result;
}
// A bounded single-image cache avoids decoding the wallpaper on every frame.
QImage background(const QString &path) {
  static QMutex mutex;
  static QString cachedPath;
  static QDateTime modified;
  static qint64 bytes = -1;
  static QImage cached;
  QMutexLocker lock(&mutex);
  const QFileInfo file(path);
  if (cachedPath != path || modified != file.lastModified() ||
      bytes != file.size()) {
    cachedPath = path;
    modified = file.lastModified();
    bytes = file.size();
    QImageReader reader(path);
    reader.setAutoTransform(true);
    cached = reader.read();
  }
  return cached;
}
} // namespace
QVariantMap CanvasRenderer::defaults() {
  return {{"ratio", "source"},
          {"padding", 100.0},
          {"radius", 24.0},
          {"shadow", .28},
          {"backgroundColor", "#20242c"},
          {"backgroundImage", ""}};
}
bool CanvasRenderer::validate(const QVariantMap &settings, QString *error) {
  const auto s = resolved(settings);
  auto fail = [error](const QString &message) {
    if (error)
      *error = message;
    return false;
  };
  const QStringList ratios = {"source", "16:9", "4:3",    "9:16",
                              "1:1",    "3:4",  "2.39:1", "1:2.39"};
  if (!ratios.contains(s.value("ratio").toString()))
    return fail(QStringLiteral("不支持的画布比例。"));
  for (const auto &entry : {qMakePair(QStringLiteral("padding"), 300.0),
                            qMakePair(QStringLiteral("radius"), 100.0),
                            qMakePair(QStringLiteral("shadow"), 1.0)}) {
    bool ok = false;
    const double value = s.value(entry.first).toDouble(&ok);
    if (!ok || !std::isfinite(value) || value < 0 || value > entry.second)
      return fail(QStringLiteral("画布留白、圆角或阴影数值无效。"));
  }
  const QString color = s.value("backgroundColor").toString();
  if (!color.startsWith('#') || !QColor(color).isValid())
    return fail(QStringLiteral("背景颜色无效。"));
  const QString path = s.value("backgroundImage").toString();
  if (!path.isEmpty() &&
      (!QFileInfo(path).isAbsolute() || !QFileInfo(path).isFile() ||
       background(path).isNull()))
    return fail(QStringLiteral("背景图片必须是可读取的本地图片。"));
  if (error)
    error->clear();
  return true;
}
QSize CanvasRenderer::outputSize(QSize source, const QVariantMap &settings) {
  if (source.isEmpty())
    return {};
  double aspect = double(source.width()) / source.height();
  const QString ratio = resolved(settings).value("ratio").toString();
  const auto parts = ratio.split(':');
  if (parts.size() == 2)
    aspect = parts[0].toDouble() / parts[1].toDouble();
  if (!std::isfinite(aspect) || aspect <= 0)
    return {};
  const double width = std::min(1920.0, 1080.0 * aspect);
  const double height = width / aspect;
  return {std::max(2, int(std::round(width)) & ~1),
          std::max(2, int(std::round(height)) & ~1)};
}
QImage CanvasRenderer::render(const QImage &source, const QVariantMap &settings,
                              AutoFraming::State camera) {
  if (source.isNull())
    return {};
  // Empty maps preserve pre-canvas exports and are also useful for raw
  // previews.
  if (settings.isEmpty())
    return AutoFraming::render(source, camera);
  if (!validate(settings))
    return {};
  const auto s = resolved(settings);
  const QSize size = outputSize(source.size(), s);
  QImage result(size, QImage::Format_ARGB32_Premultiplied);
  result.fill(QColor(s.value("backgroundColor").toString()));
  QPainter painter(&result);
  painter.setRenderHints(QPainter::Antialiasing |
                         QPainter::SmoothPixmapTransform);
  const QString path = s.value("backgroundImage").toString();
  if (!path.isEmpty()) {
    const QImage wallpaper = background(path);
    QSizeF cover = wallpaper.size();
    cover.scale(QSizeF(size), Qt::KeepAspectRatioByExpanding);
    painter.drawImage(QRectF((size.width() - cover.width()) / 2,
                             (size.height() - cover.height()) / 2,
                             cover.width(), cover.height()),
                      wallpaper);
  }
  // Reference style units are measured against a 1080-pixel short edge.
  const double unit = double(std::min(size.width(), size.height())) / 1080;
  const double padding = std::min(s.value("padding").toDouble() * unit,
                                  std::min(size.width(), size.height()) * .42);
  QSizeF fit = source.size();
  fit.scale(QSizeF(size.width() - padding * 2, size.height() - padding * 2),
            Qt::KeepAspectRatio);
  const QRectF video((size.width() - fit.width()) / 2,
                     (size.height() - fit.height()) / 2, fit.width(),
                     fit.height());
  const double radius = std::min(s.value("radius").toDouble() * unit,
                                 std::min(fit.width(), fit.height()) / 2);
  const double shadow = s.value("shadow").toDouble();
  // Layered falloff approximates a soft shadow; it is not the Mac blur kernel.
  if (shadow > 0) {
    painter.setPen(Qt::NoPen);
    for (int i = 16; i >= 1; --i) {
      const double spread = i * 2.0 * unit;
      painter.setBrush(
          QColor(0, 0, 0, int(255 * shadow * .075 * (17 - i) / 16)));
      painter.drawRoundedRect(video.translated(0, 10 * unit)
                                  .adjusted(-spread, -spread, spread, spread),
                              radius + spread, radius + spread);
    }
  }
  QPainterPath clip;
  clip.addRoundedRect(video, radius, radius);
  painter.setClipPath(clip);
  painter.drawImage(video, AutoFraming::render(source, camera));
  return result;
}
