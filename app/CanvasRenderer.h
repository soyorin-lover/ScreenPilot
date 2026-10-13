#pragma once
#include "AutoFraming.h"
#include <QVariantMap>

class CanvasRenderer {
public:
  static QVariantMap defaults();
  static bool validate(const QVariantMap &settings, QString *error = nullptr);
  static QRectF cropRect(QSize source, const QVariantMap &settings);
  static QRectF sourceRect(QSize source, const QVariantMap &settings, AutoFraming::State camera = {});
  static QSize croppedSize(QSize source, const QVariantMap &settings);
  static QSize outputSize(QSize source, const QVariantMap &settings);
  static QSize nativeOutputSize(QSize source, const QVariantMap &settings);
  static QImage render(const QImage &source, const QVariantMap &settings,
                       AutoFraming::State camera = {},
                       QSize previewBounds = {},
                       const QVariantMap &pointer = {},
                       QSize exportSize = {}, const QImage &backgroundFrame = {},
                       double outputSeconds = 0, double sourceSeconds = -1);
};
