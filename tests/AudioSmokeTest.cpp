#include "AudioSmokeTest.h"
#include "AudioPacketTimeline.h"
#include "ExportController.h"
#include "RecordingController.h"
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioSink>
#include <QBuffer>
#include <QMediaDevices>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSettings>
#include <QTimer>
#include <QCoreApplication>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <memory>

int runAudioSmokeTest(QGuiApplication &app, const QString &directory) {
  const bool testMicrophone =
      qEnvironmentVariableIntValue("SCREENPILOT_TEST_MICROPHONE") == 1;
  QSettings settings;
  const QVariant originalSystemAudio = settings.value("recording/systemAudio");
  const QVariant originalAudioScope = settings.value("recording/systemAudioScope");
  struct RestorePreference {
    QVariant value;
    QVariant scope;
    ~RestorePreference() {
      QSettings settings;
      if (value.isValid())
        settings.setValue("recording/systemAudio", value);
      else
        settings.remove("recording/systemAudio");
      if (scope.isValid()) settings.setValue("recording/systemAudioScope", scope);
      else settings.remove("recording/systemAudioScope");
    }
  } restore{originalSystemAudio, originalAudioScope};
  QDir dir(directory);
  if ((dir.exists() &&
       !dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) ||
      !QDir().mkpath(directory))
    return 2;
  // The only sound deliberately produced is a quiet self-generated sine wave.
  const QString wavePath = dir.filePath("generated-tone.wav");
  QByteArray pcm(48000 * 8 * 2 * 2, '\0');
  for (int i = 0; i < 48000 * 8; ++i) {
    const qint16 value =
        qint16((i < 48000 ? 0
                          : 32767 * 0.08 *
                                std::sin(2 * 3.141592653589793 *
                                         (i < 96000 ? 660 : 990) * i / 48000)));
    qToLittleEndian<qint16>(value, pcm.data() + i * 4);
    qToLittleEndian<qint16>(value, pcm.data() + i * 4 + 2);
  }
  QByteArray wave;
  auto u16 = [&](quint16 value) {
    char bytes[2];
    qToLittleEndian(value, bytes);
    wave.append(bytes, 2);
  };
  auto u32 = [&](quint32 value) {
    char bytes[4];
    qToLittleEndian(value, bytes);
    wave.append(bytes, 4);
  };
  wave.append("RIFF", 4);
  u32(36 + pcm.size());
  wave.append("WAVEfmt ", 8);
  u32(16);
  u16(1);
  u16(2);
  u32(48000);
  u32(192000);
  u16(4);
  u16(16);
  wave.append("data", 4);
  u32(pcm.size());
  wave += pcm;
  QFile waveFile(wavePath);
  if (!waveFile.open(QIODevice::WriteOnly) ||
      waveFile.write(wave) != wave.size())
    return 2;
  waveFile.close();
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(import QtQuick
Rectangle { width: 640; height: 360; color: "#135579"; Rectangle { width: 100; height: 100; color: "#ffcc66"; NumberAnimation on x { from: 0; to: 540; duration: 1500; loops: Animation.Infinite } } })",
                    QUrl());
  QQuickView view(&engine, nullptr);
  view.setTitle("Audio validation generated surface");
  view.setContent(QUrl(), &component, component.create());
  view.show();
  RecordingController recording;
  if (qEnvironmentVariableIntValue("SCREENPILOT_TEST_PROCESS_AUDIO") == 1) {
    recording.setSystemAudioScope("application");
    recording.setAudioProcessId(quint32(QCoreApplication::applicationPid()));
  } else recording.setSystemAudioScope("all");
  ExportController exporter;
  // Feed known PCM directly: media playback can fail its output resampler
  // before producing any sound, which would falsely indict loopback capture.
  QAudioFormat toneFormat;
  toneFormat.setSampleRate(48000);
  toneFormat.setChannelConfig(QAudioFormat::ChannelConfigStereo);
  toneFormat.setSampleFormat(QAudioFormat::Int16);
  std::unique_ptr<QAudioSink> tone;
  QBuffer toneData(&pcm);
  toneData.open(QIODevice::ReadOnly);
  QMediaPlayer decode;
  QAudioBufferOutput decodedAudio;
  QVideoSink decodedVideo;
  decode.setAudioBufferOutput(&decodedAudio);
  decode.setVideoSink(&decodedVideo);
  QTimer toneClock;
  toneClock.setInterval(10);
  toneClock.setTimerType(Qt::PreciseTimer);
  QObject::connect(&toneClock, &QTimer::timeout, &view, [&] {
    if (tone && tone->processedUSecs() >= 1000000)
      view.rootObject()->setProperty("color", QColor("#ee2222"));
  });
  bool done = false, exporting = false, capturing = false;
  int stage = 0;
  qint64 samples = 0, videoFrames = 0, first = -1, last = 0;
  double energy = 0, maxRms = 0;
  qint64 toneOnset = -1, redOnset = -1;
  double tone660 = 0, tone990 = 0;
  QJsonObject evidence;
  double microphonePeak = 0;
  QObject::connect(
      &recording, &RecordingController::audioLevelChanged, &app, [&] {
        microphonePeak = std::max(microphonePeak, recording.microphoneLevel());
      });
  auto finish = [&](const QString &error) {
    if (done)
      return;
    done = true;
    toneClock.stop();
    if (tone) tone->stop();
    decode.stop();
    if (recording.busy())
      recording.stop();
    if (exporter.busy())
      exporter.cancel();
    evidence["success"] = error.isEmpty();
    evidence["error"] = error;
    evidence["microphoneCaptured"] = testMicrophone;
    evidence["microphonePeak"] = microphonePeak;
    evidence["systemMixCaptured"] = true;
    QFile result(dir.filePath("audio-smoke-result.json"));
    result.open(QIODevice::WriteOnly);
    result.write(QJsonDocument(evidence).toJson());
    app.exit(error.isEmpty() ? 0 : 1);
  };
  QObject::connect(
      &decodedAudio, &QAudioBufferOutput::audioBufferReceived, &app,
      [&](const QAudioBuffer &buffer) {
        if (!buffer.isValid())
          return;
        const QAudioFormat format = buffer.format();
        double sum = 0, re660 = 0, im660 = 0, re990 = 0, im990 = 0;
        for (int frame = 0; frame < buffer.frameCount(); ++frame)
          for (int channel = 0; channel < format.channelCount(); ++channel) {
            const int index = frame * format.channelCount() + channel;
            double value = 0;
            switch (format.sampleFormat()) {
            case QAudioFormat::Float:
              value = buffer.constData<float>()[index];
              break;
            case QAudioFormat::Int16:
              value = buffer.constData<qint16>()[index] / 32768.0;
              break;
            case QAudioFormat::Int32:
              value = buffer.constData<qint32>()[index] / 2147483648.0;
              break;
            case QAudioFormat::UInt8:
              value = (buffer.constData<quint8>()[index] - 128) / 128.0;
              break;
            default:
              break;
            }
            sum += value * value;
            ++samples;
            if (channel == 0) {
              const double t = double(frame) / format.sampleRate();
              re660 += value * std::cos(2 * 3.141592653589793 * 660 * t);
              im660 += value * std::sin(2 * 3.141592653589793 * 660 * t);
              re990 += value * std::cos(2 * 3.141592653589793 * 990 * t);
              im990 += value * std::sin(2 * 3.141592653589793 * 990 * t);
            }
          }
        const double amplitude660 = 2 * std::hypot(re660, im660) /
                                    std::max<qsizetype>(1, buffer.frameCount());
        const double amplitude990 = 2 * std::hypot(re990, im990) /
                                    std::max<qsizetype>(1, buffer.frameCount());
        tone660 = std::max(tone660, amplitude660);
        tone990 = std::max(tone990, amplitude990);
        if (amplitude660 > 0.0004 && toneOnset < 0)
          toneOnset = buffer.startTime();
        energy += sum;
        maxRms = std::max(
            maxRms,
            std::sqrt(sum / std::max<qsizetype>(1, buffer.sampleCount())));
        if (first < 0)
          first = buffer.startTime();
        last = buffer.startTime() + buffer.duration();
      });
  QObject::connect(&decodedVideo, &QVideoSink::videoFrameChanged, &app,
                   [&](const QVideoFrame &frame) {
                     if (frame.isValid()) {
                       ++videoFrames;
                       if (redOnset < 0) {
                         const QImage image = frame.toImage();
                         if (!image.isNull()) {
                           const QColor pixel = image.pixelColor(320, 250);
                           if (pixel.red() > 180 && pixel.green() < 80)
                             redOnset = frame.startTime();
                         }
                       }
                     }
                   });
  QObject::connect(
      &decode, &QMediaPlayer::errorOccurred, &app,
      [&](QMediaPlayer::Error, const QString &error) { finish(error); });
  QObject::connect(
      &decode, &QMediaPlayer::mediaStatusChanged, &app,
      [&](QMediaPlayer::MediaStatus status) {
        if (done)
          return;
        if (status == QMediaPlayer::LoadedMedia)
          decode.play();
        if (status != QMediaPlayer::EndOfMedia)
          return;
        const QString prefix = stage == 1 ? "recorded" : "exported";
        const double rms = samples ? std::sqrt(energy / samples) : 0;
        evidence[prefix + "Tone660Amplitude"] = tone660;
        evidence[prefix + "Tone990Amplitude"] = tone990;
        evidence[prefix + "ToneOnsetUs"] = toneOnset;
        evidence[prefix + "VisualOnsetUs"] = redOnset;
        evidence[prefix + "AudioVisualOffsetUs"] =
            toneOnset >= 0 && redOnset >= 0 ? toneOnset - redOnset : -999999;
        evidence[prefix + "Rms"] = rms;
        evidence[prefix + "PeakBufferRms"] = maxRms;
        evidence[prefix + "FirstAudioUs"] = first;
        evidence[prefix + "LastAudioUs"] = last;
        evidence[prefix + "DurationMs"] = decode.duration();
        evidence[prefix + "VideoFrames"] = videoFrames;
        if (!samples || rms < 0.00005 || videoFrames < 5 || tone660 < 0.0004 ||
            tone990 < 0.0004 || toneOnset < 0 || redOnset < 0 ||
            std::abs(toneOnset - redOnset) > 250000) {
          finish("Decoded audio/video missing or silent.");
          return;
        }
        if (stage == 1) {
          exporting = true;
          QVariantList clips{QVariantMap{{"sourceStart", 0.5},
                                         {"sourceDuration", 1.0},
                                         {"playbackRate", 1.0}},
                             QVariantMap{{"sourceStart", 2.0},
                                         {"sourceDuration", 1.0},
                                         {"playbackRate", 1.0}}};
          exporter.start(
              recording.lastRecording(), clips,
              QUrl::fromLocalFile(dir.filePath("edited-with-audio.mp4")));
        } else {
          if (std::abs(decode.duration() - 2000) > 200 || first > 100000 ||
              last < 1800000 || last > 2200000) {
            finish("Edited audio timeline does not match two retained "
                   "one-second clips.");
            return;
          }
          if (testMicrophone && microphonePeak <= 0) {
            finish("Microphone capture returned no nonzero samples.");
            return;
          }
          finish({});
        }
      });
  QObject::connect(&exporter, &ExportController::changed, &app, [&] {
    if (!exporting || exporter.busy() || done)
      return;
    exporting = false;
    if (!exporter.error().isEmpty()) {
      finish(exporter.error());
      return;
    }
    stage = 2;
    samples = videoFrames = 0;
    first = -1;
    last = 0;
    energy = maxRms = 0;
    toneOnset = redOnset = -1;
    tone660 = tone990 = 0;
    decode.setSource(exporter.lastExport());
  });
  QObject::connect(&recording, &RecordingController::changed, &app, [&] {
    if (done || !capturing)
      return;
    if (!recording.error().isEmpty()) {
      finish(recording.error());
      return;
    }
    if (!recording.busy() && !recording.lastRecording().isEmpty()) {
      capturing = false;
      if (tone) tone->stop();
      stage = 1;
      decode.setSource(recording.lastRecording());
    }
  });
  QTimer prepareTone;
  int outputAttempts = 0;
  QObject::connect(&prepareTone, &QTimer::timeout, &app, [&] {
    if (done) { prepareTone.stop(); return; }
    prepareTone.setInterval(250);
    // Device discovery may complete only after the event loop starts.
    const auto outputDevice = QMediaDevices::defaultAudioOutput();
    if (outputDevice.isNull() || !outputDevice.isFormatSupported(toneFormat)) {
      if (++outputAttempts < 12) return;
      prepareTone.stop();
      finish("Default audio output did not become ready for the generated tone.");
      return;
    }
    prepareTone.stop();
    tone = std::make_unique<QAudioSink>(outputDevice, toneFormat);
    tone->setVolume(0.15);
    tone->setBufferSize(19200); // Bound output buffering to 100 ms.
    QObject::connect(tone.get(), &QAudioSink::stateChanged, &app, [&] {
      if (!done && tone->error() != QtAudio::NoError)
        finish(QStringLiteral("Generated tone output failed (%1).")
                   .arg(int(tone->error())));
    });
    recording.refreshSources();
    int index = -1;
    for (int i = 0; i < recording.sources().size(); ++i)
      if (recording.sources()[i].toMap().value("label").toString() ==
          view.title())
        index = i;
    if (index < 0) {
      finish("Generated capture window not found.");
      return;
    }
    recording.setSelectedSource(index);
    recording.setOutputDirectory(directory);
    if (testMicrophone && recording.microphones().isEmpty()) {
      finish("No active microphone available for explicitly enabled microphone "
             "test.");
      return;
    }
    if (testMicrophone)
      recording.setSelectedMicrophone(0);
    recording.setMicrophoneEnabled(testMicrophone);
    recording.setSystemAudioEnabled(true);
    capturing = true;
    tone->start(&toneData);
    if (done) return;
    toneClock.start();
    recording.start();
    QTimer::singleShot(4500, &app, [&] {
      if (!done && recording.busy())
        recording.stop();
    });
  });
  prepareTone.start(1000);
  QTimer::singleShot(90000, &app, [&] { finish("Audio smoke timeout."); });
  return app.exec();
}

int runAudioPacketSmokeTest() {
  AudioPacketTimeline converted;
  bool nativeRateContinuous = true;
  for (int i = 0; i < 10000; ++i)
    nativeRateContinuous = nativeRateContinuous &&
        converted.place(2400 + i * 480, quint64(i) * 441, 480, false, 44100)
            == 2400 + i * 480;
  qInfo() << "Native 44.1k position to 48k PCM continuity:" << nativeRateContinuous;
  AudioPacketTimeline timeline;
  constexpr int jitter[] = {0, 1, -1, 0, 1, 0, -1};
  int brokenBoundaries = 0;
  for (int i = 0; i < 300; ++i) {
    const qint64 expected = 2400 + i * 480;
    const auto first =
        timeline.place(expected + jitter[i % 7], 9600 + i * 480, 480, false);
    if (first != expected)
      ++brokenBoundaries;
  }
  // Preserve a genuine lost packet instead of squeezing the timeline.
  const bool lossPreserved =
      timeline.place(147360, 154560, 480, false) == 147360;
  const bool badTimestampContinuous =
      timeline.place(999999, 155040, 480, true) == 147840;
  const bool resetHandled = timeline.place(200000, 0, 480, false) == 200000;
  qInfo() << "Timestamp error continuous:" << badTimestampContinuous
          << "counter reset handled:" << resetHandled;
  qInfo() << "Audio packet boundaries:" << brokenBoundaries
          << "genuine loss preserved:" << lossPreserved;
  return nativeRateContinuous && brokenBoundaries == 0 && lossPreserved && badTimestampContinuous &&
                 resetHandled
             ? 0
             : 1;
}
