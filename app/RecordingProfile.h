#pragma once
#include <QSize>
#include <QString>
#include <QtGlobal>

// Capture dimensions are physical frame pixels, never QScreen logical geometry.
namespace RecordingProfile {
inline QSize outputSize(QSize source, const QString &mode) {
  if (source.width() < 2 || source.height() < 2)
    return {};
  QSize limit = mode == QStringLiteral("4k") ? QSize(3840, 2160)
                    : mode == QStringLiteral("1080p") ? QSize(1920, 1080)
                                                       : source;
  // Resolution presets follow the frame orientation, including portrait capture.
  if (source.height() > source.width() && mode != QStringLiteral("source"))
    limit.transpose();
  if (source.width() > limit.width() || source.height() > limit.height())
    source.scale(limit, Qt::KeepAspectRatio);
  // Chroma subsampling requires even dimensions. Round down without upscaling.
  source.setWidth(source.width() & ~1);
  source.setHeight(source.height() & ~1);
  return source.width() >= 2 && source.height() >= 2 ? source : QSize();
}
inline int videoBitRate(QSize size) {
  if (size.width() < 2 || size.height() < 2)
    return 0;
  return int(qBound<qint64>(qint64(4000000),
                           qint64(size.width()) * size.height() * 6,
                           qint64(48000000)));
}
} // namespace RecordingProfile
