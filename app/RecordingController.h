#pragma once
#include <QCapturableWindow>
#include "PcmWaveFile.h"
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QRectF>
#include <QVideoFrame>
#include <QUrl>
#include <QVariantList>
#include <memory>
struct RecordingBufferMetrics;
class QScreen;
class QWindow;
class QMediaCaptureSession;
class QScreenCapture;
class QWindowCapture;
class QMediaRecorder;
class QAudioBufferInput;
class QVideoFrameInput;
class WindowsAudioCapture;
class QVideoSink;
class QTimer;

class RecordingController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int frameRate READ frameRate WRITE setFrameRate NOTIFY changed)
  Q_PROPERTY(QRectF captureRegion READ captureRegion WRITE setCaptureRegion NOTIFY changed)
  Q_PROPERTY(QString resolutionMode READ resolutionMode WRITE setResolutionMode NOTIFY changed)
  Q_PROPERTY(QString videoCodec READ videoCodec WRITE setVideoCodec NOTIFY changed)
  Q_PROPERTY(QVariantList availableVideoCodecs READ availableVideoCodecs NOTIFY changed)
  Q_PROPERTY(QSize effectiveVideoSize READ effectiveVideoSize NOTIFY changed)
  Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
  Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY changed)
  Q_PROPERTY(QVariantMap readiness READ readiness NOTIFY changed)
  Q_PROPERTY(int selectedSource READ selectedSource WRITE setSelectedSource
                 NOTIFY changed)
  Q_PROPERTY(bool recording READ recording NOTIFY changed)
  Q_PROPERTY(bool paused READ paused NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE
                 setOutputDirectory NOTIFY changed)
  Q_PROPERTY(QUrl lastSystemAudio READ lastSystemAudio NOTIFY changed)
  Q_PROPERTY(QUrl lastMicrophoneAudio READ lastMicrophoneAudio NOTIFY changed)
  Q_PROPERTY(QUrl lastRecording READ lastRecording NOTIFY changed)
  Q_PROPERTY(
      QVariantList lastPointerSamples READ lastPointerSamples NOTIFY changed)
  Q_PROPERTY(QVariantList recordingMarkers READ recordingMarkers NOTIFY changed)
  Q_PROPERTY(QVariantList lastRecordingMarkers READ lastRecordingMarkers NOTIFY changed)
  Q_PROPERTY(qint64 duration READ duration NOTIFY changed)
  Q_PROPERTY(bool microphoneEnabled READ microphoneEnabled WRITE
                 setMicrophoneEnabled NOTIFY changed)
  Q_PROPERTY(
      QObject *previewSink READ previewSink WRITE setPreviewSink NOTIFY changed)
  Q_PROPERTY(QString systemAudioScope READ systemAudioScope WRITE setSystemAudioScope NOTIFY changed)
  Q_PROPERTY(quint32 audioProcessId READ audioProcessId WRITE setAudioProcessId NOTIFY changed)
  Q_PROPERTY(bool systemAudioEnabled READ systemAudioEnabled WRITE
                 setSystemAudioEnabled NOTIFY changed)
  Q_PROPERTY(QVariantList microphones READ microphones NOTIFY changed)
  Q_PROPERTY(int selectedMicrophone READ selectedMicrophone WRITE
                 setSelectedMicrophone NOTIFY changed)
  Q_PROPERTY(
      double microphoneLevel READ microphoneLevel NOTIFY audioLevelChanged)
public:
  int frameRate() const { return m_frameRate; }
  QRectF captureRegion() const { return m_captureRegion; }
  void setFrameRate(int value);
  Q_INVOKABLE void setCaptureRegion(const QRectF &normalized);
  Q_INVOKABLE void clearCaptureRegion();
  QString resolutionMode() const { return m_resolutionMode; }
  QString videoCodec() const { return m_videoCodec; }
  QVariantList availableVideoCodecs() const;
  QSize effectiveVideoSize() const { return m_effectiveVideoSize; }
  Q_INVOKABLE QVariantMap performanceMetrics() const;
  void setResolutionMode(const QString &value);
  void setVideoCodec(const QString &value);
  QString systemAudioScope() const { return m_systemAudioScope; }
  quint32 audioProcessId() const { return m_audioProcessId; }
  void setSystemAudioScope(const QString &scope);
  void setAudioProcessId(quint32 processId);
  bool systemAudioEnabled() const { return m_systemAudio; }
  QVariantList microphones() const { return m_microphones; }
  int selectedMicrophone() const { return m_selectedMicrophone; }
  double microphoneLevel() const { return m_microphoneLevel; }
  void setSystemAudioEnabled(bool value);
  void setSelectedMicrophone(int value);
  explicit RecordingController(QObject *parent = nullptr);
  ~RecordingController() override;
  QVariantList sources() const { return m_sources; }
  QVariantMap capabilities() const;
  QVariantMap readiness() const;
  int selectedSource() const { return m_selected; }
  bool recording() const { return m_recording; }
  bool paused() const { return m_paused; }
  bool busy() const { return m_busy; }
  QString status() const { return m_status; }
  QString error() const { return m_error; }
  QString outputDirectory() const { return m_directory; }
  QUrl lastSystemAudio() const { return m_lastSystemAudio; }
  QUrl lastMicrophoneAudio() const { return m_lastMicrophoneAudio; }
  QUrl lastRecording() const { return m_last; }
  QVariantList lastPointerSamples() const { return m_lastPointerSamples; }
  QVariantList recordingMarkers() const { return m_pendingMarkers; }
  QVariantList lastRecordingMarkers() const { return m_lastMarkers; }
  Q_INVOKABLE bool addRecordingMarker();
  // Keeps recorder helper windows (the floating recording bar) out of the
  // captured video. Returns whether Windows confirmed the exclusion.
  Q_INVOKABLE bool excludeFromCapture(QWindow *window);
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
  Q_INVOKABLE void pause();
  Q_INVOKABLE void resume();
  Q_INVOKABLE void discard();
  Q_INVOKABLE void restart();
  Q_INVOKABLE void openOutputDirectory();
  Q_INVOKABLE void openLastRecording();
signals:
  void changed();
  void audioLevelChanged();
  void mediaStarted(QString directory);
  void mediaStopping();
  void mediaPaused(bool paused);
  void mediaDiscarding();
  void discarded();
  void restartRequested();

private:
  struct Source {
    QPointer<QScreen> screen;
    QCapturableWindow window;
    bool isScreen;
  };
  void createRecorder();
  void scheduleVideoFrame();
  void pumpVideoFrame();
  void metric(const QString &key, qint64 value = 1);
  void timingMetric(const QString &key, qint64 nanoseconds);
  QRect capturePixels(const QSize &size) const;
  void beginPointerCapture();
  void samplePointer();
  void fail(const QString &message);
  void finish();
  void stopCapture();
  QString m_resolutionMode = QStringLiteral("source");
  QString m_videoCodec = QStringLiteral("h264");
  QSize m_effectiveVideoSize;
  QVariantList m_sources;
  QList<Source> m_targets;
  int m_selected = -1;
  bool m_recording = false, m_busy = false, m_microphone = false;
  bool m_waitingFrame = false, m_failed = false, m_stopRequested = false;
  bool m_paused = false, m_discard = false, m_restart = false;
  qint64 m_pauseStarted = 0, m_pausedNsecs = 0;
  QString m_status = QStringLiteral("就绪"), m_error, m_directory, m_pending;
  QUrl m_last, m_lastSystemAudio, m_lastMicrophoneAudio;
  PcmWaveWriter m_systemWriter, m_microphoneWriter;
  QString m_pendingSystemAudio, m_pendingMicrophoneAudio;
  qint64 m_duration = 0;
  QMediaCaptureSession *m_session;
  QMediaCaptureSession *m_sourceSession;
  QVideoFrameInput *m_videoInput;
  QTimer *m_videoTimer;
  QVideoFrame m_latestVideoFrame, m_pendingVideoFrame;
  qint64 m_nextVideoIndex = 0;
  qint64 m_mixedAudioFrames = 0;
  bool m_pumpingVideo = false;
  bool m_measurePerformance = false;
  QVariantMap m_performanceMetrics;
  QElapsedTimer m_performanceClock;
  qint64 m_lastCaptureNs = -1, m_lastSentUs = -1;
  std::shared_ptr<RecordingBufferMetrics> m_bufferMetrics;
  int m_frameRate = 60;
  QRectF m_captureRegion{0, 0, 1, 1};
  QScreenCapture *m_screen;
  QWindowCapture *m_window;
  QMediaRecorder *m_recorder = nullptr;
  WindowsAudioCapture *m_nativeAudio;
  QAudioBufferInput *m_bufferInput;
  QVariantList m_microphones;
  int m_selectedMicrophone = -1;
  bool m_systemAudio = true;
  QString m_systemAudioScope = QStringLiteral("all");
  quint32 m_audioProcessId = 0;
  double m_microphoneLevel = 0;
  QVideoSink *m_sink;
  QPointer<QVideoSink> m_preview;
  QTimer *m_watchdog;
  QTimer *m_pointerTimer;
  QElapsedTimer m_pointerClock;
  QVariantList m_pendingPointerSamples, m_lastPointerSamples;
  QVariantList m_pendingMarkers, m_lastMarkers;
  bool m_leftButtonDown = false;
  quintptr m_pointerWindow = 0;
};
