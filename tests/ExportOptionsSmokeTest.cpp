#include "ExportOptionsSmokeTest.h"
#include "ExportController.h"
#include <QAudioBufferInput>
#include <QAudioBufferOutput>
#include <QColor>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QQueue>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoFrameInput>
#include <QVideoSink>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {
bool waitUntil(const std::function<bool()> &done, int timeout = 30000) {
  QEventLoop loop;
  QTimer poll, deadline;
  poll.setInterval(10);
  deadline.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (done())
      loop.quit();
  });
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  deadline.start(timeout);
  if (!done())
    loop.exec();
  return done();
}
bool generate(const QString &path) {
  QMediaCaptureSession session;
  QMediaRecorder recorder;
  QAudioBufferInput audio; // Qt 6.8 pending-format readiness path.
  QVideoFrameInput video;
  session.setRecorder(&recorder);
  session.setVideoFrameInput(&video);
  session.setAudioBufferInput(&audio);
  QMediaFormat format(QMediaFormat::MPEG4);
  format.setVideoCodec(QMediaFormat::VideoCodec::H264);
  format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
  recorder.setMediaFormat(format);
  recorder.setVideoResolution(QSize(160, 90));
  recorder.setVideoFrameRate(30);
  recorder.setOutputLocation(QUrl::fromLocalFile(path));
  recorder.setAutoStop(true);
  QAudioFormat pcm;
  pcm.setSampleRate(48000);
  pcm.setChannelCount(1);
  pcm.setSampleFormat(QAudioFormat::Int16);
  // A 440 Hz tone verifies pitch after AAC encoding and decoding.
  QQueue<QVideoFrame> frames;
  QQueue<QAudioBuffer> buffers;
  for (int i = 0; i < 90; ++i) {
    QImage image(160, 90, QImage::Format_RGBA8888);
    image.fill(i < 30 ? Qt::red : i < 60 ? Qt::green : Qt::blue);
    QVideoFrame frame(image);
    frame.setStartTime(qint64(i) * 1000000 / 30);
    frame.setEndTime(qint64(i + 1) * 1000000 / 30);
    frames.enqueue(frame);
    QByteArray data(1600 * 2, '\0');
    auto *samples = reinterpret_cast<qint16 *>(data.data());
    for (int j = 0; j < 1600; ++j)
      samples[j] = qint16(
          5000 * std::sin(2 * 3.141592653589793 * 440 * (i * 1600 + j) / 48000));
    buffers.enqueue(QAudioBuffer(data, pcm, qint64(i) * 1000000 / 30));
  }
  bool ve = false, ae = false, active = false;
  auto pump = [&] {
    if (active)
      return;
    active = true;
    while (!frames.isEmpty() && video.sendVideoFrame(frames.head()))
      frames.dequeue();
    while (!buffers.isEmpty() && audio.sendAudioBuffer(buffers.head()))
      buffers.dequeue();
    if (frames.isEmpty() && !ve)
      ve = video.sendVideoFrame({});
    if (buffers.isEmpty() && !ae)
      ae = audio.sendAudioBuffer({});
    active = false;
  };
  QObject::connect(&video, &QVideoFrameInput::readyToSendVideoFrame, &recorder,
                   pump);
  QObject::connect(&audio, &QAudioBufferInput::readyToSendAudioBuffer,
                   &recorder, pump);
  recorder.record();
  pump();
  return waitUntil([&] {
           return recorder.error() != QMediaRecorder::NoError ||
                  (ve && ae &&
                   recorder.recorderState() == QMediaRecorder::StoppedState);
         }) &&
         recorder.error() == QMediaRecorder::NoError;
}
QJsonObject inspect(const QString &path, bool expectedAudio, bool expectedVideo,
                    qint64 expectedDuration = 1500, bool expectCamera = false,
                    bool expectCameraEnd = false) {
  QMediaPlayer player;
  QVideoSink sink;
  QAudioBufferOutput output;
  player.setVideoSink(&sink);
  player.setAudioBufferOutput(&output);
  int count = 0, blue = 0, red = 0, cameraGreen = 0, cameraHidden = 0,
      cameraEndHidden = 0, cameraLaterGreen = 0;
  double peak = 0;
  std::vector<float> decoded;
  bool ended = false;
  QObject::connect(&sink, &QVideoSink::videoFrameChanged, &player,
                   [&](const QVideoFrame &frame) {
                     if (!frame.isValid())
                       return;
                     const QImage image = frame.toImage();
                     if (image.isNull())
                       return;
                     ++count;
                     // The inset spans x=.6..9 and its y is clamped to fit;
                     // (.75,.8) is well inside it, away from codec edges.
                     const QColor inset = image.pixelColor(
                         int(image.width() * .75), int(image.height() * .8));
                     if (frame.startTime() < 350000 && inset.green() > 120 &&
                         inset.red() < 80 && inset.blue() < 80)
                       ++cameraGreen;
                     if (frame.startTime() > 650000 && inset.red() > 150 &&
                         inset.green() < 80)
                       ++cameraHidden;
                     if (frame.startTime() < 350000 && inset.blue() > 150 &&
                         inset.red() < 80 && inset.green() < 80)
                       ++cameraEndHidden;
                     if (frame.startTime() > 650000 && inset.green() > 120 &&
                         inset.red() < 80 && inset.blue() < 80)
                       ++cameraLaterGreen;
                     const QColor color = image.pixelColor(image.width() / 2,
                                                           image.height() / 2);
                     if (frame.startTime() < 350000 && color.blue() > 150 &&
                         color.red() < 80)
                       ++blue;
                     if (frame.startTime() > 650000 && color.red() > 150 &&
                         color.blue() < 80)
                       ++red;
                   });
  QObject::connect(
      &output, &QAudioBufferOutput::audioBufferReceived, &player,
      [&](const QAudioBuffer &buffer) {
        if (!buffer.isValid())
          return;
        for (int frame = 0; frame < buffer.frameCount(); ++frame)
          decoded.push_back(buffer.format().normalizedSampleValue(
              buffer.constData<char>() + frame * buffer.format().bytesPerFrame()));
        if (buffer.format().sampleFormat() == QAudioFormat::Float) {
          for (int i = 0; i < buffer.sampleCount(); ++i)
            peak =
                std::max(peak, double(std::abs(buffer.constData<float>()[i])));
        } else if (buffer.format().sampleFormat() == QAudioFormat::Int16) {
          for (int i = 0; i < buffer.sampleCount(); ++i)
            peak = std::max(
                peak, std::abs(double(buffer.constData<qint16>()[i])) / 32768);
        }
      });
  QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &player,
                   [&](QMediaPlayer::MediaStatus state) {
                     if (state == QMediaPlayer::LoadedMedia)
                       player.play();
                     if (state == QMediaPlayer::EndOfMedia ||
                         state == QMediaPlayer::InvalidMedia)
                       ended = true;
                   });
  player.setSource(QUrl::fromLocalFile(path));
  const bool completed = waitUntil([&] { return ended; });
  // Search the retained tone band and the old rate-shifted band. A short
  // 100x export has only a few cycles, so use a spectral peak instead of
  // requiring a long zero-crossing window. Zero padding does not add evidence.
  double strongest = 0;
  int pitch = 0;
  for (int hz = 200; hz <= 2000; hz += 5) {
    double real = 0, imaginary = 0;
    for (size_t i = 0; i < decoded.size(); ++i) {
      const double window = decoded.size() > 1 ? .5 - .5 * std::cos(2 * 3.141592653589793 * i / (decoded.size() - 1)) : 1;
      const double phase = 2 * 3.141592653589793 * hz * i / 48000;
      real += decoded[i] * window * std::cos(phase);
      imaginary += decoded[i] * window * std::sin(phase);
    }
    const double energy = real * real + imaginary * imaginary;
    if (energy > strongest) { strongest = energy; pitch = hz; }
  }
  const bool pitchPassed = !expectedAudio || std::abs(pitch - 440) <= 60;
  const bool passed =
      pitchPassed && completed && player.error() == QMediaPlayer::NoError &&
      player.hasAudio() == expectedAudio &&
      player.hasVideo() == expectedVideo &&
      std::abs(player.duration() - expectedDuration) < 100 &&
      (!expectedAudio || peak > .03) &&
      (!expectedVideo || (blue > 0 && red > 0)) &&
      (!expectCamera || (cameraGreen > 0 && cameraHidden > 0)) &&
      (!expectCameraEnd || (cameraEndHidden > 0 && cameraLaterGreen > 0));
  return {{"passed", passed},
          {"durationMs", player.duration()},
          {"hasAudio", player.hasAudio()},
          {"hasVideo", player.hasVideo()},
          {"audioPeak", peak},
          {"pitchHz", pitch},
          {"pitchPassed", pitchPassed},
          {"videoFrames", count},
          {"blueFirst", blue},
          {"redSecond", red},
          {"cameraGreen", cameraGreen},
          {"cameraHidden", cameraHidden},
          {"cameraEndHidden", cameraEndHidden},
          {"cameraLaterGreen", cameraLaterGreen}};
}
} // namespace
int runExportOptionsSmokeTest(QGuiApplication &app, const QString &directory) {
  Q_UNUSED(app);
  QDir dir(directory);
  if ((dir.exists() &&
       !dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) ||
      !QDir().mkpath(directory))
    return 2;
  const QString source = dir.filePath("generated-source.mp4");
  if (!generate(source))
    return 3;
  const QVariantList clips{QVariantMap{{"sourceStart", 2.0},
                                       {"sourceDuration", 1.0},
                                       {"playbackRate", 2.0}},
                           QVariantMap{{"sourceStart", 0.0},
                                       {"sourceDuration", 1.0},
                                       {"playbackRate", 1.0}}};
  QJsonArray results;
  bool passed = true;
  for (const QString mode :
       {QStringLiteral("videoAudio"), QStringLiteral("muted"),
        QStringLiteral("audioOnly")}) {
    const QString path =
        dir.filePath(mode + (mode == "audioOnly" ? ".m4a" : ".mp4"));
    ExportController exporter;
    exporter.start(QUrl::fromLocalFile(source), clips,
                   QUrl::fromLocalFile(path), {}, {}, {{"audioMode", mode}});
    const bool complete = waitUntil([&] { return !exporter.busy(); });
    QJsonObject result =
        complete && exporter.error().isEmpty()
            ? inspect(path, mode != "muted", mode != "audioOnly")
            : QJsonObject{{"passed", false}, {"error", exporter.error()}};
    result.insert("mode", mode);
    passed = passed && result.value("passed").toBool();
    results.append(result);
  }
  {
    const QString path = dir.filePath("camera-offset.mp4");
    ExportController exporter;
    exporter.start(QUrl::fromLocalFile(source), clips,
                   QUrl::fromLocalFile(path), {}, {}, {{"audioMode", "muted"}},
                   QUrl::fromLocalFile(source),
                   {{"enabled", true},
                    {"x", .6},
                    {"y", .6},
                    {"width", .3},
                    {"shape", "rectangle"},
                    {"mirror", false},
                    {"shadow", false},
                    {"offset", 1.0}});
    const bool complete = waitUntil([&] { return !exporter.busy(); });
    QJsonObject result =
        complete && exporter.error().isEmpty()
            ? inspect(path, false, true, 1500, true)
            : QJsonObject{{"passed", false}, {"error", exporter.error()}};
    result.insert("mode", "cameraOffset");
    passed = passed && result.value("passed").toBool();
    results.append(result);
  }
  {
    const QString path = dir.filePath("camera-end.mp4");
    ExportController exporter;
    exporter.start(QUrl::fromLocalFile(source), clips,
                   QUrl::fromLocalFile(path), {}, {}, {{"audioMode", "muted"}},
                   QUrl::fromLocalFile(source),
                   {{"enabled", true},
                    {"x", .6},
                    {"y", .6},
                    {"width", .3},
                    {"shape", "rectangle"},
                    {"mirror", false},
                    {"shadow", false},
                    {"offset", -1.0}});
    const bool complete = waitUntil([&] { return !exporter.busy(); });
    QJsonObject result =
        complete && exporter.error().isEmpty()
            ? inspect(path, false, true, 1500, false, true)
            : QJsonObject{{"passed", false}, {"error", exporter.error()}};
    result.insert("mode", "cameraEnd");
    passed = passed && result.value("passed").toBool();
    results.append(result);
  }
  // Every requested rate also exercises PCM sample-count compression.
  for (double rate : {1.5, 4., 8., 12., 20., 50., 100.}) {
    const QString path = dir.filePath(QStringLiteral("rate-%1.m4a").arg(rate));
    ExportController exporter;
    exporter.start(QUrl::fromLocalFile(source),
                   {QVariantMap{{"sourceStart", 1.0},
                                {"sourceDuration", 1.0},
                                {"playbackRate", rate}}},
                   QUrl::fromLocalFile(path), {}, {},
                   {{"audioMode", "audioOnly"}});
    const bool complete = waitUntil([&] { return !exporter.busy(); });
    QJsonObject result =
        complete && exporter.error().isEmpty()
            ? inspect(path, true, false, qRound64(1000 / rate))
            : QJsonObject{{"passed", false}, {"error", exporter.error()}};
    result.insert("rate", rate);
    passed = passed && result.value("passed").toBool();
    results.append(result);
  }
  QFile report(dir.filePath("export-options-smoke-result.json"));
  if (!report.open(QIODevice::WriteOnly))
    return 4;
  report.write(
      QJsonDocument(
          QJsonObject{
              {"passed", passed},
              {"results", results},
              {"pitchPolicy",
               "Pitch-preserving streaming waveform-similarity overlap-add; extreme rates discard temporal detail."}})
          .toJson());
  return passed ? 0 : 1;
}
