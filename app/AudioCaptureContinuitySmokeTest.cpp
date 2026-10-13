#include "AudioCaptureContinuitySmokeTest.h"
#include "WindowsAudioCapture.h"
#include <QAudioOutput>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

int runAudioCaptureContinuitySmokeTest(QGuiApplication &app, const QString &directory) {
  QDir dir(directory);
  if ((dir.exists() && !dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty()) || !QDir().mkpath(directory)) return 2;
  constexpr int rate = 48000, binFrames = 960;
  constexpr double pi = 3.14159265358979323846;
  // The sole stimulus is a quiet six-second generated 1kHz tone, no microphone or video.
  QByteArray pcm(rate * 6 * 4, '\0'), wave;
  for (int i = 0; i < rate * 6; ++i) {
    const qint16 value = qint16(32767 * .08 * std::sin(2 * pi * 1000 * i / rate));
    qToLittleEndian(value, pcm.data() + i * 4);
    qToLittleEndian(value, pcm.data() + i * 4 + 2);
  }
  auto u16 = [&](quint16 v) { char b[2]; qToLittleEndian(v, b); wave.append(b, 2); };
  auto u32 = [&](quint32 v) { char b[4]; qToLittleEndian(v, b); wave.append(b, 4); };
  wave.append("RIFF", 4); u32(36 + pcm.size()); wave.append("WAVEfmt ", 8);
  u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
  wave.append("data", 4); u32(pcm.size()); wave += pcm;
  QFile file(dir.filePath("generated-continuous-tone.wav"));
  if (!file.open(QIODevice::WriteOnly) || file.write(wave) != wave.size()) return 2;
  file.close();
  WindowsAudioCapture capture;
  QMediaPlayer player;
  QAudioOutput speaker;
  speaker.setVolume(.15);
  player.setAudioOutput(&speaker);
  player.setSource(QUrl::fromLocalFile(file.fileName()));
  struct Bin { double energy = 0, re = 0, im = 0; int frames = 0; };
  std::vector<Bin> bins(400);
  QJsonArray phases;
  bool finished = false, active = false;
  int phase = 0, blocks = 0;
  qint64 next = -1, missingFrames = 0, overlappingFrames = 0;
  double baseline = 0;
  auto finish = [&](const QString &error) {
    if (finished) return;
    finished = true; active = false; capture.stop(); player.stop();
    QJsonObject result{{"success", error.isEmpty()}, {"error", error},
      {"scope", "Generated quiet 1kHz system-output tone; no microphone or video; baseline and GUI-stressed six-second phases"},
      {"bin_ms", 20}, {"stress_gui_block_ms", 180}, {"phases", phases}};
    QSaveFile output(dir.filePath("audio-capture-continuity-result.json"));
    const QByteArray json = QJsonDocument(result).toJson();
    const bool saved = output.open(QIODevice::WriteOnly) && output.write(json) == json.size() && output.commit();
    app.exit(saved ? (error.isEmpty() ? 0 : 1) : 2);
  };
  QObject::connect(&capture, &WindowsAudioCapture::failed, &app, [&](const QString &e) { finish(e); });
  QObject::connect(&player, &QMediaPlayer::errorOccurred, &app, [&](QMediaPlayer::Error, const QString &e) { finish(e); });
  QObject::connect(&capture, &WindowsAudioCapture::bufferReady, &app, [&](const QAudioBuffer &buffer) {
    if (!active || !buffer.isValid()) return;
    if (buffer.format() != WindowsAudioCapture::format()) { finish("Unexpected capture PCM format"); return; }
    const qint64 first = qRound64(buffer.startTime() * double(rate) / 1000000);
    if (next >= 0) {
      missingFrames += std::max(qint64(0), first - next);
      overlappingFrames += std::max(qint64(0), next - first);
    }
    next = first + buffer.frameCount();
    const float *data = buffer.constData<float>();
    for (int i = 0; i < buffer.frameCount(); ++i) {
      const qint64 frame = first + i, index = frame / binFrames;
      if (frame < 0 || index >= qint64(bins.size())) continue;
      auto &bin = bins[size_t(index)];
      const double value = (data[2 * i] + data[2 * i + 1]) * .5;
      const double angle = 2 * pi * 1000 * frame / rate;
      bin.energy += value * value; bin.re += value * std::cos(angle); bin.im += value * std::sin(angle); ++bin.frames;
    }
  });
  std::function<void()> begin;
  begin = [&] {
    bins.assign(400, {}); next = -1; missingFrames = overlappingFrames = 0; blocks = 0;
    QString error;
    if (!capture.start(true, {}, &error)) { finish(error); return; }
    active = true; player.setPosition(0); player.play();
    if (phase == 1) for (int at : {1000, 2000, 3000, 4000, 5000})
      QTimer::singleShot(at, &app, [&] { if (active && !finished && phase == 1) { ++blocks; QThread::msleep(180); } });
    QTimer::singleShot(6700, &app, [&] {
      if (finished) return;
      capture.flush(); active = false; capture.stop(); player.stop();
      // Ignore startup latency and both tone boundaries. Check every 20ms of the continuous interior.
      std::vector<double> amplitudes;
      QJsonArray windows;
      for (int b = 40; b < 260; ++b) {
        const auto &bin = bins[size_t(b)];
        const double amplitude = 2 * std::hypot(bin.re, bin.im) / binFrames;
        amplitudes.push_back(amplitude);
        windows.append(QJsonObject{{"time_ms", b * 20}, {"tone_amplitude", amplitude},
          {"rms", std::sqrt(bin.energy / binFrames)}, {"frames", bin.frames}});
      }
      auto sorted = amplitudes; std::sort(sorted.begin(), sorted.end());
      const double median = sorted[sorted.size() / 2];
      if (phase == 0) baseline = median;
      int bad = 0, weakTone = 0, run = 0, longest = 0;
      for (size_t i = 0; i < amplitudes.size(); ++i) {
        const bool weak = amplitudes[i] < std::max(1e-5, baseline * .35);
        weakTone += weak;
        // Other applications may contribute energy at the test frequency.
        // A weak narrow-band estimate with loud PCM is contamination, not a
        // missing capture block; report it separately instead of mislabelling it.
        const double rms = std::sqrt(bins[i + 40].energy / binFrames);
        if (weak && rms < std::max(1e-5, baseline * .25)) { ++bad; longest = std::max(longest, ++run); } else run = 0;
      }
      phases.append(QJsonObject{{"phase", phase == 0 ? "baseline" : "gui_stressed"},
        {"median_tone_amplitude", median}, {"low_tone_windows", weakTone}, {"missing_audio_windows", bad}, {"longest_low_tone_ms", longest * 20},
        {"missing_pts_frames", missingFrames}, {"overlapping_pts_frames", overlappingFrames},
        {"gui_blocks", blocks}, {"windows", windows}});
      if (baseline < 1e-5) { finish("Generated tone not measurable in system loopback; check default output/mute"); return; }
      if (bad > 0 || missingFrames > 0 || overlappingFrames > 0) { finish("Continuous tone has missing/attenuated 20ms windows or discontinuous PCM timestamps"); return; }
      if (phase++ == 0) QTimer::singleShot(400, &app, begin);
      else finish({});
    });
  };
  QTimer::singleShot(0, &app, begin);
  QTimer::singleShot(20000, &app, [&] { finish("Audio continuity test timeout"); });
  return app.exec();
}
