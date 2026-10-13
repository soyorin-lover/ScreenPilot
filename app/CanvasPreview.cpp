#include "CanvasPreview.h"
#include "FrameConversion.h"
#include "CameraOverlay.h"
#include "CameraMotion.h"
#include "CanvasRenderer.h"
#include "CursorEffects.h"
#include <QElapsedTimer>
#include <QPainter>
#include <QQuickWindow>
#include <QTimer>
#include <QVideoFrame>
#include <cmath>

namespace {
// A request owns its inputs. The worker never reads a QQuickItem or a player.
struct PreviewRequest {
  QVideoFrame sourceFrame, cameraFrame;
  QImage source, cameraImage, background;
  QVariantMap settings, camera, cameraSettings;
  QVariantList pointerSamples;
  QSize bounds;
  double sourceTime = -1, outputTime = 0;
  bool cameraVisible = true;
};
struct PreviewResult {
  QImage source, camera, composite;
  qint64 conversionNanos = 0, renderNanos = 0;
  bool convertedSource = false;
};
PreviewResult render(PreviewRequest request) {
  PreviewResult result;
  QElapsedTimer clock;
  clock.start();
  result.source = request.sourceFrame.isValid() ? FrameConversion::toImage(request.sourceFrame) : request.source;
  result.camera = request.cameraFrame.isValid() ? FrameConversion::toImage(request.cameraFrame) : request.cameraImage;
  result.convertedSource = request.sourceFrame.isValid();
  result.conversionNanos = clock.nsecsElapsed();
  if (result.source.isNull()) return result;
  clock.restart();
  result.composite = CanvasRenderer::render(result.source, request.settings,
      {request.camera.value("scale", 1.0).toDouble(),
       request.camera.value("centerX", .5).toDouble(),
       request.camera.value("centerY", .5).toDouble()}, request.bounds,
      CursorEffects::evaluate(request.pointerSamples, request.sourceTime), {},
      request.background, request.outputTime, request.sourceTime);
  if (request.cameraVisible && !result.camera.isNull() && !result.composite.isNull()) {
    QPainter painter(&result.composite);
    CameraOverlay::paint(painter, result.composite.size(), result.camera,
        CameraMotion::evaluate(request.cameraSettings, request.sourceTime),
        request.settings, request.outputTime);
  }
  result.renderNanos = clock.nsecsElapsed();
  return result;
}
} // namespace

QSize CanvasPreview::imageSize(const QVideoFrame &frame) {
  QSize size = frame.size();
  // toImage applies surface rotation, but not presentation-only frame rotation.
  // Match its dimensions without converting pixels for geometry bindings.
  const auto rotation = frame.surfaceFormat().rotation();
  if (rotation == QtVideo::Rotation::Clockwise90 ||
      rotation == QtVideo::Rotation::Clockwise270)
    size.transpose();
  return size;
}
QSize CanvasPreview::outputSize() const { return CanvasRenderer::outputSize(sourceFrameSize(),m_settings); }
CanvasPreview::CanvasPreview(QQuickItem *parent)
    : QQuickPaintedItem(parent), m_settings(CanvasRenderer::defaults()) {
  setAntialiasing(true);
  m_renderWorker = new QObject;
  m_renderWorker->moveToThread(&m_renderThread);
  connect(&m_renderThread, &QThread::finished, m_renderWorker, &QObject::deleteLater);
  m_renderThread.start();
  connect(&m_cameraSink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            if (!frame.isValid())
              return;
            m_pendingCamera = frame;
            ++m_cameraRevision;
            refresh();
            emit cameraFrameReady();
          });
  connect(this, &QQuickItem::widthChanged, this, &CanvasPreview::refresh);
  connect(this, &QQuickItem::heightChanged, this, &CanvasPreview::refresh);
  connect(&m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            // Pausing/seeking can clear the sink; retain the last displayed
            // image.
            if (!frame.isValid())
              return;
            m_frameTime = frame.startTime() / 1000000.0;
            // At high playback rates Qt can deliver several decoded frames
            // before the GUI can paint. Convert only the newest one when the
            // coalesced render runs; obsolete GPU readbacks must not starve it.
            m_pendingSource = frame;
            ++m_sourceRevision;
            refresh();
            emit frameReady();
          });
}
CanvasPreview::~CanvasPreview() {
  // The one in-flight job may post its result while we wait; QObject removes
  // that queued callback on destruction. No worker accesses item members.
  m_renderThread.quit();
  m_renderThread.wait();
}
void CanvasPreview::setOutputTime(double value) {
  if (m_outputTime == value) return;
  m_outputTime = value;
  emit outputTimeChanged();
  if (!m_settings.value("backgroundVideo").toString().isEmpty())
    requestBackground();
  if (m_settings.value("introEnabled").toBool())
    refresh();
}
void CanvasPreview::setPreviewQuality(QString value) {
  if (value == m_previewQuality || !QStringList{"high","medium","low"}.contains(value)) return;
  m_previewQuality = value;
  emit previewQualityChanged();
  refresh();
}
void CanvasPreview::requestBackground() {
  // Position notifications can arrive faster than a media seek completes.
  // Keep the current decode alive and coalesce later positions into one target;
  // replacing the request on every playback tick can starve frame delivery.
  if (m_backgroundRequestPending) return;
  m_backgroundRequestPending = true;
  const double requestedTime = m_outputTime;
  const quint64 generation = ++m_backgroundGeneration;
  m_background.request(requestedTime, [this, generation, requestedTime](QImage image, QString error) {
    if (generation != m_backgroundGeneration) return;
    m_backgroundRequestPending = false;
    m_backgroundImage = image;
    if (m_backgroundError != error) { m_backgroundError = error; emit backgroundErrorChanged(); }
    refresh();
    if (m_outputTime != requestedTime) requestBackground();
  });
}
void CanvasPreview::setPointerSamples(const QVariantList &samples) {
  if (m_pointerSamples == samples) return;
  m_pointerSamples = samples;
  emit pointerSamplesChanged();
  refresh();
}
void CanvasPreview::clearCameraFrame() {
  ++m_renderGeneration;
  ++m_cameraRevision;
  m_pendingCamera = {};
  m_cameraImage = {};
  emit cameraFrameReady();
  refresh();
}
void CanvasPreview::setCameraSettings(const QVariantMap &settings) {
  if (m_cameraSettings == settings)
    return;
  m_cameraSettings = settings;
  emit cameraSettingsChanged();
  refresh();
}
void CanvasPreview::setCameraVisible(bool value) {
  if (m_cameraVisible == value)
    return;
  m_cameraVisible = value;
  emit cameraSettingsChanged();
  refresh();
}
void CanvasPreview::clearFrame() {
  ++m_renderGeneration;
  ++m_sourceRevision;
  m_pendingSource = {};
  m_source = {};
  m_frameTime = -1;
  m_rendered = {};
  m_renderedFrameTime = -1;
  update();
  emit frameReady();
  emit renderedChanged();
}
void CanvasPreview::setCanvasSettings(const QVariantMap &settings) {
  if (m_settings == settings || !CanvasRenderer::validate(settings))
    return;
  const QString path = settings.value("backgroundVideo").toString();
  if (m_background.path() != path) {
    ++m_backgroundGeneration;
    m_backgroundRequestPending = false;
    m_backgroundImage = {};
    m_background.setPath(path);
  }
  m_settings = settings;
  requestBackground();
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
QVariantMap CanvasPreview::performanceMetrics() const {
  return {
      {"converted_frames", m_convertedFrames},
      {"conversion_total_ms", m_conversionNanos / 1000000.0},
      {"rendered_frames", m_renderCount},
      {"discarded_renders", m_discardedRenders},
      {"pending_jobs", int(m_renderBusy) + int(m_renderRequested)},
      {"render_mean_ms",
       m_renderCount ? double(m_renderNanos) / m_renderCount / 1000000.0 : 0.0},
      {"render_max_ms", m_maxRenderNanos / 1000000.0}};
}
void CanvasPreview::refresh() {
  // Frame arrival, camera and layout changes in one event-loop turn share one
  // render of the newest state instead of blocking playback several times.
  m_renderRequested = true;
  if (m_refreshPending || m_renderBusy)
    return;
  m_refreshPending = true;
  QTimer::singleShot(0, this, [this] {
    m_refreshPending = false;
    renderPreview();
  });
}
void CanvasPreview::renderPreview() {
  m_renderRequested = false;
  if (!hasFrame() || width() <= 0 || height() <= 0)
    return;
  const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
  QSize bounds(std::max(2, int(std::ceil(width() * dpr))),
                     std::max(2, int(std::ceil(height() * dpr))));
  const QSize limit = m_previewQuality == "low" ? QSize(854,480) :
      m_previewQuality == "medium" ? QSize(1280,720) : QSize(1920,1080);
  if (bounds.width() > limit.width() || bounds.height() > limit.height()) bounds.scale(limit,Qt::KeepAspectRatio);
  PreviewRequest request{m_pendingSource, m_pendingCamera, m_source, m_cameraImage,
      m_backgroundImage, m_settings, m_camera, m_cameraSettings, m_pointerSamples,
      bounds, m_frameTime, m_outputTime, m_cameraVisible};
  const auto generation = m_renderGeneration, sourceRevision = m_sourceRevision,
             cameraRevision = m_cameraRevision;
  m_renderBusy = true;
  QMetaObject::invokeMethod(m_renderWorker,
      [owner = this, request = std::move(request), generation, sourceRevision, cameraRevision]() mutable {
    const double sourceTime = request.sourceTime;
    auto result = render(std::move(request));
    QMetaObject::invokeMethod(owner,
        [owner, result = std::move(result), generation, sourceRevision, cameraRevision, sourceTime]() mutable {
      owner->m_renderBusy = false;
      if (generation == owner->m_renderGeneration && !result.composite.isNull()) {
        owner->m_source = std::move(result.source);
        owner->m_cameraImage = std::move(result.camera);
        if (sourceRevision == owner->m_sourceRevision) owner->m_pendingSource = {};
        if (cameraRevision == owner->m_cameraRevision) owner->m_pendingCamera = {};
        owner->m_rendered = std::move(result.composite);
        owner->m_renderedFrameTime = sourceTime;
        owner->m_convertedFrames += result.convertedSource;
        owner->m_conversionNanos += result.conversionNanos;
        ++owner->m_renderCount;
        owner->m_renderNanos += result.renderNanos;
        owner->m_maxRenderNanos = std::max(owner->m_maxRenderNanos, result.renderNanos);
        owner->update();
        emit owner->renderedChanged();
      } else {
        ++owner->m_discardedRenders;
      }
      if (owner->m_renderRequested) owner->refresh();
    }, Qt::QueuedConnection);
  }, Qt::QueuedConnection);
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
