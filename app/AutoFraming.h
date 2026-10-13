#pragma once
#include <QImage>
#include <QRectF>
#include <QVariantList>

// Source-time camera track shared by preview and export.
class AutoFraming {
public:
  struct State {
    double scale = 1, centerX = .5, centerY = .5;
  };
  static QVariantList generate(const QVariantList &pointerSamples,
                               double duration, QVariantMap options = {});
  static bool validateManual(const QVariantMap &segment, double duration);
  static State evaluate(const QVariantList &segments, double sourceSeconds);
  static QRectF sourceRect(QSize size, State state);
  static QImage render(const QImage &image, State state);
};
