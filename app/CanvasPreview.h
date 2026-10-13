#pragma once
#include <QImage>
#include "BackgroundVideo.h"
#include <QQuickItem>
#include <QVariantMap>
#include <QVideoSink>
#include <QVideoFrame>
#include <QThread>

// Shows the worker-composed canvas as one scene graph texture. The image is
// uploaded as-is during sync; no per-frame QPainter pass on the GUI thread.
class CanvasPreview : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(double outputTime READ outputTime WRITE setOutputTime NOTIFY outputTimeChanged)
  Q_PROPERTY(QString error READ error NOTIFY backgroundErrorChanged)
  Q_PROPERTY(QString previewQuality READ previewQuality WRITE setPreviewQuality NOTIFY previewQualityChanged)
  Q_PROPERTY(QVideoSink *videoSink READ videoSink CONSTANT)
  Q_PROPERTY(QVideoSink *cameraSink READ cameraSink CONSTANT)
  Q_PROPERTY(QVariantMap cameraSettings READ cameraSettings WRITE
                 setCameraSettings NOTIFY cameraSettingsChanged)
  Q_PROPERTY(bool cameraVisible READ cameraVisible WRITE setCameraVisible NOTIFY
                 cameraSettingsChanged)
  Q_PROPERTY(QVariantList pointerSamples READ pointerSamples WRITE setPointerSamples NOTIFY pointerSamplesChanged)
  Q_PROPERTY(double frameTime READ frameTime NOTIFY frameReady)
  Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameReady)
  Q_PROPERTY(QSize cameraFrameSize READ cameraFrameSize NOTIFY cameraFrameReady)
  Q_PROPERTY(QVariantMap canvasSettings READ canvasSettings WRITE
                 setCanvasSettings NOTIFY canvasSettingsChanged)
  Q_PROPERTY(
      QVariantMap camera READ camera WRITE setCamera NOTIFY cameraChanged)
public:
  explicit CanvasPreview(QQuickItem *parent = nullptr);
  ~CanvasPreview() override;
  Q_INVOKABLE QVideoSink *videoSink() { return &m_sink; }
  Q_INVOKABLE QVideoSink *cameraSink() { return &m_cameraSink; }
  Q_INVOKABLE void clearCameraFrame();
  Q_INVOKABLE void clearFrame();
  QVariantMap cameraSettings() const { return m_cameraSettings; }
  void setCameraSettings(const QVariantMap &settings);
  bool cameraVisible() const { return m_cameraVisible; }
  void setCameraVisible(bool value);
  Q_INVOKABLE QVariantMap performanceMetrics() const;
  QVariantList pointerSamples() const { return m_pointerSamples; }
  void setPointerSamples(const QVariantList &samples);
  double outputTime() const { return m_outputTime; }
  void setOutputTime(double value);
  QString error() const { return m_backgroundError; }
  QString previewQuality() const { return m_previewQuality; }
  void setPreviewQuality(QString value);
  double frameTime() const { return m_frameTime; }
  QImage sourceImage() const { return m_source; }
  QSize sourceFrameSize() const { return m_pendingSource.isValid() ? imageSize(m_pendingSource) : m_source.size(); }
  QSize cameraFrameSize() const { return m_pendingCamera.isValid() ? imageSize(m_pendingCamera) : m_cameraImage.size(); }
  Q_INVOKABLE QSize outputSize() const;
  QImage renderedImage() const { return m_rendered; }
  quint64 renderedSerial() const { return m_renderCount; }
  double renderedFrameTime() const { return m_renderedFrameTime; }
  bool hasFrame() const { return m_pendingSource.isValid() || !m_source.isNull(); }
  QVariantMap canvasSettings() const { return m_settings; }
  QVariantMap camera() const { return m_camera; }
  void setCanvasSettings(const QVariantMap &settings);
  void setCamera(const QVariantMap &camera);
protected:
  QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *) override;

public:
signals:
  void renderedChanged();
  void outputTimeChanged();
  void previewQualityChanged();
  void backgroundErrorChanged();
  void frameReady();
  void pointerSamplesChanged();
  void canvasSettingsChanged();
  void cameraChanged();
  void cameraFrameReady();
  void cameraSettingsChanged();

private:
  static QSize imageSize(const QVideoFrame &frame);
  void requestBackground();
  BackgroundVideo m_background;
  QImage m_backgroundImage;
  QString m_backgroundError;
  double m_outputTime = 0;
  QString m_previewQuality = "high";
  quint64 m_backgroundGeneration = 0;
  bool m_backgroundRequestPending = false;
  void refresh();
  void renderPreview();
  bool m_refreshPending = false;
  bool m_renderBusy = false, m_renderRequested = false;
  QThread m_renderThread;
  QObject *m_renderWorker = nullptr;
  quint64 m_renderGeneration = 0, m_sourceRevision = 0, m_cameraRevision = 0;
  qint64 m_discardedRenders = 0;
  qint64 m_renderCount = 0, m_renderNanos = 0, m_maxRenderNanos = 0;
  qint64 m_convertedFrames = 0, m_conversionNanos = 0;
  QVideoFrame m_pendingSource, m_pendingCamera;
  QVideoSink m_sink, m_cameraSink;
  QImage m_cameraImage;
  QVariantMap m_cameraSettings;
  bool m_cameraVisible = true;
  QVariantMap m_settings, m_camera;
  QImage m_source, m_rendered;
  bool m_textureDirty = false;
  QVariantList m_pointerSamples;
  double m_frameTime = -1;
  double m_renderedFrameTime = -1;
};
