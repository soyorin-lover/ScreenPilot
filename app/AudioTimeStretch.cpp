#include "AudioTimeStretch.h"
#include <algorithm>
#include <cmath>
#include <limits>

void AudioTimeStretch::reset(const QAudioFormat &format, double rate) {
  format_ = format;
  format_.setSampleFormat(QAudioFormat::Float);
  rate_ = std::max(1.0, rate);
  hop_ = std::max(32, format.sampleRate() / 100);
  search_ = std::max(8, format.sampleRate() / 200);
  base_ = received_ = emitted_ = 0;
  input_.clear();
  tail_.clear();
}
QByteArray AudioTimeStretch::process(const QAudioBuffer &buffer) {
  const int channels = format_.channelCount();
  if (!buffer.isValid() || channels <= 0)
    return {};
  const auto sourceFormat = buffer.format();
  const char *data = buffer.constData<char>();
  const int bytes = sourceFormat.bytesPerSample();
  for (int i = 0; i < buffer.sampleCount(); ++i)
    input_.push_back(sourceFormat.normalizedSampleValue(data + i * bytes));
  received_ += buffer.frameCount();
  return drain(false, std::numeric_limits<qint64>::max());
}
QByteArray AudioTimeStretch::finish(qint64 exactOutputFrames) {
  return drain(true, std::max<qint64>(0, exactOutputFrames));
}
QByteArray AudioTimeStretch::drain(bool final, qint64 limit) {
  QByteArray result;
  const int channels = format_.channelCount();
  if (channels <= 0 || received_ == 0)
    return result;
  if (rate_ == 1) {
    const qint64 frames = std::min(received_ - base_, limit - emitted_);
    if (frames > 0) {
      result.append(reinterpret_cast<const char *>(input_.data()), int(frames * channels * sizeof(float)));
      emitted_ += frames;
      base_ += frames;
      input_.erase(input_.begin(), input_.begin() + frames * channels);
    }
    return result;
  }
  while (emitted_ < limit) {
    const qint64 desired = qRound64(double(emitted_) * rate_);
    if (!final && (desired + search_ + 2 * hop_ > received_ ||
                   double(emitted_ + hop_) * rate_ > received_))
      break;
    const qint64 maximum = std::max(base_, received_ - 2 * hop_);
    qint64 chosen = std::clamp(desired, base_, maximum);
    if (!tail_.empty()) {
      double best = -std::numeric_limits<double>::infinity();
      const qint64 low = std::max(base_, chosen - search_);
      const qint64 high = std::min(maximum, chosen + search_);
      // Use all channels with a common alignment to preserve stereo phase.
      for (qint64 candidate = low; candidate <= high; ++candidate) {
        double dot = 0, energy = 1e-12;
        for (int f = 0; f < hop_; f += 4)
          for (int c = 0; c < channels; ++c) {
            const float sample = input_[size_t((candidate - base_ + f) * channels + c)];
            dot += sample * tail_[size_t(f * channels + c)];
            energy += sample * sample;
          }
        const double score = dot / std::sqrt(energy);
        if (score > best) { best = score; chosen = candidate; }
      }
    }
    const int count = int(std::min<qint64>(hop_, limit - emitted_));
    // A short final clip uses its available original samples, never a
    // resampling step. Its requested output may be shorter than one grain.
    for (int f = 0; f < count; ++f)
      for (int c = 0; c < channels; ++c) {
        const qint64 index = std::min(received_ - base_ - 1, chosen - base_ + f);
        float value = input_[size_t(index * channels + c)];
        if (!tail_.empty()) {
          const float blend = float(f) / hop_;
          value = tail_[size_t(f * channels + c)] * (1 - blend) + value * blend;
        }
        result.append(reinterpret_cast<const char *>(&value), sizeof(value));
      }
    tail_.resize(size_t(hop_ * channels));
    for (int f = 0; f < hop_; ++f)
      for (int c = 0; c < channels; ++c) {
        const qint64 index = std::min(received_ - base_ - 1, chosen - base_ + hop_ + f);
        tail_[size_t(f * channels + c)] = input_[size_t(index * channels + c)];
      }
    emitted_ += count;
    const qint64 keep = std::min(std::max(base_, received_ - 2 * hop_),
        std::max(base_, qRound64(double(emitted_) * rate_) - search_));
    input_.erase(input_.begin(), input_.begin() + (keep - base_) * channels);
    base_ = keep;
    if (!final && emitted_ >= qint64(double(received_) / rate_))
      break;
  }
  return result;
}
