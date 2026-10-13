#pragma once
#include <QVariantMap>
#include <QRectF>
class QPainter;
namespace SceneEffects {
// Both layers use edited-output time; all elements settle by introDuration.
void apply(QPainter &painter, const QRectF &bounds, const QVariantMap &settings,
           double outputSeconds, bool camera = false);
}
