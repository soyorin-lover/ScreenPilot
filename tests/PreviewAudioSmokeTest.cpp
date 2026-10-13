#include "PreviewAudioSmokeTest.h"
#include "PreviewAudioProcessor.h"
#include "PcmWaveFile.h"
#include "AudioMix.h"
#include <QProcess>
#include <QAudioOutput>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QtEndian>
#include <cmath>
#include <vector>

int runPreviewAudioSmokeTest(QGuiApplication &app, const QString &directory) {
  QDir output(directory);
  if (directory.isEmpty() ||
      (output.exists() &&
       !output.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) ||
      !QDir().mkpath(directory))
    return 2;
  QByteArray wav;
  auto u16 = [&](quint16 n) {
    char b[2];
    qToLittleEndian(n, b);
    wav.append(b, 2);
  };
  auto u32 = [&](quint32 n) {
    char b[4];
    qToLittleEndian(n, b);
    wav.append(b, 4);
  };
  constexpr int frames = 48000 * 3;
  wav.append("RIFF", 4);
  u32(36 + frames * 2);
  wav.append("WAVEfmt ", 8);
  u32(16);
  u16(1);
  u16(1);
  u32(48000);
  u32(96000);
  u16(2);
  u16(16);
  wav.append("data", 4);
  u32(frames * 2);
  for (int i = 0; i < frames; ++i)
    u16(quint16(qint16(32767 * .1 *
                       std::sin(2 * 3.141592653589793 * 440 * i / 48000))));
  QFile file(output.filePath("generated-preview-tone.wav"));
  if (!file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size())
    return 2;
  file.close();
  // A video with no audio stream reproduces the missing decoder-callback case.
  const QString video = output.filePath("generated-silent-video.mp4");
  QProcess generator;
  generator.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
      "color=c=black:s=160x90:r=30:d=3", "-an", "-c:v", "mpeg4", video});
  if (!generator.waitForFinished(15000) || generator.exitCode() != 0) return 2;
  // Embedded AAC follows decoder callbacks, unlike timer-paced sidecars.
  const QString embedded = output.filePath("generated-embedded-audio.mp4");
  generator.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
      "color=c=black:s=160x90:r=30:d=20", "-f", "lavfi", "-i",
      "sine=frequency=440:sample_rate=48000:duration=20", "-c:v", "mpeg4",
      "-c:a", "aac", "-shortest", embedded});
  if (!generator.waitForFinished(15000) || generator.exitCode() != 0) return 2;
  const QString sidecar = output.filePath("generated-independent-tone.wav");
  PcmWaveWriter writer;
  QByteArray stereo(frames * 8, '\0');
  auto *tone = reinterpret_cast<float *>(stereo.data());
  for (int i = 0; i < frames; ++i)
    tone[i * 2] = tone[i * 2 + 1] = float(.1 * std::sin(2 * 3.141592653589793 * 440 * i / 48000));
  if (!writer.open(sidecar) || !writer.append(QAudioBuffer(stereo, AudioMix::format())) ||
      !writer.finalize()) return 2;
  QVideoSink videoSink;
  QMediaPlayer player;
  player.setVideoSink(&videoSink);
  int videoFrames = 0;
  QObject::connect(&videoSink, &QVideoSink::videoFrameChanged, &app,
                   [&](const QVideoFrame &frame) { if (frame.isValid()) ++videoFrames; });
  QAudioOutput speaker;
  player.setAudioOutput(&speaker);
  PreviewAudioProcessor processor;
  processor.attach(&player);
  processor.setVolume(0); // Inspect processed PCM without changing system volume.
  processor.setMixSources(QUrl::fromLocalFile(sidecar), {}, {});
  player.setPlaybackRate(2);
  QByteArray pcm;
  QAudioFormat decodedFormat;
  bool finished = false, paused = false, pauseStable = false, seekProducedAudio = false;
  bool independentCompleted = false;
  bool legacyCompleted = false, embeddedCompleted = false;
  qsizetype legacyStart = 0;
  qsizetype embeddedStart = 0;
  qsizetype afterSeek = 0;
  qsizetype beforePause = 0;
  QObject::connect(&processor, &PreviewAudioProcessor::processedAudio, &app,
                   [&](const QAudioBuffer &buffer) {
                     decodedFormat = buffer.format();
                     pcm.append(buffer.constData<char>(), buffer.byteCount());
                   });
  auto finish = [&](QString error) {
    if (finished)
      return;
    finished = true;
    const QString processorError = processor.error();
    const qint64 finalPosition = player.position();
    const int finalMediaStatus = int(player.mediaStatus());
    player.stop();
    std::vector<double> crossings;
    const auto samples = reinterpret_cast<const float *>(pcm.constData());
    const int channels = decodedFormat.channelCount();
    if (channels > 0)
      for (int i = 1; i < pcm.size() / int(sizeof(float)) / channels; ++i)
        if (samples[(i - 1) * channels] < 0 && samples[i * channels] >= 0)
          crossings.push_back(
              i - double(samples[i * channels]) /
                      (samples[i * channels] - samples[(i - 1) * channels]));
    const double hz = crossings.size() > 1
                          ? decodedFormat.sampleRate() *
                                (crossings.size() - 1) /
                                (crossings.back() - crossings.front())
                          : 0;
    const bool pass = error.isEmpty() && processorError.isEmpty() &&
                      pauseStable && seekProducedAudio && videoFrames > 0 &&
                      independentCompleted && legacyCompleted && embeddedCompleted &&
                      embeddedStart - legacyStart > 48000 && pcm.size() - embeddedStart > 48000 && std::abs(hz - 440) < 8 &&
                      pcm.size() > 48000;
    QFile report(output.filePath("preview-audio-result.json"));
    report.open(QIODevice::WriteOnly);
    report.write(
        QJsonDocument(
            QJsonObject{
                {"success", pass},
                {"error", error.isEmpty() ? processorError : error},
                {"measuredPitchHz", hz},
                {"rate", 2},
                {"pauseStable", pauseStable},
                {"seekProducedAudio", seekProducedAudio},
                {"independentAudioWithoutEmbeddedStream", true},
                {"processedBytes", double(pcm.size())},
                {"decodedVideoFrames", videoFrames},
                {"legacyProcessedBytes", double(embeddedStart - legacyStart)},
                {"embeddedAac4xCompleted", embeddedCompleted},
                {"embeddedProcessedBytes", double(pcm.size() - embeddedStart)},
                {"independentReachedEnd", independentCompleted},
                {"finalPositionMs", double(finalPosition)},
                {"finalMediaStatus", finalMediaStatus},
                {"scope",
                 "Generated PCM through real QMediaPlayer and custom preview "
                 "processor; not microphone or acoustic loopback"}})
            .toJson());
    app.exit(pass ? 0 : 1);
  };
  QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &app,
                   [&](QMediaPlayer::MediaStatus s) {
                     if (s == QMediaPlayer::LoadedMedia) {
                       processor.reset(0, legacyCompleted ? 20 : 3);
                       player.play();
                     }
                     if (s == QMediaPlayer::EndOfMedia) {
                       if (!independentCompleted) {
                         independentCompleted = true;
                         legacyStart = pcm.size();
                         // Exercise the original decoded-PCM path in the same run.
                         QTimer::singleShot(0, &app, [&] {
                           processor.setMixSources({}, {}, {});
                           player.setPlaybackRate(1);
                           player.setSource(QUrl::fromLocalFile(file.fileName()));
                         });
                       } else if (!legacyCompleted) {
                         legacyCompleted = true;
                         embeddedStart = pcm.size();
                         QTimer::singleShot(0, &app, [&] {
                           player.setPlaybackRate(4);
                           player.setSource(QUrl::fromLocalFile(embedded));
                         });
                       } else {
                         embeddedCompleted = true;
                         finish({});
                       }
                     }
                   });
  QObject::connect(&player, &QMediaPlayer::errorOccurred, &app,
                   [&](QMediaPlayer::Error, const QString &e) { finish(e); });
  QTimer::singleShot(450, &app, [&] {
    if (finished)
      return;
    paused = true;
    player.pause();
    beforePause = pcm.size();
    QTimer::singleShot(200, &app, [&] {
      if (finished)
        return;
      pauseStable = paused && pcm.size() == beforePause;
      paused = false;
      player.setPosition(1600);
      processor.reset(1.6, 3);
      afterSeek = pcm.size();
      player.play();
      QTimer::singleShot(300, &app, [&] { seekProducedAudio = pcm.size() > afterSeek; });
    });
  });
  QTimer::singleShot(15000, &app, [&] { finish("Preview audio timeout"); });
  player.setSource(QUrl::fromLocalFile(video));
  return app.exec();
}
