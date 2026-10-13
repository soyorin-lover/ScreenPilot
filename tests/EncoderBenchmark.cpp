#include "EncoderBenchmark.h"
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoFrameInput>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
// A few distinct moving frames so the encoder cannot skip work.
std::vector<QVideoFrame> makeFrames(QVideoFrameFormat::PixelFormat pixel, QSize size) {
  std::vector<QVideoFrame> frames;
  for (int n = 0; n < 8; ++n) {
    QVideoFrame frame(QVideoFrameFormat(size, pixel));
    frame.map(QVideoFrame::WriteOnly);
    for (int plane = 0; plane < frame.planeCount(); ++plane) {
      uchar *bits = frame.bits(plane);
      const int stride = frame.bytesPerLine(plane);
      const int rows = frame.mappedBytes(plane) / stride;
      for (int y = 0; y < rows; ++y)
        for (int x = 0; x < stride; ++x)
          bits[y * stride + x] = uchar((x * (plane + 1) + y * 2 + n * 37) ^ (x / 64 * 17));
    }
    frame.unmap();
    frames.push_back(frame);
  }
  return frames;
}
} // namespace

int runEncoderBenchmark(QGuiApplication &app, const QString &directory) {
  QDir().mkpath(directory);
  const QSize size(2560, 1440);
  const int count = 600, fps = 60;
  QJsonObject report;
  for (auto pixel : {QVideoFrameFormat::Format_BGRA8888_Premultiplied, QVideoFrameFormat::Format_NV12,
                     QVideoFrameFormat::Format_YUV420P}) {
    const auto frames = makeFrames(pixel, size);
    QMediaCaptureSession session;
    QMediaRecorder recorder;
    QVideoFrameInput input(frames.front().surfaceFormat());
    session.setRecorder(&recorder);
    session.setVideoFrameInput(&input);
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    recorder.setMediaFormat(format);
    recorder.setVideoResolution(size);
    recorder.setVideoFrameRate(fps);
    recorder.setQuality(QMediaRecorder::HighQuality);
    recorder.setAutoStop(true);
    const QString name = QVideoFrameFormat::pixelFormatToString(pixel);
    const QString path = QDir(directory).filePath(name + ".mp4");
    QFile::remove(path);
    recorder.setOutputLocation(QUrl::fromLocalFile(path));
    int sent = 0;
    bool eos = false;
    QElapsedTimer clock;
    auto pump = [&] {
      while (sent < count) {
        QVideoFrame frame = frames[sent % frames.size()];
        frame.setStartTime(qint64(sent) * 1000000 / fps);
        frame.setEndTime(qint64(sent + 1) * 1000000 / fps);
        if (!input.sendVideoFrame(frame)) return;
        ++sent;
      }
      if (!eos) eos = input.sendVideoFrame(QVideoFrame());
    };
    QObject::connect(&input, &QVideoFrameInput::readyToSendVideoFrame, &app, pump);
    QObject::connect(&recorder, &QMediaRecorder::recorderStateChanged, &app, [&](QMediaRecorder::RecorderState state) {
      if (state == QMediaRecorder::StoppedState) app.exit(0);
      else pump();
    });
    QObject::connect(&recorder, &QMediaRecorder::errorOccurred, &app, [&](QMediaRecorder::Error, const QString &message) {
      report[name] = QJsonObject{{"error", message}};
      app.exit(1);
    });
    QTimer::singleShot(180000, &app, [&] { app.exit(3); });
    clock.start();
    recorder.record();
    pump();
    const int rc = app.exec();
    if (rc == 0) {
      const double seconds = clock.nsecsElapsed() / 1e9;
      report[name] = QJsonObject{{"seconds", seconds}, {"fps", sent / seconds}, {"frames", sent}};
    }
  }
  std::fprintf(stdout, "%s\n", QJsonDocument(report).toJson(QJsonDocument::Compact).constData());
  return 0;
}
