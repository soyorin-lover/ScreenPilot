#pragma once
#include <QCapturableWindow>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
class QScreen;
class QMediaCaptureSession;
class QScreenCapture;
class QWindowCapture;
class QMediaRecorder;
class QAudioInput;
class QVideoSink;
class QTimer;

class RecordingController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
  Q_PROPERTY(int selectedSource READ selectedSource WRITE setSelectedSource
                 NOTIFY changed)
  Q_PROPERTY(bool recording READ recording NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE
                 setOutputDirectory NOTIFY changed)
  Q_PROPERTY(QUrl lastRecording READ lastRecording NOTIFY changed)
  Q_PROPERTY(qint64 duration READ duration NOTIFY changed)
  Q_PROPERTY(bool microphoneEnabled READ microphoneEnabled WRITE
                 setMicrophoneEnabled NOTIFY changed)
  Q_PROPERTY(
      QObject *previewSink READ previewSink WRITE setPreviewSink NOTIFY changed)
public:
  explicit RecordingController(QObject *parent = nullptr);
  ~RecordingController() override;
  QVariantList sources() const { return m_sources; }
  int selectedSource() const { return m_selected; }
  bool recording() const { return m_recording; }
  bool busy() const { return m_busy; }
  QString status() const { return m_status; }
  QString error() const { return m_error; }
  QString outputDirectory() const { return m_directory; }
  QUrl lastRecording() const { return m_last; }
  qint64 duration() const { return m_duration; }
  bool microphoneEnabled() const { return m_microphone; }
  QObject *previewSink() const;
  void setSelectedSource(int value);
  void setOutputDirectory(const QString &value);
  void setMicrophoneEnabled(bool value);
  void setPreviewSink(QObject *value);
  Q_INVOKABLE void refreshSources();
  Q_INVOKABLE void start();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void openOutputDirectory();
  Q_INVOKABLE void openLastRecording();
signals:
  void changed();

private:
  struct Source {
    QPointer<QScreen> screen;
    QCapturableWindow window;
    bool isScreen;
  };
  void createRecorder();
  void fail(const QString &message);
  void finish();
  void stopCapture();
  QVariantList m_sources;
  QList<Source> m_targets;
  int m_selected = -1;
  bool m_recording = false, m_busy = false, m_microphone = false;
  bool m_waitingFrame = false, m_failed = false, m_stopRequested = false;
  QString m_status = QStringLiteral("就绪"), m_error, m_directory, m_pending;
  QUrl m_last;
  qint64 m_duration = 0;
  QMediaCaptureSession *m_session;
  QScreenCapture *m_screen;
  QWindowCapture *m_window;
  QMediaRecorder *m_recorder = nullptr;
  QAudioInput *m_audio;
  QVideoSink *m_sink;
  QPointer<QVideoSink> m_preview;
  QTimer *m_watchdog;
};
