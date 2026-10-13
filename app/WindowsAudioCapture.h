#pragma once
#include <QAudioBuffer>
#include <QObject>
#include <QVariantList>
#include <memory>

// One monotonic timeline for loopback and microphone; no acoustic monitoring.
class WindowsAudioCapture final : public QObject {
  Q_OBJECT
public:
  explicit WindowsAudioCapture(QObject *parent = nullptr);
  ~WindowsAudioCapture() override;
  static QVariantList microphones();
  static QAudioFormat format();
  static bool supportsApplicationAudio();
  bool start(bool system, const QString &microphoneId, QString *error, quint32 processId = 0);
  void flush();
  void stop();
signals:
  void bufferReady(const QAudioBuffer &buffer);
  void systemBufferReady(const QAudioBuffer &buffer);
  void microphoneBufferReady(const QAudioBuffer &buffer);
  void levelChanged(double level);
  void failed(const QString &message);

private:
  struct Private;
  std::unique_ptr<Private> d;
};
