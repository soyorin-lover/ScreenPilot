#pragma once
#include <QAudioBuffer>
#include <QByteArray>
#include <vector>

// Streaming waveform-similarity overlap-add. Input is decoded at its original
// sample rate; output retains that rate and channel layout in Float PCM.
class AudioTimeStretch {
public:
  void reset(const QAudioFormat &format, double rate);
  QByteArray process(const QAudioBuffer &buffer);
  QByteArray finish(qint64 exactOutputFrames);
  QAudioFormat outputFormat() const { return format_; }
private:
  QByteArray drain(bool final, qint64 limit);
  QAudioFormat format_;
  double rate_ = 1;
  int hop_ = 0, search_ = 0;
  qint64 base_ = 0, received_ = 0, emitted_ = 0;
  std::vector<float> input_, tail_;
};
