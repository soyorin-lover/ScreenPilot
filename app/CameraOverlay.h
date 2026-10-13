#pragma once
#include <QImage>
#include <QVariantMap>
class QPainter;
namespace CameraOverlay {
QVariantMap defaults();
bool validate(const QVariantMap &settings, QString *error = nullptr);
void paint(QPainter &painter, QSize targetSize, const QImage &camera,
           const QVariantMap &settings, const QVariantMap &sceneSettings = {},
           double outputSeconds = 0);
} // namespace CameraOverlay
