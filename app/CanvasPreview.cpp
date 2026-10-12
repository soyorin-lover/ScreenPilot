#include "CanvasPreview.h"
#include "CanvasRenderer.h"
#include <QPainter>
#include <QVideoFrame>

CanvasPreview::CanvasPreview(QQuickItem *parent)
    : QQuickPaintedItem(parent), m_settings(CanvasRenderer::defaults()) {
  setAntialiasing(true);
  connect(&m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            // Pausing/seeking can clear the sink; retain the last displayed
            // image.
            if (!frame.isValid())
              return;
            const QImage image = frame.toImage();
            if (image.isNull())
              return;
            m_source = image;
            refresh();
            emit frameReady();
          });
}
void CanvasPreview::clearFrame() {
  m_source = {};
  m_rendered = {};
  update();
  emit frameReady();
}
void CanvasPreview::setCanvasSettings(const QVariantMap &settings) {
  if (m_settings == settings || !CanvasRenderer::validate(settings))
    return;
  m_settings = settings;
  emit canvasSettingsChanged();
  refresh();
}
void CanvasPreview::setCamera(const QVariantMap &camera) {
  if (m_camera == camera)
    return;
  m_camera = camera;
  emit cameraChanged();
  refresh();
}
void CanvasPreview::refresh() {
  m_rendered =
      CanvasRenderer::render(m_source, m_settings,
                             {m_camera.value("scale", 1.0).toDouble(),
                              m_camera.value("centerX", .5).toDouble(),
                              m_camera.value("centerY", .5).toDouble()});
  update();
}
void CanvasPreview::paint(QPainter *painter) {
  if (m_rendered.isNull())
    return;
  QSizeF fit = m_rendered.size();
  fit.scale(QSizeF(width(), height()), Qt::KeepAspectRatio);
  painter->setRenderHint(QPainter::SmoothPixmapTransform);
  painter->drawImage(QRectF((width() - fit.width()) / 2,
                            (height() - fit.height()) / 2, fit.width(),
                            fit.height()),
                     m_rendered);
}
