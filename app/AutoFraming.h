#pragma once
#include <QImage>
#include <QVariantList>

// Source-time camera track shared by preview and export.
class AutoFraming {
public:
  struct State {
    double scale = 1, centerX = .5, centerY = .5;
  };
  static QVariantList generate(const QVariantList &pointerSamples,
                               double duration);
  static State evaluate(const QVariantList &segments, double sourceSeconds);
  static QImage render(const QImage &image, State state);
};
