#pragma once
#include <QImage>
#include <QQuickPaintedItem>
#include <QVariantMap>
#include <QVideoSink>

class CanvasPreview : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(QVideoSink *videoSink READ videoSink CONSTANT)
  Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameReady)
  Q_PROPERTY(QVariantMap canvasSettings READ canvasSettings WRITE
                 setCanvasSettings NOTIFY canvasSettingsChanged)
  Q_PROPERTY(
      QVariantMap camera READ camera WRITE setCamera NOTIFY cameraChanged)
public:
  explicit CanvasPreview(QQuickItem *parent = nullptr);
  Q_INVOKABLE QVideoSink *videoSink() { return &m_sink; }
  Q_INVOKABLE void clearFrame();
  bool hasFrame() const { return !m_source.isNull(); }
  QVariantMap canvasSettings() const { return m_settings; }
  QVariantMap camera() const { return m_camera; }
  void setCanvasSettings(const QVariantMap &settings);
  void setCamera(const QVariantMap &camera);
  void paint(QPainter *painter) override;
signals:
  void frameReady();
  void canvasSettingsChanged();
  void cameraChanged();

private:
  void refresh();
  QVideoSink m_sink;
  QVariantMap m_settings, m_camera;
  QImage m_source, m_rendered;
};
