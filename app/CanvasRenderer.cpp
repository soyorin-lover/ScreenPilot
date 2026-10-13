// Canvas layout behavior references DogSC at
// fa24059a9ba7e769152b3c1de047d96aca873e1c. Copyright (c) 2026 神烦老狗. MIT
// notice: third_party/DogSC-LICENSE. QPainter frame chrome, opening transforms,
// and layered shadows are development approximations.
#include "CanvasRenderer.h"
#include "CursorEffects.h"
#include "SceneEffects.h"
#include "ScreenEffects.h"
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
struct BackdropCache {
  QVariantMap settings;
  QSize sourceSize, outputSize;
  qint64 wallpaperKey = 0;
  QImage image;
};
} // namespace
QVariantMap CanvasRenderer::defaults() {
  QVariantMap result{{"ratio", "source"},
          {"cropX", 0.0}, {"cropY", 0.0}, {"cropWidth", 1.0}, {"cropHeight", 1.0},
          {"padding", 100.0},
          {"radius", 24.0},
          {"shadow", .28},
          {"backgroundColor", "#20242c"},
          {"backgroundImage", ""}, {"backgroundVideo", ""},
          {"screenFrame", "none"}, {"screenFrameTitle", ""}, {"screenFrameBrowserAddress", "example.com"},
          {"introEnabled", false}, {"introStyle", "scale"}, {"introDuration", 2.2},
          {"introTempo", 1.0}, {"introOrder", "screenFirst"}, {"introElementGap", .16},
          {"cursorEnabled", false}, {"cursorVisible", true},
          {"cursorSize", 24.0}, {"cursorStyle", "halo"}, {"clickRing", true}};
  const auto effects = ScreenEffects::defaults();
  for (auto it = effects.cbegin(); it != effects.cend(); ++it) result.insert(it.key(), it.value());
  return result;
}
bool CanvasRenderer::validate(const QVariantMap &settings, QString *error) {
  const auto s = resolved(settings);
  if (!ScreenEffects::validate(s, error)) return false;
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
  for (const QString &key : {QStringLiteral("cropX"), QStringLiteral("cropY"), QStringLiteral("cropWidth"), QStringLiteral("cropHeight")}) {
    bool ok = false;
    const double v = s.value(key).toDouble(&ok);
    if (!ok || !std::isfinite(v) || v < 0 || v > 1)
      return fail(QStringLiteral("Invalid crop bounds"));
  }
  if (s.value("cropWidth").toDouble() < .01 || s.value("cropHeight").toDouble() < .01 ||
      s.value("cropX").toDouble() + s.value("cropWidth").toDouble() > 1.000000001 ||
      s.value("cropY").toDouble() + s.value("cropHeight").toDouble() > 1.000000001)
    return fail(QStringLiteral("Crop must remain inside the source and be at least 1% wide and tall"));
  for (const QString &key : {QStringLiteral("cursorEnabled"), QStringLiteral("cursorVisible"), QStringLiteral("clickRing")})
    if (s.value(key).metaType().id() != QMetaType::Bool)
      return fail(QStringLiteral("Invalid cursor toggle"));
  bool cursorSizeOk = false;
  const double cursorSize = s.value("cursorSize").toDouble(&cursorSizeOk);
  if (!cursorSizeOk || !std::isfinite(cursorSize) || cursorSize < 8 || cursorSize > 96 ||
      !QStringList{"halo", "arrow", "crosshair"}.contains(s.value("cursorStyle").toString()))
    return fail(QStringLiteral("Invalid cursor size or style"));
  const QString color = s.value("backgroundColor").toString();
  if (!color.startsWith('#') || !QColor(color).isValid())
    return fail(QStringLiteral("背景颜色无效。"));
  const QString path = s.value("backgroundImage").toString();
  if (!path.isEmpty() &&
      (!QFileInfo(path).isAbsolute() || !QFileInfo(path).isFile() ||
       background(path).isNull()))
    return fail(QStringLiteral("背景图片必须是可读取的本地图片。"));
  if (!QStringList{"none", "windowLight", "windowDark", "browserLight", "browserDark", "device"}.contains(s.value("screenFrame").toString()) ||
      !QStringList{"fade", "scale", "slide"}.contains(s.value("introStyle").toString()) ||
      !QStringList{"together", "screenFirst", "cameraFirst"}.contains(s.value("introOrder").toString())) return fail("Invalid visual style");
  if (s.value("introEnabled").metaType().id() != QMetaType::Bool) return fail("Invalid intro toggle");
  for (const QString key : {QStringLiteral("screenFrameTitle"), QStringLiteral("screenFrameBrowserAddress"), QStringLiteral("backgroundVideo")})
    if (s.value(key).metaType().id() != QMetaType::QString) return fail("Invalid scene text");
  if (s.value("screenFrameTitle").toString().size() > 120 || s.value("screenFrameBrowserAddress").toString().size() > 240) return fail("Frame text too long");
  for (const QString key : {QStringLiteral("introDuration"), QStringLiteral("introTempo"), QStringLiteral("introElementGap")}) {
    bool ok = false; const double v = s.value(key).toDouble(&ok);
    const double minimum = key == "introDuration" ? .4 : key == "introTempo" ? .5 : 0.;
    const double maximum = key == "introDuration" ? 8. : key == "introElementGap" ? 1.2 : 2.;
    if (!ok || !std::isfinite(v) || v < minimum || v > maximum) return fail("Invalid intro timing");
  }
  const QString videoPath = s.value("backgroundVideo").toString();
  if (!videoPath.isEmpty() && !path.isEmpty())
    return fail(QStringLiteral("背景图片和视频不能同时启用。"));
  if (!videoPath.isEmpty() && (!QFileInfo(videoPath).isAbsolute() || !QFileInfo(videoPath).isFile() || !QStringList{"mp4", "mov"}.contains(QFileInfo(videoPath).suffix().toLower()))) return fail("Invalid local background video");
  if (error) error->clear();
  return true;
}
QRectF CanvasRenderer::cropRect(QSize source, const QVariantMap &settings) {
  const auto s = resolved(settings);
  return {s.value("cropX").toDouble() * source.width(), s.value("cropY").toDouble() * source.height(),
          s.value("cropWidth").toDouble() * source.width(), s.value("cropHeight").toDouble() * source.height()};
}
QSize CanvasRenderer::croppedSize(QSize source, const QVariantMap &settings) {
  return cropRect(source, settings).size().toSize();
}
QRectF CanvasRenderer::sourceRect(QSize source, const QVariantMap &settings, AutoFraming::State camera) {
  const QRectF crop = cropRect(source, settings);
  const double scale = std::isfinite(camera.scale) ? std::clamp(camera.scale, 1.0, 6.0) : 1.0;
  const double w = crop.width() / scale, h = crop.height() / scale;
  const double cx = std::isfinite(camera.centerX) ? camera.centerX * source.width() : crop.center().x();
  const double cy = std::isfinite(camera.centerY) ? camera.centerY * source.height() : crop.center().y();
  return {std::clamp(cx - w / 2, crop.left(), crop.right() - w),
          std::clamp(cy - h / 2, crop.top(), crop.bottom() - h), w, h};
}
QSize CanvasRenderer::outputSize(QSize source, const QVariantMap &settings) {
  if (source.isEmpty())
    return {};
  const QRectF cropped = cropRect(source, settings);
  double aspect = cropped.width() / cropped.height();
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
QSize CanvasRenderer::nativeOutputSize(QSize source, const QVariantMap &settings) {
  if (source.isEmpty())
    return {};
  const QSize cropped = croppedSize(source, settings);
  if (cropped.isEmpty())
    return {};
  const auto parts = resolved(settings).value("ratio").toString().split(':');
  double aspect = double(cropped.width()) / cropped.height();
  if (parts.size() == 2)
    aspect = parts[0].toDouble() / parts[1].toDouble();
  if (!std::isfinite(aspect) || aspect <= 0)
    return {};
  // Fit the requested canvas inside the cropped source's pixel bounds. The
  // preview's 1080p design size must not limit source-resolution exports.
  const double width = std::min(double(cropped.width()), cropped.height() * aspect);
  const double height = width / aspect;
  return {std::max(2, int(std::floor(width)) & ~1),
          std::max(2, int(std::floor(height)) & ~1)};
}
QImage CanvasRenderer::render(const QImage &source, const QVariantMap &settings,
                              AutoFraming::State camera, QSize previewBounds,
                              const QVariantMap &pointer, QSize exportSize,
                              const QImage &backgroundFrame, double outputSeconds, double sourceSeconds) {
  if (source.isNull())
    return {};
  // Empty maps preserve pre-canvas exports and are also useful for raw
  // previews.
  if (settings.isEmpty()) {
    if (exportSize.isEmpty())
      return AutoFraming::render(source, camera);
    if (exportSize.width() > 8192 || exportSize.height() > 8192)
      return {};
    QImage result(exportSize, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::black);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRectF(QPointF(0, 0), QSizeF(exportSize)), source,
                      AutoFraming::sourceRect(source.size(), camera));
    return result;
  }
  if (!validate(settings))
    return {};
  const auto s = resolved(settings);
  QSize size = exportSize.isEmpty() ? outputSize(source.size(), s) : exportSize;
  if (size.width() > 8192 || size.height() > 8192)
    return {};
  if (!previewBounds.isEmpty() && (size.width() > previewBounds.width() ||
                                   size.height() > previewBounds.height()))
    size.scale(previewBounds, Qt::KeepAspectRatio);
  const QString path = s.value("backgroundImage").toString();
  const QImage wallpaper = backgroundFrame.isNull()
      ? (path.isEmpty() ? QImage() : background(path)) : backgroundFrame;
  // The static background, sixteen shadow layers and frame chrome are
  // identical across video frames. Cache their exact rasterized pixels, before
  // drawing the source, so zoom/crop, effects and export quality stay intact.
  // Animated backgrounds/transforms use the original per-frame path.
  thread_local BackdropCache backdrop;
  const bool cacheable = !s.value("introEnabled").toBool() &&
      s.value("screenMotionSegments").toList().isEmpty() &&
      s.value("backgroundVideo").toString().isEmpty() && backgroundFrame.isNull() &&
      qint64(size.width()) * size.height() * 4 <= 64 * 1024 * 1024;
  const bool reuse = cacheable && !backdrop.image.isNull() &&
      backdrop.settings == s && backdrop.sourceSize == source.size() &&
      backdrop.outputSize == size && backdrop.wallpaperKey == wallpaper.cacheKey();
  QImage result = reuse ? backdrop.image.copy()
                       : QImage(size, QImage::Format_ARGB32_Premultiplied);
  if (!reuse) result.fill(QColor(s.value("backgroundColor").toString()));
  QPainter painter(&result);
  painter.setRenderHints(QPainter::Antialiasing |
                         QPainter::SmoothPixmapTransform);
  if (!reuse && !wallpaper.isNull()) {
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
  const QString frame = s.value("screenFrame").toString();
  const bool device = frame == "device", chrome = frame != "none" && !device;
  const double bezel = device ? 14 * unit : 0.;
  const double bar = chrome ? (frame.startsWith("browser") ? 64 : 38) * unit : 0.;
  QSizeF fit = cropRect(source.size(), s).size();
  fit.scale(QSizeF(size.width()-padding*2-bezel*2, size.height()-padding*2-bezel*2-bar), Qt::KeepAspectRatio);
  const QRectF shell((size.width()-fit.width()-bezel*2)/2, (size.height()-fit.height()-bezel*2-bar)/2, fit.width()+bezel*2, fit.height()+bezel*2+bar);
  const QRectF video(shell.left()+bezel,shell.top()+bezel+bar,fit.width(),fit.height());
  const double radius = std::min(s.value("radius").toDouble()*unit, std::min(fit.width(),fit.height())/2);
  const double effectTime = sourceSeconds >= 0 ? sourceSeconds : outputSeconds;
  // One projective screen layer keeps frame chrome and cursor attached to the
  // source while leaving the canvas background and camera compositing separate.
  painter.save();
  painter.setTransform(ScreenEffects::projection(shell, size, s, effectTime), true);
  SceneEffects::apply(painter, shell, s, outputSeconds);
  const double shadow = s.value("shadow").toDouble();
  // Layered falloff approximates a soft shadow; it is not the Mac blur kernel.
  if (!reuse && shadow > 0) {
    painter.setPen(Qt::NoPen);
    for (int i = 16; i >= 1; --i) {
      const double spread = i * 2.0 * unit;
      painter.setBrush(
          QColor(0, 0, 0, int(255 * shadow * .075 * (17 - i) / 16)));
      painter.drawRoundedRect(shell.translated(0, 10 * unit)
                                  .adjusted(-spread, -spread, spread, spread),
                              radius + spread, radius + spread);
    }
  }
  if (!reuse && frame != "none") {
    const bool dark = device || frame.endsWith("Dark");
    painter.setPen(QPen(QColor(dark ? "#50525a" : "#c8cad0"), std::max(1.,unit)));
    painter.setBrush(QColor(dark ? "#24262c" : "#f4f5f7"));
    painter.drawRoundedRect(shell, device ? 28*unit : 12*unit, device ? 28*unit : 12*unit);
    if (chrome) {
      for (int i=0;i<3;++i) { painter.setPen(Qt::NoPen); painter.setBrush(QColor(i==0 ? "#ff6058" : i==1 ? "#ffbd2e" : "#28c840")); painter.drawEllipse(QPointF(shell.left()+(16+i*17)*unit,shell.top()+19*unit),5*unit,5*unit); }
      QFont font=painter.font(); font.setPixelSize(std::max(1,int(13*unit))); painter.setFont(font);
      painter.setPen(QColor(dark ? "#eceef2" : "#34363d"));
      const bool browser=frame.startsWith("browser");
      const QRectF textRect(shell.left()+80*unit,shell.top()+(browser ? 30 : 0)*unit,std::max(0.,shell.width()-100*unit),(browser ? 30 : 38)*unit);
      painter.drawText(textRect,Qt::AlignCenter,painter.fontMetrics().elidedText(s.value(browser ? "screenFrameBrowserAddress" : "screenFrameTitle").toString(),Qt::ElideRight,int(textRect.width())));
    }
  }
  if (cacheable && !reuse) {
    backdrop.settings = s;
    backdrop.sourceSize = source.size();
    backdrop.outputSize = size;
    backdrop.wallpaperKey = wallpaper.cacheKey();
    backdrop.image = result.copy();
  }
  QPainterPath clip;
  clip.addRoundedRect(video, radius, radius);
  painter.setClipPath(clip);
  // Crop directly into the destination: avoid a full-resolution zoom image
  // followed by a second scale into the canvas.
  const QImage effectedSource = ScreenEffects::applyRegions(source, s, effectTime);
  painter.drawImage(video, effectedSource,
                    sourceRect(source.size(), s, camera));
  CursorEffects::paint(painter, video, sourceRect(source.size(), s, camera),
                       source.size(), s, pointer);
  painter.restore();
  ScreenEffects::paintStickers(painter, size, s, effectTime);
  return result;
}
