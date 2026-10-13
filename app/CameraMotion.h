#pragma once
#include <QVariantMap>
namespace CameraMotion {
// Times are source-media seconds, independent of timeline cuts and playback rate.
bool validate(const QVariantMap &settings, QString *error = nullptr);
QVariantMap evaluate(const QVariantMap &settings, double sourceSeconds);
// Positive offset delays the camera relative to the screen source.
double sourceToCameraTime(const QVariantMap &settings, double sourceSeconds);
}
