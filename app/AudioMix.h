#pragma once
#include "PcmWaveFile.h"
#include <QAudioFormat>
#include <QUrl>
#include <QVariantMap>
#include <memory>

// Bounded, seekable source-time mixer shared by preview and export.
class AudioMix {
public:
  bool open(QUrl system, QUrl microphone, QVariantMap settings, QString *error = nullptr);
  void close();
  bool hasSources() const { return bool(system_) || bool(microphone_); }
  void setSettings(QVariantMap settings) { settings_ = std::move(settings); }
  static QAudioFormat format();
  QByteArray readFrames(qint64 first, qint64 count, QString *error = nullptr);
private:
  std::unique_ptr<PcmWaveReader> system_, microphone_;
  QVariantMap settings_;
};
