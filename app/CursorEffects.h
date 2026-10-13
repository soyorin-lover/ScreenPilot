#pragma once
#include <QVariantList>
#include <QRectF>
#include <QSize>
class QPainter;
// Optional emphasis overlays; captured native cursor pixels remain in the video.
class CursorEffects {
public:
  static QVariantMap evaluate(const QVariantList &samples, double sourceSeconds);
  static void paint(QPainter &, QRectF video, QRectF sourceCrop, QSize sourceSize,
                    const QVariantMap &settings, const QVariantMap &pointer);
};
