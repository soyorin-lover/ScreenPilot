#include "AudioTimeStretchSmokeTest.h"
#include "AudioTimeStretch.h"
#include <QAudioFormat>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <vector>
namespace {
double frequency(const QByteArray &data) {
  const auto *samples = reinterpret_cast<const float *>(data.constData());
  const int count = data.size() / int(sizeof(float));
  std::vector<double> crossings;
  for (int i = 1; i < count; ++i)
    if (samples[i - 1] < 0 && samples[i] >= 0)
      crossings.push_back(i - double(samples[i]) / (samples[i] - samples[i - 1]));
  return crossings.size() < 2 ? 0 :
      48000.0 * (crossings.size() - 1) / (crossings.back() - crossings.front());
}
}
int runAudioTimeStretchSmokeTest() {
  QAudioFormat format;
  format.setSampleRate(48000);
  format.setChannelCount(1);
  format.setSampleFormat(QAudioFormat::Float);
  bool passed = true;
  for (double rate : {1., 1.5, 2., 4., 8., 12., 20., 50., 100.}) {
    AudioTimeStretch stretch;
    stretch.reset(format, rate);
    QByteArray output;
    constexpr int frames = 48000 * 20;
    for (int start = 0; start < frames; start += 777) {
      const int count = std::min(777, frames - start);
      QByteArray input(count * int(sizeof(float)), '\0');
      auto *samples = reinterpret_cast<float *>(input.data());
      for (int i = 0; i < count; ++i)
        samples[i] = float(.4 * std::sin(2 * 3.141592653589793 * 440 * (start + i) / 48000));
      output += stretch.process(QAudioBuffer(input, format));
    }
    output += stretch.finish(qRound64(frames / rate));
    const double measured = frequency(output);
    const bool ok = output.size() / int(sizeof(float)) == qRound64(frames / rate) && std::abs(measured - 440) < 4.4;
    qInfo() << "Pitch smoke rate" << rate << "Hz" << measured << "passed" << ok;
    passed &= ok;
  }
  // The old sample-skipping path fails the same tone criterion at 2x.
  QByteArray old(48000 * int(sizeof(float)), '\0');
  auto *oldSamples = reinterpret_cast<float *>(old.data());
  for (int i = 0; i < 48000; ++i)
    oldSamples[i] = float(.4 * std::sin(2 * 3.141592653589793 * 440 * i * 2 / 48000));
  passed &= std::abs(frequency(old) - 440) > 100;
  return passed ? 0 : 1;
}

