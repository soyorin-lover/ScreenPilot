#pragma once
#include <QCameraDevice>
#include <QElapsedTimer>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaRecorder>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVideoSink>
class QCamera;
class CameraController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
  Q_PROPERTY(bool previewActive READ previewActive WRITE setPreviewActive NOTIFY
                 changed)
  Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
  Q_PROPERTY(QString selectedDevice READ selectedDevice WRITE setSelectedDevice
                 NOTIFY changed)
  Q_PROPERTY(QVariantList formats READ formats NOTIFY changed)
  Q_PROPERTY(int selectedFormat READ selectedFormat WRITE setSelectedFormat
                 NOTIFY changed)
  Q_PROPERTY(bool mirror READ mirror WRITE setMirror NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool recording READ recording NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QUrl lastRecording READ lastRecording NOTIFY changed)
  Q_PROPERTY(double offset READ offset NOTIFY changed)
  Q_PROPERTY(
      QObject *previewSink READ previewSink WRITE setPreviewSink NOTIFY changed)
public:
  explicit CameraController(QObject *parent = nullptr);
  bool enabled() const { return m_enabled; }
  void setEnabled(bool value);
  bool previewActive() const { return m_previewActive; }
  void setPreviewActive(bool value);
  QVariantList devices() const;
  QString selectedDevice() const { return m_selectedDevice; }
  void setSelectedDevice(const QString &value);
  QVariantList formats() const;
  int selectedFormat() const { return m_selectedFormat; }
  void setSelectedFormat(int value);
  bool mirror() const { return m_mirror; }
  void setMirror(bool value);
  bool busy() const { return m_pending; }
  bool recording() const {
    return m_recorder.recorderState() == QMediaRecorder::RecordingState;
  }
  QString error() const { return m_error; }
  QString status() const;
  QUrl lastRecording() const { return m_lastRecording; }
  double offset() const { return m_offset; }
  QObject *previewSink() const { return m_previewSink; }
  void setPreviewSink(QObject *sink);
  Q_INVOKABLE bool startRecording(const QString &directory);
  Q_INVOKABLE void stopRecording();
  Q_INVOKABLE void setRecordingPaused(bool paused);
  Q_INVOKABLE void discardRecording();
signals:
  void changed();
  void recordingFinished(QUrl source, double offsetSeconds);
  void recordingFailed(QString message);

private:
  void refreshDevices();
  void configureCamera();
  void fail(const QString &message);
  QMediaDevices m_devices;
  QMediaCaptureSession m_session;
  QMediaRecorder m_recorder;
  QVideoSink m_sink;
  QPointer<QVideoSink> m_previewSink;
  QCamera *m_camera = nullptr;
  QList<QCameraDevice> m_available;
  QString m_selectedDevice;
  int m_selectedFormat = -1;
  bool m_enabled = false, m_mirror = true, m_pending = false;
  bool m_sawRecording = false, m_awaitingFrame = false;
  bool m_stopping = false;
  bool m_discard = false;
  bool m_previewActive = true;
  QString m_error;
  QUrl m_lastRecording, m_requestedOutput;
  QElapsedTimer m_anchor;
  double m_offset = 0;
};
