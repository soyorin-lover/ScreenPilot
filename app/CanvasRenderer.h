#pragma once
#include "AutoFraming.h"
#include <QVariantMap>

class CanvasRenderer {
public:
  static QVariantMap defaults();
  static bool validate(const QVariantMap &settings, QString *error = nullptr);
  static QSize outputSize(QSize source, const QVariantMap &settings);
  static QImage render(const QImage &source, const QVariantMap &settings,
                       AutoFraming::State camera = {});
};
