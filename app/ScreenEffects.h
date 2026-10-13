#pragma once
#include <QImage>
#include <QRectF>
#include <QTransform>
#include <QVariantMap>
class QPainter;

class ScreenEffects {
public:
  static QVariantMap defaults();
  static bool validate(const QVariantMap &settings, QString *error = nullptr);
  static QVariantMap evaluate(const QVariantMap &settings, double sourceSeconds);
  static QTransform projection(QRectF shell, QSize canvas, const QVariantMap &settings, double sourceSeconds);
  static QImage applyRegions(const QImage &source, const QVariantMap &settings, double sourceSeconds);
  static void paintStickers(QPainter &painter, QSize canvas, const QVariantMap &settings, double sourceSeconds);
};
