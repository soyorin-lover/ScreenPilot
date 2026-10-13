#pragma once
#include "AudioTimeStretch.h"
#include "AudioMix.h"
#include <QAudioBufferOutput>
#include <QAudioSink>
#include <QMediaPlayer>
#include <QMediaDevices>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>

// Qt 6.8 emits original-rate PCM even when video playback is accelerated.
class PreviewAudioProcessor final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed)
  Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
public:
  explicit PreviewAudioProcessor(QObject *parent = nullptr);
  ~PreviewAudioProcessor() override;
  void attach(QMediaPlayer *player);
  bool active() const;
  bool muted() const { return m_muted; }
  void setMuted(bool value);
  double volume() const { return m_volume; }
  void setVolume(double value);
  QString error() const { return m_error; }
  Q_INVOKABLE void reset(double sourceStart = 0, double sourceEnd = -1);
  Q_INVOKABLE void setMixSources(QUrl system, QUrl microphone, QVariantMap settings);
signals:
  void changed();
  void processedAudio(const QAudioBuffer &buffer);

private:
  void receive(const QAudioBuffer &buffer);
  void pump();
  void drainOutput();
  void scheduleMix();
  void consume(const QAudioBuffer &buffer);
  void flush();
  void enqueue(const QByteArray &pcm);
  void rateChanged();
  QPointer<QMediaPlayer> m_player;
  QAudioBufferOutput m_buffers;
  QMediaDevices m_devices;
  std::unique_ptr<QAudioSink> m_sink;
  QIODevice *m_device = nullptr;
  QAudioFormat m_format, m_sinkFormat;
  AudioTimeStretch m_stretch;
  AudioMix m_mix;
  QUrl m_systemSource, m_microphoneSource;
  QVariantMap m_mixSettings;
  QTimer m_timer;
  QByteArray m_pending;
  qint64 m_nextMixFrame = 0;
  qint64 m_target = 0, m_end = -1, m_inputFrames = 0;
  bool m_finished = false, m_draining = false;
  bool m_waiting = true, m_muted = false;
  double m_volume = .8;
  QString m_error;
};
