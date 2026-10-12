#include "ExportController.h"
#include "AutoFraming.h"
#include "CanvasRenderer.h"
#include <QAudioBufferInput>
#include <QAudioBufferOutput>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QQueue>
#include <QTimer>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoFrameInput>
#include <QVideoSink>
#include <algorithm>
#include <cmath>

struct ExportController::Private {
  struct Clip {
    qint64 start, end, output;
  };
  ExportController *q;
  QObject *run = nullptr;
  QMediaPlayer *player = nullptr;
  QMediaRecorder *recorder = nullptr;
  QMediaCaptureSession *session = nullptr;
  QVideoFrameInput *video = nullptr;
  QAudioBufferInput *audio = nullptr;
  QQueue<QVideoFrame> frames;
  QQueue<QAudioBuffer> buffers;
  QList<Clip> clips;
  QVariantList framing;
  QVariantMap canvas;
  QAudioFormat audioFormat;
  QSize size;
  QString temporary, target, status, error;
  QUrl last;
  bool busy = false, loaded = false, hasAudio = false, ended = false;
  bool started = false, videoEos = false, audioEos = false, sawAudio = false;
  bool pumping = false, paused = false;
  qint64 total = 0, sentVideo = 0, sentAudio = 0;
  quint64 generation = 0;
  double progress = 0;
  QElapsedTimer activity;

  explicit Private(ExportController *owner) : q(owner) {}
  void touch() { activity.restart(); }
  void clear() {
    if (run) {
      // Disconnect before stop: stopping can synchronously emit state changes.
      const auto children = run->findChildren<QObject *>();
      for (QObject *child : children)
        QObject::disconnect(child, nullptr, q, nullptr);
      if (player)
        player->stop();
      if (recorder)
        recorder->stop();
      run->deleteLater();
    }
    run = nullptr;
    player = nullptr;
    recorder = nullptr;
    session = nullptr;
    video = nullptr;
    audio = nullptr;
    frames.clear();
    buffers.clear();
  }
  void fail(const QString &message) {
    if (!busy)
      return;
    error = message;
    status = QStringLiteral("导出失败");
    busy = false;
    const QString cleanup = temporary;
    clear();
    // Release the recorder's file handle before removing this run's private
    // file.
    QTimer::singleShot(0, q, [cleanup] {
      if (!cleanup.isEmpty())
        QFile::remove(cleanup);
    });
    emit q->changed();
  }
  void finish() {
    if (!busy || !ended || !videoEos || (hasAudio && !audioEos))
      return;
    if (!sentVideo || (hasAudio && !sentAudio)) {
      fail(QStringLiteral("素材未产生可导出的视频或音频，已停止导出。"));
      return;
    }
    const QString from = temporary;
    const QString to = target;
    const quint64 completedGeneration = generation;
    clear();
    QTimer::singleShot(0, q, [this, from, to, completedGeneration] {
      if (!busy || generation != completedGeneration) {
        QFile::remove(from);
        return;
      }
      if (!QFileInfo(from).isFile() || QFileInfo(from).size() == 0 ||
          QFileInfo::exists(to) || !QFile::rename(from, to)) {
        fail(
            QStringLiteral("无法发布导出文件（目标可能已存在或文件不可写）。"));
        return;
      }
      temporary.clear();
      last = QUrl::fromLocalFile(to);
      progress = 1;
      status = QStringLiteral("导出完成");
      busy = false;
      emit q->changed();
    });
  }
  void initialize() {
    if (!busy || started || !loaded || frames.isEmpty() ||
        (hasAudio && !audioFormat.isValid()))
      return;
    session = new QMediaCaptureSession(run);
    recorder = new QMediaRecorder(run);
    video = new QVideoFrameInput(frames.head().surfaceFormat(), run);
    session->setRecorder(recorder);
    session->setVideoFrameInput(video);
    if (hasAudio) {
      audio = new QAudioBufferInput(audioFormat, run);
      session->setAudioBufferInput(audio);
      QObject::connect(audio, &QAudioBufferInput::readyToSendAudioBuffer, q,
                       [this] { pump(); });
    }
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    if (hasAudio)
      format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    if (!format.isSupported(QMediaFormat::Encode)) {
      fail(QStringLiteral("当前 Qt 媒体后端不支持 MP4 / H.264 / AAC 导出。"));
      return;
    }
    recorder->setMediaFormat(format);
    recorder->setVideoResolution(size);
    recorder->setVideoFrameRate(30);
    recorder->setQuality(QMediaRecorder::HighQuality);
    recorder->setAutoStop(true);
    recorder->setOutputLocation(QUrl::fromLocalFile(temporary));
    QObject::connect(video, &QVideoFrameInput::readyToSendVideoFrame, q,
                     [this] { pump(); });
    QObject::connect(recorder, &QMediaRecorder::errorOccurred, q,
                     [this](QMediaRecorder::Error, const QString &message) {
                       fail(message);
                     });
    QObject::connect(
        recorder, &QMediaRecorder::recorderStateChanged, q,
        [this](QMediaRecorder::RecorderState state) {
          if (!busy)
            return;
          touch();
          if (state == QMediaRecorder::StoppedState && started) {
            if (ended && videoEos && (!hasAudio || audioEos))
              finish();
            else
              fail(QStringLiteral("编码器在输入结束前停止。"));
          } else
            pump();
        },
        Qt::QueuedConnection);
    started = true;
    status = QStringLiteral("正在导出 MP4…");
    emit q->changed();
    recorder->record();
    pump();
  }
  void pump() {
    if (!busy || !started || pumping || !recorder)
      return;
    pumping = true;
    while (!frames.isEmpty() && video->sendVideoFrame(frames.head())) {
      frames.dequeue();
      ++sentVideo;
      touch();
    }
    while (audio && !buffers.isEmpty() &&
           audio->sendAudioBuffer(buffers.head())) {
      buffers.dequeue();
      ++sentAudio;
      touch();
    }
    if (ended && frames.isEmpty() && !videoEos)
      videoEos = video->sendVideoFrame(QVideoFrame());
    if (ended && audio && buffers.isEmpty() && !audioEos)
      audioEos = audio->sendAudioBuffer(QAudioBuffer());
    pumping = false;
    if (!busy || !player)
      return;
    if (!ended && (frames.size() >= 24 || buffers.size() >= 96)) {
      if (!paused) {
        paused = true;
        player->pause();
      }
    } else if (!ended && paused && frames.size() < 12 && buffers.size() < 48) {
      paused = false;
      player->play();
    }
    // autoStop can emit StoppedState synchronously during the EOS send.
    if (ended && videoEos && (!hasAudio || audioEos) &&
        recorder->recorderState() == QMediaRecorder::StoppedState)
      finish();
  }
  void receiveVideo(const QVideoFrame &source) {
    if (!busy || !source.isValid())
      return;
    touch();
    const qint64 timestamp = source.startTime();
    if (timestamp < 0) {
      fail(QStringLiteral("视频帧缺少时间戳，无法准确剪辑。"));
      return;
    }
    for (const Clip &clip : clips) {
      if (timestamp < clip.start || timestamp >= clip.end)
        continue;
      QImage image = source.toImage();
      if (image.isNull()) {
        fail(QStringLiteral("视频帧无法转换为可编码图像。"));
        return;
      }
      if (size.isEmpty()) {
        size = image.size();
        size.scale(QSize(1920, 1080), Qt::KeepAspectRatio);
        if (image.width() <= 1920 && image.height() <= 1080)
          size = image.size();
        if (!canvas.isEmpty())
          size = CanvasRenderer::outputSize(image.size(), canvas);
        size.setWidth(std::max(2, size.width() & ~1));
        size.setHeight(std::max(2, size.height() & ~1));
      }
      image = CanvasRenderer::render(
          image, canvas,
          AutoFraming::evaluate(framing, double(timestamp) / 1000000.0));
      if (image.isNull()) {
        fail(QStringLiteral("画布渲染失败，请检查背景图片是否仍可读取。"));
        return;
      }
      image =
          image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_RGBA8888)
              .copy();
      QVideoFrame output(image);
      const qint64 mapped = clip.output + timestamp - clip.start;
      output.setStartTime(mapped);
      output.setEndTime(clip.output +
                        std::min(clip.end, source.endTime() > timestamp
                                               ? source.endTime()
                                               : timestamp + 33333) -
                        clip.start);
      frames.enqueue(output);
      if (frames.size() > 120) {
        fail(QStringLiteral("编码速度不足，视频缓冲已达上限。"));
        return;
      }
      progress = std::clamp(double(mapped) / double(total), 0.0, 0.99);
      emit q->changed();
      break;
    }
    initialize();
    pump();
  }
  void receiveAudio(const QAudioBuffer &source) {
    if (!busy || !source.isValid())
      return;
    touch();
    sawAudio = true;
    audioFormat = source.format();
    if (source.startTime() < 0 || !audioFormat.isValid()) {
      fail(QStringLiteral("音频缺少有效格式或时间戳。"));
      return;
    }
    const qint64 start = source.startTime(), end = start + source.duration();
    for (const Clip &clip : clips) {
      const qint64 begin = std::max(start, clip.start),
                   finish = std::min(end, clip.end);
      if (finish <= begin)
        continue;
      const int first = int(std::clamp<qint64>(
          (begin - start) * audioFormat.sampleRate() / 1000000, 0,
          source.frameCount()));
      const int lastFrame = int(std::clamp<qint64>(
          (finish - start) * audioFormat.sampleRate() / 1000000, first,
          source.frameCount()));
      if (lastFrame == first)
        continue;
      const int stride = audioFormat.bytesPerFrame();
      QByteArray data(source.constData<char>() + first * stride,
                      (lastFrame - first) * stride);
      buffers.enqueue(QAudioBuffer(
          data, audioFormat,
          clip.output + start +
              qint64(first) * 1000000 / audioFormat.sampleRate() - clip.start));
    }
    if (buffers.size() > 512) {
      fail(QStringLiteral("编码速度不足，音频缓冲已达上限。"));
      return;
    }
    initialize();
    pump();
  }
};

ExportController::ExportController(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this)) {}
ExportController::~ExportController() {
  d->clear();
  if (!d->temporary.isEmpty())
    QFile::remove(d->temporary);
}
bool ExportController::busy() const { return d->busy; }
double ExportController::progress() const { return d->progress; }
QString ExportController::status() const { return d->status; }
QString ExportController::error() const { return d->error; }
QUrl ExportController::lastExport() const { return d->last; }

void ExportController::start(QUrl source, QVariantList clips, QUrl destination,
                             QVariantList framing, QVariantMap canvas) {
  if (d->busy)
    return;
  d->error.clear();
  d->progress = 0;
  d->clips.clear();
  d->framing = std::move(framing);
  d->canvas = std::move(canvas);
  d->total = 0;
  auto reject = [this](const QString &message) {
    d->error = message;
    d->status = QStringLiteral("导出失败");
    emit changed();
  };
  QString canvasError;
  if (!d->canvas.isEmpty() && !CanvasRenderer::validate(d->canvas, &canvasError)) {
    reject(canvasError);
    return;
  }
  if (!source.isLocalFile() || !QFileInfo(source.toLocalFile()).isFile() ||
      !destination.isLocalFile()) {
    reject(QStringLiteral("请选择有效的本地素材和导出路径。"));
    return;
  }
  d->target = QFileInfo(destination.toLocalFile()).absoluteFilePath();
  if (QFileInfo::exists(d->target) ||
      QFileInfo(source.toLocalFile())
              .canonicalFilePath()
              .compare(d->target, Qt::CaseInsensitive) == 0) {
    reject(QStringLiteral("导出目标已存在；请选择新的 MP4 文件名。"));
    return;
  }
  if (!d->target.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive) ||
      !QFileInfo(d->target).dir().exists()) {
    reject(QStringLiteral("导出路径必须是现有文件夹中的 .mp4 文件。"));
    return;
  }
  qint64 previousEnd = 0;
  for (const QVariant &value : clips) {
    const QVariantMap map = value.toMap();
    const double start = map.value(QStringLiteral("sourceStart")).toDouble();
    const double duration =
        map.value(QStringLiteral("sourceDuration")).toDouble();
    const double rate =
        map.value(QStringLiteral("playbackRate"), 1.0).toDouble();
    if (!std::isfinite(start) || !std::isfinite(duration) || start < 0 ||
        duration <= 0 || duration > 86400 || start > 86400 ||
        !std::isfinite(rate) || std::abs(rate - 1.0) > 0.000001) {
      reject(QStringLiteral("本轮仅支持有效的 1× 裁切片段。"));
      return;
    }
    const qint64 begin = qRound64(start * 1000000),
                 length = qRound64(duration * 1000000);
    if (begin < previousEnd || length <= 0) {
      reject(QStringLiteral("片段必须按原素材顺序排列且不能重叠。"));
      return;
    }
    d->clips.append({begin, begin + length, d->total});
    d->total += length;
    previousEnd = begin + length;
  }
  if (d->clips.isEmpty()) {
    reject(QStringLiteral("没有可导出的片段。"));
    return;
  }
  d->temporary = QFileInfo(d->target).dir().filePath(
      QStringLiteral(".screenpilot-export-%1.mp4")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  ++d->generation;
  d->busy = true;
  d->loaded = false;
  d->hasAudio = false;
  d->ended = false;
  d->started = false;
  d->videoEos = false;
  d->audioEos = false;
  d->paused = false;
  d->pumping = false;
  d->sawAudio = false;
  d->sentAudio = d->sentVideo = 0;
  d->audioFormat = {};
  d->size = {};
  d->run = new QObject(this);
  d->player = new QMediaPlayer(d->run);
  auto *sink = new QVideoSink(d->run);
  auto *output = new QAudioBufferOutput(d->run);
  d->player->setVideoSink(sink);
  d->player->setAudioBufferOutput(output);
  connect(sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) { d->receiveVideo(frame); });
  connect(output, &QAudioBufferOutput::audioBufferReceived, this,
          [this](const QAudioBuffer &buffer) { d->receiveAudio(buffer); });
  connect(d->player, &QMediaPlayer::errorOccurred, this,
          [this](QMediaPlayer::Error, const QString &message) {
            d->fail(message);
          });
  connect(d->player, &QMediaPlayer::mediaStatusChanged, this,
          [this](QMediaPlayer::MediaStatus status) {
            if (!d->busy)
              return;
            d->touch();
            if (status == QMediaPlayer::LoadedMedia && !d->loaded) {
              d->loaded = true;
              d->hasAudio = d->player->hasAudio();
              if (!d->player->hasVideo()) {
                d->fail(QStringLiteral("素材没有可解码的视频轨。"));
                return;
              }
              if (d->player->duration() > 0 &&
                  d->clips.last().end > d->player->duration() * 1000 + 100000) {
                d->fail(QStringLiteral("剪辑片段超出原素材时长。"));
                return;
              }
              d->player->play();
            } else if (status == QMediaPlayer::EndOfMedia) {
              d->ended = true;
              if (!d->started) {
                d->fail(QStringLiteral("未获得可编码的视频或音频数据。"));
                return;
              }
              if (d->hasAudio && !d->sawAudio) {
                d->fail(QStringLiteral("原素材有音轨，但当前后端未解码音频。"));
                return;
              }
              d->status = QStringLiteral("正在完成 MP4 文件…");
              emit changed();
              d->pump();
            } else if (status == QMediaPlayer::InvalidMedia)
              d->fail(QStringLiteral("当前媒体后端无法解码素材。"));
          });
  auto *watchdog = new QTimer(d->run);
  watchdog->setInterval(1000);
  connect(watchdog, &QTimer::timeout, this, [this] {
    if (d->busy && d->activity.elapsed() > 45000)
      d->fail(QStringLiteral("媒体处理超过 45 秒没有进展，已停止导出。"));
  });
  d->touch();
  watchdog->start();
  d->status = QStringLiteral("正在读取素材…");
  emit changed();
  d->player->setSource(source);
}

void ExportController::cancel() {
  if (!d->busy)
    return;
  d->busy = false;
  d->status = QStringLiteral("导出已取消");
  d->error.clear();
  const QString cleanup = d->temporary;
  d->clear();
  d->temporary.clear();
  QTimer::singleShot(0, this, [cleanup] { QFile::remove(cleanup); });
  emit changed();
}
