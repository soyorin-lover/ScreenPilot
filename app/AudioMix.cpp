#include "AudioMix.h"
#include <algorithm>
#include <cmath>
#include <cstring>

QAudioFormat AudioMix::format() {
  QAudioFormat result;
  result.setSampleRate(48000); result.setChannelCount(2);
  result.setSampleFormat(QAudioFormat::Float);
  return result;
}
void AudioMix::close() { system_.reset(); microphone_.reset(); }
bool AudioMix::open(QUrl system, QUrl microphone, QVariantMap settings, QString *error) {
  close(); settings_ = std::move(settings);
  auto load = [error](QUrl url, std::unique_ptr<PcmWaveReader> &reader) {
    if (url.isEmpty()) return true;
    if (!url.isLocalFile()) { if (error) *error = QStringLiteral("音频素材必须是本地 WAV 文件。"); return false; }
    reader = std::make_unique<PcmWaveReader>();
    return reader->open(url.toLocalFile(), error);
  };
  if (!load(system, system_) || !load(microphone, microphone_)) { close(); return false; }
  return true;
}
QByteArray AudioMix::readFrames(qint64 first, qint64 count, QString *error) {
  if (error) error->clear();
  if (first < 0 || count < 0 || count > 48000) {
    if (error) *error = QStringLiteral("音频读取范围无效。");
    return {};
  }
  QByteArray result(count * 8, char(0));
  auto add = [&](PcmWaveReader *reader, const char *prefix, double fallback) {
    if (!reader || first >= reader->frameCount()) return true;
    QString readError;
    const QByteArray data = reader->readFrames(first, count, &readError);
    if (!readError.isEmpty()) { if (error) *error = readError; return false; }
    const QString key = QString::fromLatin1(prefix);
    const double requested = settings_.value(key + "Volume", fallback).toDouble();
    const double gain = settings_.value(key + "Muted", false).toBool() ? 0 :
        (std::isfinite(requested) ? std::clamp(requested, 0.0, 1.0) : fallback);
    for (qsizetype i = 0; i + 4 <= data.size(); i += 4) {
      float sample, mixed;
      std::memcpy(&sample, data.constData() + i, 4);
      std::memcpy(&mixed, result.constData() + i, 4);
      mixed += std::isfinite(sample) ? float(sample * gain) : 0;
      std::memcpy(result.data() + i, &mixed, 4);
    }
    return true;
  };
  if (!add(system_.get(), "system", .82) || !add(microphone_.get(), "microphone", .9)) return {};
  for (qsizetype i = 0; i < result.size(); i += 4) {
    float sample; std::memcpy(&sample, result.constData() + i, 4);
    sample = std::clamp(sample, -1.f, 1.f);
    std::memcpy(result.data() + i, &sample, 4);
  }
  return result;
}
