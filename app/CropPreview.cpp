#include "CropPreview.h"
#include <QPainter>
CropPreview::CropPreview(QQuickItem *parent) : QQuickPaintedItem(parent) {
  connect(this, &QQuickItem::widthChanged, this, [this] { emit imageRectChanged(); update(); });
  connect(this, &QQuickItem::heightChanged, this, [this] { emit imageRectChanged(); update(); });
}
void CropPreview::setSourcePreview(CanvasPreview *source) {
  if (m_source == source) return;
  if (m_source) disconnect(m_source, nullptr, this, nullptr);
  m_source = source;
  if (source) {
    connect(source, &CanvasPreview::frameReady, this, [this] { emit imageRectChanged(); update(); });
    connect(source, &CanvasPreview::renderedChanged, this, [this] { update(); });
    connect(source, &QObject::destroyed, this, [this] { emit sourcePreviewChanged(); emit imageRectChanged(); update(); });
  }
  emit sourcePreviewChanged(); emit imageRectChanged(); update();
}
QRectF CropPreview::imageRect() const {
  if (!m_source || !m_source->hasFrame()) return {};
  // Geometry bindings also run for hidden crop/effect inspectors. Reading
  // dimensions must never trigger a full video-frame GPU readback.
  QSizeF fit = m_source->sourceFrameSize();
  fit.scale(QSizeF(width(), height()), Qt::KeepAspectRatio);
  return {(width() - fit.width()) / 2, (height() - fit.height()) / 2, fit.width(), fit.height()};
}
void CropPreview::paint(QPainter *painter) {
  if (!m_source) return;
  painter->setRenderHint(QPainter::SmoothPixmapTransform);
  painter->drawImage(imageRect(), m_source->sourceImage());
}
