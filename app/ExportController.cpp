#include "ExportController.h"
#include "FrameConversion.h"
#include "BackgroundVideo.h"
#include "AutoFraming.h"
#include "AudioTimeStretch.h"
#include "AudioMix.h"
#include "CursorEffects.h"
#include "CameraOverlay.h"
#include "CameraMotion.h"
#include "ProjectController.h"
#include "CanvasRenderer.h"
#include <QAudioBufferInput>
#include <QAudioBufferOutput>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QPainter>
#include <QQueue>
#include <QTimer>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoFrameInput>
#include <QVideoSink>
#include <QMutex>
#include <QThreadPool>
#include <map>
#include <algorithm>
#include <cmath>

struct ExportController::Private {
  struct Clip {
    qint64 start, end, output;
    double rate;
  };
  ExportController *q;
  QObject *run = nullptr;
  QMediaPlayer *player = nullptr;
  QObject *decoder = nullptr;
  QUrl sourceUrl, cameraUrl;
  QVariantMap cameraSettings;
  QMediaPlayer *cameraPlayer = nullptr;
  QQueue<QVideoFrame> cameraFrames, pendingScreen;
  QImage cameraImage;
  BackgroundVideo *background = nullptr;
  // One decoded frame, converted once by whichever render worker needs it
  // first. A sparse frame can cover several output ticks rendered in parallel.
  struct SourceFrame {
    QMutex mutex;
    QVideoFrame frame;
    QImage image;
    QImage get() {
      QMutexLocker lock(&mutex);
      if (image.isNull() && frame.isValid()) {
        image = FrameConversion::toImage(frame, false);
        frame = {};
      }
      return image;
    }
  };
  struct RenderJob { std::shared_ptr<SourceFrame> source; QImage camera; qint64 sourceTime, tick, lastTick; };
  QQueue<RenderJob> renderJobs;
  bool renderScheduled = false, backgroundPending = false;
  // Output ticks are composed concurrently on renderPool and committed to the
  // encoder queue strictly in sequence order.
  QThreadPool renderPool;
  int renderInFlight = 0;
  qint64 nextRenderSequence = 0, nextCommitSequence = 0;
  std::map<qint64, QVideoFrame> renderedFrames;
  int pendingRenderTicks() const {
    return renderInFlight + int(renderedFrames.size()) + int(backgroundPending);
  }

  qint64 cameraImageTime = -1, cameraOffset = 0;
  bool cameraEnded = false, cameraLoaded = false, cameraPaused = false;
  int clipIndex = 0;
  bool clipVideoDone = false, clipAudioDone = false, advancing = false;
  bool audioOnly = false, muted = false;
  qint64 audioFrame = 0, lastVideoTime = -1;
  QMediaRecorder *recorder = nullptr;
  QMediaCaptureSession *session = nullptr;
  QVideoFrameInput *video = nullptr;
  QAudioBufferInput *audio = nullptr;
  QQueue<QVideoFrame> frames;
  QQueue<QAudioBuffer> buffers;
  QList<Clip> clips;
  QVariantList framing, pointerSamples;
  AudioTimeStretch stretch;
  AudioMix mix;
  qint64 mixFrame = 0;
  bool stretchReady = false;
  QVariantMap canvas;
  QAudioFormat audioFormat;
  QSize size;
  int fps = 60;
  QString resolution = "source";
  QStringList copySources, copyTargets;
  int copyIndex = 0;
  qint64 copyTotal = 0, copyDone = 0;
  QFile copyInput, copyOutput;
  QString temporary, target, status, error;
  QUrl last;
  bool busy = false, loaded = false, hasAudio = false, ended = false;
  bool cancelling = false;
  bool started = false, videoEos = false, audioEos = false, sawAudio = false;
  bool pumping = false, paused = false;
  qint64 total = 0, sentVideo = 0, sentAudio = 0;
  quint64 generation = 0;
  double progress = 0;
  QElapsedTimer activity;
  bool measure = false;
  QElapsedTimer measurementClock;
  QVariantMap metrics;
  void count(const QString &key, qint64 amount = 1) {
    if (measure) metrics[key] = metrics.value(key).toLongLong() + amount;
  }
  void peak(const QString &key, qint64 value) {
    if (measure) metrics[key] = std::max(metrics.value(key).toLongLong(), value);
  }
  void queueMetrics() {
    peak("videoQueuePeak", frames.size());
    peak("renderQueuePeak", renderJobs.size());
    peak("combinedVideoQueuePeak", frames.size() + renderJobs.size() + pendingRenderTicks());
    peak("renderInFlightPeak", renderInFlight);
    peak("audioQueuePeak", buffers.size());
    peak("pendingScreenQueuePeak", pendingScreen.size());
  }
  int queuedVideoTicks() const {
    // Only the hysteresis thresholds need the count. Saturate while summing
    // compact spans so a long sparse frame cannot overflow or make this scan
    // proportional to the number of output ticks.
    int ticks = std::min<int>(24, frames.size() + pendingRenderTicks());
    for (const RenderJob &job : renderJobs) {
      const qint64 remaining = job.lastTick - job.tick + 1;
      if (remaining >= 24 - ticks) return 24;
      ticks += int(remaining);
    }
    return ticks;
  }

  explicit Private(ExportController *owner) : q(owner) {
    // Each worker holds one output-sized image; the queue bound below caps
    // total memory as before. A few workers outpace the encoder; more only
    // compete with a software encoder for cores.
    // Workers convert single-threaded (frames are the unit of parallelism) at
    // low priority so the GUI and encoder threads keep their CPU time.
    renderPool.setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 4, 2, 4));
    renderPool.setThreadPriority(QThread::LowPriority);
  }
  void touch() { activity.restart(); }
  void clear() {
    copyInput.close();
    copyOutput.close();
    mix.close();
    if (run) {
      // Disconnect before stop: stopping can synchronously emit state changes.
      const auto children = run->findChildren<QObject *>();
      for (QObject *child : children)
        QObject::disconnect(child, nullptr, q, nullptr);
      if (player)
        player->stop();
      if (cameraPlayer)
        cameraPlayer->stop();
      if (recorder)
        recorder->stop();
      run->deleteLater();
    }
    background = nullptr; renderJobs.clear(); renderScheduled = false; backgroundPending = false;
    // Workers own copies of their inputs; stale results are dropped by the
    // generation check when they arrive.
    renderInFlight = 0; renderedFrames.clear(); nextRenderSequence = nextCommitSequence = 0;
    run = nullptr;
    player = nullptr;
    decoder = nullptr;
    cameraPlayer = nullptr;
    pendingScreen.clear();
    cameraFrames.clear();
    cameraImage = {};
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
    if (measure) {
      queueMetrics();
      metrics["failure"] = message;
      metrics["wallTimeMs"] = measurementClock.elapsed();
    }
    status = QStringLiteral("导出失败");
    busy = false;
    const QString cleanup = temporary;
    clear();
    // Release the recorder's file handle before removing this run's private
    // file.
    QTimer::singleShot(0, q, [cleanup] {
      if (!cleanup.isEmpty())
        QFileInfo(cleanup).isDir() ? QDir(cleanup).removeRecursively() : QFile::remove(cleanup);
    });
    emit q->changed();
  }
  void finish() {
    if (!busy || !ended || (!audioOnly && !videoEos) || (hasAudio && !audioEos))
      return;
    if ((!audioOnly && !sentVideo) || (hasAudio && !sentAudio)) {
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
      if (measure) metrics["wallTimeMs"] = measurementClock.elapsed();
      busy = false;
      emit q->changed();
    });
  }
  void initialize() {
    if (!busy || started || !loaded || (!audioOnly && frames.isEmpty()) ||
        (hasAudio && !audioFormat.isValid()))
      return;
    session = new QMediaCaptureSession(run);
    recorder = new QMediaRecorder(run);
    if (!audioOnly)
      video = new QVideoFrameInput(frames.head().surfaceFormat(), run);
    session->setRecorder(recorder);
    if (video)
      session->setVideoFrameInput(video);
    if (hasAudio) {
      // Qt 6.8.3 needs the pending-format path to prime encoder readiness.
      // The first queued decoded buffer supplies the actual PCM format.
      audio = new QAudioBufferInput(run);
      session->setAudioBufferInput(audio);
      QObject::connect(audio, &QAudioBufferInput::readyToSendAudioBuffer, q,
                       [this] { pump(); });
    }
    QMediaFormat format(audioOnly ? QMediaFormat::Mpeg4Audio
                                  : QMediaFormat::MPEG4);
    if (!audioOnly)
      format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    if (hasAudio)
      format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    if (!format.isSupported(QMediaFormat::Encode)) {
      fail(QStringLiteral("当前 Qt 媒体后端不支持 MP4 / H.264 / AAC 导出。"));
      return;
    }
    recorder->setMediaFormat(format);
    if (!audioOnly) {
      recorder->setVideoResolution(size);
      recorder->setVideoFrameRate(fps);
    }
    recorder->setQuality(QMediaRecorder::HighQuality);
    recorder->setAutoStop(true);
    recorder->setOutputLocation(QUrl::fromLocalFile(temporary));
    if (video)
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
            if (ended && (audioOnly || videoEos) && (!hasAudio || audioEos))
              finish();
            else
              fail(QStringLiteral("编码器在输入结束前停止。"));
          } else
            pump();
        },
        Qt::QueuedConnection);
    started = true;
    status = audioOnly ? QStringLiteral("正在导出 M4A…")
                       : QStringLiteral("正在导出 MP4…");
    emit q->changed();
    recorder->record();
    pump();
  }
  void pump() {
    if (!busy || !started || pumping || !recorder)
      return;
    pumping = true;
    while (video && !frames.isEmpty() && video->sendVideoFrame(frames.head())) {
      frames.dequeue();
      ++sentVideo;
      count("sentVideoFrames");
      touch();
    }
    while (audio && !buffers.isEmpty() &&
           audio->sendAudioBuffer(buffers.head())) {
      buffers.dequeue();
      ++sentAudio;
      count("sentAudioBuffers");
      touch();
    }
    if (ended && video && frames.isEmpty() && !videoEos)
      videoEos = video->sendVideoFrame(QVideoFrame());
    if (ended && audio && buffers.isEmpty() && !audioEos)
      audioEos = audio->sendAudioBuffer(QAudioBuffer());
    pumping = false;
    queueMetrics();
    scheduleRender();
    if (!busy || !player)
      return;
    if (advancing)
      return;
    const int queuedTicks = queuedVideoTicks();
    if (!ended && (queuedTicks >= 24 || buffers.size() >= 96 ||
                   pendingScreen.size() >= 24)) {
      if (!paused) {
        paused = true;
        count("decoderPauseCount");
        player->pause();
      }
    } else if (!ended && paused && queuedTicks < 12 && buffers.size() < 48 &&
               pendingScreen.size() < 12) {
      paused = false;
      player->play();
    }
    // autoStop can emit StoppedState synchronously during the EOS send.
    if (ended && (audioOnly || videoEos) && (!hasAudio || audioEos) &&
        recorder->recorderState() == QMediaRecorder::StoppedState)
      finish();
  }
  bool cameraEnabled() const {
    return !audioOnly && !cameraUrl.isEmpty() &&
           cameraSettings.value(QStringLiteral("enabled"), true).toBool();
  }
  void drainCameraJoin() {
    while (busy && !pendingScreen.isEmpty()) {
      const qint64 desired = qRound64(CameraMotion::sourceToCameraTime(cameraSettings,
          pendingScreen.head().startTime() / 1000000.0) * 1000000);
      // Keep at most one preceding camera image and a bounded look-ahead queue.
      while (!cameraFrames.isEmpty() &&
             cameraFrames.head().startTime() <= desired) {
        cameraImageTime = cameraFrames.head().startTime();
        cameraImage = FrameConversion::toImage(cameraFrames.dequeue());
      }
      if (desired >= 0 && !cameraEnded && cameraFrames.isEmpty() &&
          cameraImageTime < desired)
        break;
      const QVideoFrame screen = pendingScreen.dequeue();
      renderVideo(screen);
    }
    if (cameraPlayer && cameraPaused && cameraFrames.size() < 12 &&
        !cameraEnded) {
      cameraPaused = false;
      cameraPlayer->play();
    }
    pump();
    nextClipIfReady();
  }
  void receiveVideo(const QVideoFrame &source) {
    if (!busy || ended || !source.isValid() || advancing)
      return;
    count("receivedVideoFrames");
    if (measure) {
      metrics["lastSourceStartUs"] = source.startTime();
      metrics["lastSourceEndUs"] = source.endTime();
      peak("sourceFrameSpanMaxUs", source.endTime() - source.startTime());
    }
    if (!cameraEnabled()) {
      renderVideo(source);
      return;
    }
    if (source.startTime() >= clips[clipIndex].end) {
      clipVideoDone = true;
      drainCameraJoin();
      return;
    }
    const qint64 end = source.endTime() > source.startTime()
                           ? source.endTime()
                           : source.startTime() + 33333;
    if (end <= clips[clipIndex].start)
      return;
    pendingScreen.enqueue(source);
    if (pendingScreen.size() > 120) {
      fail(QStringLiteral("摄像头同步缓冲已达上限。"));
      return;
    }
    drainCameraJoin();
  }
  void renderVideo(const QVideoFrame &source) {
    if (!busy || ended || advancing || !source.isValid())
      return;
    touch();
    const qint64 timestamp = source.startTime();
    if (timestamp < 0) {
      fail(QStringLiteral("视频帧缺少时间戳，无法准确剪辑。"));
      return;
    }
    const Clip &clip = clips[clipIndex];
    if (timestamp >= clip.end) {
      clipVideoDone = true;
      nextClipIfReady();
      return;
    }
    if (audioOnly || advancing)
      return;
    const qint64 frameEnd =
        source.endTime() > timestamp ? source.endTime() : timestamp + 33333;
    if (frameEnd <= clip.start)
      return;
    const qint64 mapped = clip.output + qRound64(
        double(std::max(timestamp, clip.start) - clip.start) / clip.rate);
    const qint64 mappedEnd = clip.output + qRound64(
        double(std::min(clip.end, frameEnd) - clip.start) / clip.rate);
    auto tickTime = [this](qint64 tick) { return qRound64(double(tick) * 1000000.0 / fps); };
    qint64 first = lastVideoTime < 0 ? 0 : qRound64(double(lastVideoTime) * fps / 1000000.0) + 1;
    first = std::max(first, qint64(std::floor(double(mapped) * fps / 1000000.0)));
    while (tickTime(first) < mapped) ++first;
    qint64 last = qint64(std::ceil(double(mappedEnd) * fps / 1000000.0));
    while (last >= first && tickTime(last) >= mappedEnd) --last;
    if (first > last) { count("skippedReadbackFrames"); return; }
    // CPU frames are converted later by a render worker. GPU surfaces (only
    // with an explicit hardware-decoding override) are read back now so the
    // decoder's small surface pool is never held by queued jobs.
    auto sourceFrame = std::make_shared<SourceFrame>();
    sourceFrame->frame = source;
    if (source.handleType() != QVideoFrame::NoHandle && sourceFrame->get().isNull()) {
      fail(QStringLiteral("视频帧无法转换为可编码图像。"));
      return;
    }
    if (size.isEmpty()) {
      QSize sourceSize = source.size();
      const auto rotation = source.surfaceFormat().rotation();
      if (rotation == QtVideo::Rotation::Clockwise90 || rotation == QtVideo::Rotation::Clockwise270)
        sourceSize.transpose();
      const QString ratio = canvas.value("ratio", "source").toString();
      size = resolution == "source" ? CanvasRenderer::nativeOutputSize(sourceSize, canvas)
          : canvas.isEmpty() ? sourceSize : ratio == "source" ? CanvasRenderer::croppedSize(sourceSize, canvas) : CanvasRenderer::outputSize(sourceSize, canvas);
      const int edge = resolution == "1080p" ? 1080 : resolution == "1440p" ? 1440 : resolution == "4k" ? 2160 : 0;
      if (edge) {
        const bool longEdge = ratio == "source" || ratio == "2.39:1" || ratio == "1:2.39";
        const double scale = longEdge ? double(edge * 16 / 9) / std::max(size.width(), size.height()) : double(edge) / std::min(size.width(), size.height());
        size = QSize(qRound(size.width() * scale / 2) * 2, qRound(size.height() * scale / 2) * 2);
      }
      size.setWidth(std::max(2, size.width() & ~1));
      size.setHeight(std::max(2, size.height() & ~1));
    }
    const qint64 cameraTime = qRound64(CameraMotion::sourceToCameraTime(cameraSettings, timestamp / 1000000.0) * 1000000);
    const bool validCamera = cameraEnabled() && cameraTime >= 0 &&
        !cameraImage.isNull() && cameraImageTime <= cameraTime &&
        (!cameraPlayer || cameraPlayer->duration() <= 0 || cameraTime < cameraPlayer->duration() * 1000);
    // Store one compact span per decoded frame. Expand its output ticks only as
    // encoder capacity becomes available, including long-duration sparse frames.
    renderJobs.enqueue({sourceFrame, validCamera ? cameraImage : QImage(), timestamp, first, last});
    lastVideoTime = tickTime(last);
    count("scheduledVideoFrames", last - first + 1);
    queueMetrics();
    progress = std::clamp(double(mapped) / double(total), 0.0, 0.99);
    emit q->changed();
    drainRenderJobs();
    pump();
  }
  void scheduleRender() {
    if (!busy || renderScheduled || renderJobs.isEmpty() || !canDispatchRender()) return;
    renderScheduled = true;
    const quint64 requestGeneration = generation;
    QTimer::singleShot(0, q, [this, requestGeneration] {
      if (!busy || generation != requestGeneration) return;
      renderScheduled = false;
      drainRenderJobs();
    });
  }
  bool canDispatchRender() const {
    return !backgroundPending && renderInFlight < renderPool.maxThreadCount() &&
           frames.size() + pendingRenderTicks() < 24;
  }
  // Takes output ticks in order. The background video decoder is sequential,
  // so its frame is fetched on this thread; composition then runs on a worker.
  void drainRenderJobs() {
    while (busy && !renderJobs.isEmpty() && canDispatchRender()) {
      RenderJob &head = renderJobs.head();
      const RenderJob job = head;
      if (++head.tick > head.lastTick) renderJobs.dequeue();
      const qint64 time = qRound64(double(job.tick) * 1000000.0 / fps);
      const qint64 end = std::min(total, qRound64(double(job.tick + 1) * 1000000.0 / fps));
      if (!background) {
        background = new BackgroundVideo(run);
        background->setPath(canvas.value("backgroundVideo").toString());
      }
      const qint64 sequence = nextRenderSequence++;
      const quint64 requestGeneration = generation;
      backgroundPending = true;
      // Completes synchronously without a background video.
      background->request(double(time) / 1000000.0,
          [this, job, time, end, sequence, requestGeneration](QImage backdrop, QString error) {
        if (!busy || generation != requestGeneration) return;
        backgroundPending = false;
        if (!error.isEmpty()) { fail(error); return; }
        dispatchRender(job, time, end, sequence, backdrop);
        drainRenderJobs();
      });
    }
  }
  void dispatchRender(const RenderJob &job, qint64 time, qint64 end, qint64 sequence, const QImage &backdrop) {
    ++renderInFlight;
    queueMetrics();
    struct Inputs {
      RenderJob job;
      qint64 time, end;
      QImage backdrop;
      QVariantMap canvas, cameraSettings;
      QVariantList framing, pointerSamples;
      QSize size;
      bool camera;
    };
    Inputs in{job, time, end, backdrop, canvas, cameraSettings, framing, pointerSamples, size,
              cameraEnabled()};
    const quint64 requestGeneration = generation;
    renderPool.start([this, in = std::move(in), sequence, requestGeneration] {
      QElapsedTimer clock;
      clock.start();
      const QImage source = in.job.source->get();
      const qint64 renderStart = clock.nsecsElapsed();
      const double sourceSeconds = double(in.job.sourceTime) / 1000000.0;
      QImage image = source.isNull() ? QImage() : CanvasRenderer::render(source, in.canvas,
          AutoFraming::evaluate(in.framing, sourceSeconds), {},
          CursorEffects::evaluate(in.pointerSamples, sourceSeconds),
          in.size, in.backdrop, double(in.time) / 1000000.0, sourceSeconds);
      const qint64 renderNs = clock.nsecsElapsed() - renderStart;
      QVideoFrame sampled;
      QString failure;
      if (source.isNull()) failure = QStringLiteral("视频帧无法转换为可编码图像。");
      else if (image.isNull()) failure = QStringLiteral("画布渲染失败，请检查背景媒体。");
      else {
        // The canvas is rendered at the export size; scale only legacy raw
        // (settings-less) outputs, then hand the encoder tagged BT.709 NV12.
        if (image.size() != in.size)
          image = image.scaled(in.size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (image.format() != QImage::Format_ARGB32_Premultiplied)
          image.convertTo(QImage::Format_ARGB32_Premultiplied);
        if (in.camera && CameraMotion::sourceToCameraTime(in.cameraSettings, sourceSeconds) >= 0 && !in.job.camera.isNull()) {
          QPainter painter(&image);
          CameraOverlay::paint(painter, image.size(), in.job.camera,
                               CameraMotion::evaluate(in.cameraSettings, sourceSeconds),
                               in.canvas, double(in.time) / 1000000.0);
        }
        sampled = FrameConversion::toNV12(image, false);
        sampled.setStartTime(in.time);
        sampled.setEndTime(in.end);
      }
      const qint64 totalNs = clock.nsecsElapsed();
      // q outlives the pool: ~ExportController destroys Private (waiting for
      // this pool) before QObject teardown discards undelivered events.
      QMetaObject::invokeMethod(q, [this, sampled, failure, sequence, requestGeneration,
                                    sourceNs = renderStart, renderNs, totalNs] {
        if (!busy || generation != requestGeneration) return;
        --renderInFlight;
        if (!failure.isEmpty()) { fail(failure); return; }
        count("renderCount"); count("renderTotalNs", renderNs); peak("renderMaxNs", renderNs);
        count("workerTotalNs", totalNs);
        count("sourceAccessTotalNs", sourceNs);
        renderedFrames.emplace(sequence, sampled);
        while (!renderedFrames.empty() && renderedFrames.begin()->first == nextCommitSequence) {
          frames.enqueue(renderedFrames.begin()->second);
          renderedFrames.erase(renderedFrames.begin());
          ++nextCommitSequence;
        }
        touch(); initialize(); pump(); nextClipIfReady();
        drainRenderJobs();
      }, Qt::QueuedConnection);
    });
  }
  void receiveAudio(const QAudioBuffer &source) {
    if (!busy || ended || advancing || !hasAudio || !source.isValid() ||
        clipAudioDone)
      return;
    touch();
    sawAudio = true;
    if (!stretchReady)
      audioFormat = source.format();
    if (source.startTime() < 0 || !audioFormat.isValid()) {
      fail(QStringLiteral("音频缺少有效格式或时间戳。"));
      return;
    }
    const qint64 start = source.startTime(), end = start + source.duration();
    const Clip &clip = clips[clipIndex];
    const qint64 begin = std::max(start, clip.start),
                 finish = std::min(end, clip.end);
    if (finish > begin) {
      const QAudioFormat inputFormat = source.format();
      const int sampleRate = inputFormat.sampleRate();
      if (!stretchReady) {
        stretch.reset(inputFormat, clip.rate);
        stretchReady = true;
        audioFormat = stretch.outputFormat();
      }
      const int first = std::clamp(int(std::ceil(double(begin - start) * sampleRate / 1000000.0)), 0, int(source.frameCount()));
      const int last = std::clamp(int(std::ceil(double(finish - start) * sampleRate / 1000000.0)), first, int(source.frameCount()));
      const QByteArray trimmed(source.constData<char>() + first * inputFormat.bytesPerFrame(),
                               (last - first) * inputFormat.bytesPerFrame());
      enqueueStretched(stretch.process(QAudioBuffer(trimmed, inputFormat)));
      progress =
          std::clamp(double(clip.output +
                            qRound64(double(finish - clip.start) / clip.rate)) /
                         double(total),
                     0.0, 0.99);
    }
    if (end >= clip.end) {
      flushAudio();
      clipAudioDone = true;
    }
    if (buffers.size() > 512) {
      fail(QStringLiteral("编码速度不足，音频缓冲已达上限。"));
      return;
    }
    initialize();
    pump();
    nextClipIfReady();
  }

  void enqueueStretched(const QByteArray &data) {
    if (data.isEmpty()) return;
    buffers.enqueue(QAudioBuffer(data, audioFormat,
                    clips[clipIndex].output + audioFrame * 1000000 / audioFormat.sampleRate()));
    audioFrame += data.size() / audioFormat.bytesPerFrame();
  }
  void flushAudio() {
    if (!stretchReady || clipAudioDone) return;
    const Clip &clip = clips[clipIndex];
    enqueueStretched(stretch.finish(qRound64(double(clip.end - clip.start) *
        audioFormat.sampleRate() / (1000000.0 * clip.rate))));
  }

  void nextClipIfReady() {
    if (!busy || ended || advancing || !clipVideoDone || !clipAudioDone ||
        !pendingScreen.isEmpty() || !renderJobs.isEmpty() || pendingRenderTicks())
      return;
    advancing = true;
    player->pause();
    if (cameraPlayer)
      cameraPlayer->pause();
    const quint64 clipGeneration = generation;
    QTimer::singleShot(0, q, [this, clipGeneration] {
      if (!busy || clipGeneration != generation)
        return;
      if (++clipIndex < clips.size()) {
        openClip();
      } else {
        ended = true;
        advancing = false;
        if (!started) {
          fail(QStringLiteral("未获得可编码的视频或音频数据。"));
          return;
        }
        status = QStringLiteral("正在完成导出文件…");
        emit q->changed();
        pump();
      }
    });
  }

  void openClip() {
    if (decoder) {
      for (QObject *child : decoder->findChildren<QObject *>())
        QObject::disconnect(child, nullptr, q, nullptr);
      player->stop();
      if (cameraPlayer)
        cameraPlayer->stop();
      decoder->deleteLater();
    }
    cameraPlayer = nullptr;
    cameraFrames.clear();
    pendingScreen.clear();
    cameraImage = {};
    cameraImageTime = -1;
    cameraEnded = cameraLoaded = !cameraEnabled();
    cameraPaused = false;
    decoder = new QObject(run);
    player = new QMediaPlayer(decoder);
    auto *sink = new QVideoSink(decoder);
    auto *output = new QAudioBufferOutput(decoder);
    player->setVideoSink(sink);
    player->setAudioBufferOutput(output);
    loaded = false;
    advancing = false;
    paused = false;
    audioFrame = 0;
    stretchReady = false;
    clipVideoDone = audioOnly;
    clipAudioDone = muted;
    QObject::connect(sink, &QVideoSink::videoFrameChanged, q,
                     [this](const QVideoFrame &frame) { receiveVideo(frame); });
    QObject::connect(
        output, &QAudioBufferOutput::audioBufferReceived, q,
        [this](const QAudioBuffer &buffer) {
          if (!mix.hasSources()) receiveAudio(buffer);
        });
    // Sidecars have their own source clock: decoding video need not expose an
    // embedded audio stream. Keep PCM reads bounded and encoder-backpressured.
    auto *mixTimer = new QTimer(decoder);
    mixTimer->setInterval(0);
    QObject::connect(mixTimer, &QTimer::timeout, q, [this] {
      if (!busy || !loaded || advancing || clipAudioDone || !mix.hasSources() ||
          buffers.size() >= 48) return;
      const qint64 endFrame = qRound64(double(clips[clipIndex].end) * 48000 / 1000000);
      const qint64 count = std::min<qint64>(960, endFrame - mixFrame);
      if (count <= 0) {
        flushAudio(); clipAudioDone = true; initialize(); pump(); nextClipIfReady(); return;
      }
      QString error;
      const QByteArray pcm = mix.readFrames(mixFrame, count, &error);
      if (!error.isEmpty()) { fail(error); return; }
      const qint64 timestamp = mixFrame * 1000000 / 48000;
      mixFrame += count;
      receiveAudio(QAudioBuffer(pcm, AudioMix::format(), timestamp));
    });
    if (mix.hasSources() && !muted) mixTimer->start();
    QObject::connect(
        player, &QMediaPlayer::errorOccurred, q,
        [this](QMediaPlayer::Error, const QString &message) { fail(message); });
    QObject::connect(
        player, &QMediaPlayer::mediaStatusChanged, q,
        [this](QMediaPlayer::MediaStatus mediaStatus) {
          if (!busy)
            return;
          touch();
          if (mediaStatus == QMediaPlayer::LoadedMedia && !loaded) {
            loaded = true;
            hasAudio = !muted && (mix.hasSources() || player->hasAudio());
            clipAudioDone = !hasAudio;
            if (mix.hasSources() && hasAudio) {
              audioFormat = AudioMix::format();
              mixFrame = qRound64(double(clips[clipIndex].start) * 48000 / 1000000);
            }
            if ((!audioOnly && !player->hasVideo()) ||
                (audioOnly && !hasAudio)) {
              fail(QStringLiteral("素材缺少所选导出模式需要的轨道。"));
              return;
            }
            for (const Clip &clip : clips) {
              if (player->duration() > 0 &&
                  clip.end > player->duration() * 1000 + 100000) {
                fail(QStringLiteral("剪辑片段超出原素材时长。"));
                return;
              }
            }
            player->setPosition(clips[clipIndex].start / 1000);
            if (cameraLoaded)
              player->play();
          } else if (mediaStatus == QMediaPlayer::EndOfMedia) {
            if (!mix.hasSources()) { flushAudio(); clipAudioDone = true; }
            clipVideoDone = true;
            drainCameraJoin();
          } else if (mediaStatus == QMediaPlayer::InvalidMedia)
            fail(QStringLiteral("当前媒体后端无法解码素材。"));
        });
    if (cameraEnabled()) {
      cameraPlayer = new QMediaPlayer(decoder);
      auto *cameraSink = new QVideoSink(decoder);
      cameraPlayer->setVideoSink(cameraSink);
      QObject::connect(cameraSink, &QVideoSink::videoFrameChanged, q,
                       [this](const QVideoFrame &frame) {
                         if (!busy || advancing || !frame.isValid())
                           return;
                         touch();
                         cameraFrames.enqueue(frame);
                         if (cameraFrames.size() >= 48 && !cameraPaused) {
                           cameraPaused = true;
                           cameraPlayer->pause();
                         }
                         if (cameraFrames.size() > 120) {
                           fail(QStringLiteral("摄像头解码缓冲已达上限。"));
                           return;
                         }
                         drainCameraJoin();
                       });
      QObject::connect(cameraPlayer, &QMediaPlayer::errorOccurred, q,
                       [this](QMediaPlayer::Error, const QString &message) {
                         fail(QStringLiteral("摄像头素材：") + message);
                       });
      QObject::connect(
          cameraPlayer, &QMediaPlayer::mediaStatusChanged, q,
          [this](QMediaPlayer::MediaStatus state) {
            if (!busy)
              return;
            if (state == QMediaPlayer::LoadedMedia && !cameraLoaded) {
              if (!cameraPlayer->hasVideo()) {
                fail(QStringLiteral("摄像头素材没有视频轨。"));
                return;
              }
              cameraLoaded = true;
              const qint64 cameraStart =
                  qMax<qint64>(0, qRound64(CameraMotion::sourceToCameraTime(cameraSettings,
                      clips[clipIndex].start / 1000000.0) * 1000));
              if (cameraPlayer->duration() > 0 &&
                  cameraStart >= cameraPlayer->duration()) {
                cameraEnded = true;
              } else {
                cameraPlayer->setPosition(cameraStart);
                cameraPlayer->play();
              }
              if (loaded)
                player->play();
            } else if (state == QMediaPlayer::EndOfMedia) {
              cameraEnded = true;
              drainCameraJoin();
            } else if (state == QMediaPlayer::InvalidMedia)
              fail(QStringLiteral("无法解码摄像头素材。"));
          });
      cameraPlayer->setSource(cameraUrl);
    }
    player->setSource(sourceUrl);
  }
};

ExportController::ExportController(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this)) {}
ExportController::~ExportController() {
  d->clear();
  d->renderPool.waitForDone();
  if (!d->temporary.isEmpty())
    QFileInfo(d->temporary).isDir() ? QDir(d->temporary).removeRecursively() : QFile::remove(d->temporary);
}
bool ExportController::busy() const { return d->busy; }
double ExportController::progress() const { return d->progress; }
QString ExportController::status() const { return d->status; }
QString ExportController::error() const { return d->error; }
QUrl ExportController::lastExport() const { return d->last; }
QVariantMap ExportController::performanceMetrics() const {
  QVariantMap result = d->metrics;
  result["enabled"] = d->measure;
  if (d->measure && d->busy) result["wallTimeMs"] = d->measurementClock.elapsed();
  return result;
}

void ExportController::setProject(ProjectController *project) {
  if (m_project == project) return;
  if (m_project) disconnect(m_project, nullptr, this, nullptr);
  m_project = project;
  if (project) connect(project, &ProjectController::changed, this, &ExportController::changed);
  emit changed();
}
QVariantMap ExportController::projectSummary() const { return describeProjectExport(); }
QVariantMap ExportController::describeProjectExport(QVariantMap options, double begin, double end) const {
  auto invalid = [](const QString &message) { return QVariantMap{{"valid",false},{"error",message}}; };
  if (!m_project || !m_project->hasProject()) return invalid(tr("请先打开工程"));
  for (auto it=options.cbegin();it!=options.cend();++it)
    if (it.key()!="audioMode" && it.key()!="resolution" && it.key()!="fps")
      return invalid(tr("未知导出选项"));
  if (end == -1) end=m_project->duration();
  if (!std::isfinite(begin) || !std::isfinite(end) || begin<0 || end<=begin || end>m_project->duration()+1e-6)
    return invalid(tr("导出范围无效"));
  const auto settings=m_project->exportSettings();
  const QString mode=options.value("audioMode","videoAudio").toString();
  const QString resolution=options.value("resolution",settings.value("resolution","source")).toString();
  bool fpsOk=false; const double fps=options.value("fps",settings.value("fps",60)).toDouble(&fpsOk);
  if (!QStringList{"videoAudio","muted","audioOnly"}.contains(mode) ||
      !QStringList{"source","1080p","1440p","4k"}.contains(resolution) || !fpsOk || (fps!=30 && fps!=60))
    return invalid(tr("导出格式、尺寸或帧率无效"));
  const auto clips=m_project->clipsInRange(begin,end);
  if (clips.isEmpty()) return invalid(tr("没有可导出的片段"));
  return {{"valid",true},{"error",QString{}},{"canStart",!busy()},
          {"rangeStart",begin},{"rangeEnd",end},{"duration",end-begin},{"clipCount",clips.size()},
          {"audioMode",mode},{"resolution",resolution},{"fps",int(fps)},
          {"hasSeparateAudio",!m_project->systemAudioSource().isEmpty() || !m_project->microphoneAudioSource().isEmpty()},
          {"hasCamera",!m_project->cameraSource().isEmpty()}};
}
bool ExportController::startProject(QUrl destination, QVariantMap options, double begin, double end) {
  if (busy()) return false;
  const auto summary=describeProjectExport(options,begin,end);
  if (!summary.value("valid").toBool()) {
    d->error=summary.value("error").toString();d->status=tr("导出失败");emit changed();return false;
  }
  for (const auto &key:{"audioMode","resolution","fps"}) options[key]=summary.value(key);
  options["systemAudioSource"]=m_project->systemAudioSource();
  options["microphoneAudioSource"]=m_project->microphoneAudioSource();
  options["audioSettings"]=m_project->audioSettings();
  options["pointerSamples"]=m_project->pointerSamples();
  start(m_project->source(),m_project->clipsInRange(summary.value("rangeStart").toDouble(),summary.value("rangeEnd").toDouble()),
        destination,m_project->framingSegments(),m_project->canvasSettings(),options,
        m_project->cameraSource(),m_project->cameraSettings());
  return busy();
}
bool ExportController::exportProjectOriginal(QUrl folder) {
  if (busy()) return false;
  if (!m_project || !m_project->hasProject()) {
    d->error=tr("请先打开工程");emit changed();return false;
  }
  startOriginal(m_project->source(),folder,m_project->cameraSource(),m_project->systemAudioSource(),m_project->microphoneAudioSource());
  return busy();
}

void ExportController::start(QUrl source, QVariantList clips, QUrl destination,
                             QVariantList framing, QVariantMap canvas,
                             QVariantMap options, QUrl cameraSource,
                             QVariantMap cameraSettings) {
  if (d->busy)
    return;
  d->error.clear();
  d->last = {};
  d->temporary.clear();
  d->progress = 0;
  d->fps = options.value("fps", 60).toInt();
  d->resolution = options.value("resolution", "source").toString();
  d->clips.clear();
  d->framing = std::move(framing);
  d->pointerSamples = options.value(QStringLiteral("pointerSamples")).toList();
  d->canvas = std::move(canvas);
  d->cameraUrl = cameraSource;
  d->cameraSettings = std::move(cameraSettings);
  const double offset =
      d->cameraSettings.value(QStringLiteral("offset"), 0.0).toDouble();
  d->cameraOffset =
      std::isfinite(offset)
          ? qRound64(std::clamp(offset, -86400.0, 86400.0) * 1000000)
          : 0;
  d->total = 0;
  const QString mode =
      options.value(QStringLiteral("audioMode"), QStringLiteral("videoAudio"))
          .toString();
  d->audioOnly = mode == QStringLiteral("audioOnly");
  d->muted = mode == QStringLiteral("muted");
  auto reject = [this](const QString &message) {
    d->error = message;
    d->status = QStringLiteral("导出失败");
    emit changed();
  };
  if (mode != QStringLiteral("videoAudio") &&
      mode != QStringLiteral("audioOnly") && mode != QStringLiteral("muted")) {
    reject(QStringLiteral("无效的音频导出模式。"));
    return;
  }
  if ((d->fps != 30 && d->fps != 60) || !QStringList{"source", "1080p", "1440p", "4k"}.contains(d->resolution)) {
    reject(QStringLiteral("无效的导出分辨率或帧率。"));
    return;
  }
  QString cameraError;
  if (d->cameraEnabled() &&
      !CameraOverlay::validate(d->cameraSettings, &cameraError)) {
    reject(cameraError);
    return;
  }
  if (d->cameraEnabled() && (!cameraSource.isLocalFile() ||
                             !QFileInfo(cameraSource.toLocalFile()).isFile())) {
    reject(QStringLiteral("摄像头素材不存在。"));
    return;
  }
  const QString extension =
      d->audioOnly ? QStringLiteral(".m4a") : QStringLiteral(".mp4");
  QString canvasError;
  if (!d->canvas.isEmpty() &&
      !CanvasRenderer::validate(d->canvas, &canvasError)) {
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
  if (!d->target.endsWith(extension, Qt::CaseInsensitive) ||
      !QFileInfo(d->target).dir().exists()) {
    reject(QStringLiteral("导出路径必须是现有文件夹中的 %1 文件。")
               .arg(extension));
    return;
  }

  for (const QVariant &value : clips) {
    const QVariantMap map = value.toMap();
    const double start = map.value(QStringLiteral("sourceStart")).toDouble();
    const double duration =
        map.value(QStringLiteral("sourceDuration")).toDouble();
    const double rate =
        map.value(QStringLiteral("playbackRate"), 1.0).toDouble();
    if (!std::isfinite(start) || !std::isfinite(duration) || start < 0 ||
        duration <= 0 || duration > 86400 || start > 86400 ||
        !std::isfinite(rate) ||
        rate < 1 || rate > 100) {
      reject(QStringLiteral("片段时间或播放速度无效。"));
      return;
    }
    const qint64 begin = qRound64(start * 1000000),
                 length = qRound64(duration * 1000000);
    if (length <= 0 || qRound64(double(length) / rate) <= 0) {
      reject(QStringLiteral("片段时长过短。"));
      return;
    }
    d->clips.append({begin, begin + length, d->total, rate});
    d->total += qRound64(double(length) / rate);
  }
  if (d->clips.isEmpty()) {
    reject(QStringLiteral("没有可导出的片段。"));
    return;
  }
  QString mixError;
  if (!d->mix.open(options.value("systemAudioSource").toUrl(),
                   options.value("microphoneAudioSource").toUrl(),
                   options.value("audioSettings").toMap(), &mixError)) {
    reject(mixError); return;
  }
  d->temporary = QFileInfo(d->target).dir().filePath(
      QStringLiteral(".screenpilot-export-%1%2")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces), extension));
  ++d->generation;
  d->busy = true;
  d->measure = qEnvironmentVariableIntValue("SCREENPILOT_EXPORT_METRICS") != 0;
  d->metrics.clear();
  if (d->measure) d->measurementClock.start();
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
  d->sourceUrl = source;
  d->clipIndex = 0;
  d->lastVideoTime = -1;
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
  d->openClip();
}

void ExportController::startOriginal(QUrl source, QUrl destinationFolder, QUrl cameraSource,
                                     QUrl systemAudioSource, QUrl microphoneAudioSource) {
  if (d->busy) return;
  d->error.clear(); d->last = {}; d->progress = 0;
  auto reject = [this](const QString &message) {
    d->error = message; d->status = QStringLiteral("原始素材导出失败"); emit changed();
  };
  if (!destinationFolder.isLocalFile() || !QFileInfo(destinationFolder.toLocalFile()).isDir()) {
    reject(QStringLiteral("请选择现有的本地目标文件夹。")); return;
  }
  d->copySources.clear(); d->copyTargets.clear();
  const QList<QUrl> sources{source, cameraSource, systemAudioSource, microphoneAudioSource};
  const QStringList names{"screen", "camera", "system-audio", "microphone-audio"};
  for (int i = 0; i < sources.size(); ++i) {
    if (i && sources[i].isEmpty()) continue;
    if (!sources[i].isLocalFile()) { reject(QStringLiteral("原始素材必须是本地文件。")); return; }
    d->copySources.append(sources[i].toLocalFile());
    d->copyTargets.append(names[i] + "." + QFileInfo(sources[i].toLocalFile()).suffix());
  }
  d->copyTotal = d->copyDone = 0; d->copyIndex = 0;
  for (int i = 0; i < d->copySources.size(); ++i) {
    const QFileInfo info(d->copySources[i]);
    if (!info.isFile() || info.size() <= 0) {
      reject(QStringLiteral("原始素材不存在或为空。")); return;
    }
    d->copyTotal += info.size();
  }
  const QDir parent(destinationFolder.toLocalFile());
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  d->target = parent.filePath(QStringLiteral("ScreenPilot-original-%1").arg(id));
  d->temporary = parent.filePath(QStringLiteral(".screenpilot-original-%1").arg(id));
  if (!parent.mkdir(QFileInfo(d->temporary).fileName())) { reject(QStringLiteral("无法创建素材导出文件夹。")); return; }
  d->busy = true; ++d->generation; d->run = new QObject(this);
  d->status = QStringLiteral("正在复制原始屏幕与摄像头素材（保留内嵌音频，无剪辑效果）…");
  auto *timer = new QTimer(d->run);
  connect(timer, &QTimer::timeout, this, [this] {
    if (!d->busy) return;
    if (!d->copyInput.isOpen()) {
      d->copyInput.setFileName(d->copySources[d->copyIndex]);
      d->copyOutput.setFileName(QDir(d->temporary).filePath(d->copyTargets[d->copyIndex]));
      if (!d->copyInput.open(QIODevice::ReadOnly) || !d->copyOutput.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        d->fail(QStringLiteral("无法读取或写入原始素材。")); return;
      }
    }
    // One bounded block per event-loop turn keeps cancellation responsive.
    const QByteArray block = d->copyInput.read(1024 * 1024);
    if (d->copyInput.error() != QFileDevice::NoError || (!block.isEmpty() && d->copyOutput.write(block) != block.size())) {
      d->fail(QStringLiteral("复制原始素材失败（读取错误或磁盘空间不足）。")); return;
    }
    d->copyDone += block.size();
    d->progress = std::min(.99, double(d->copyDone) / double(d->copyTotal));
    if (d->copyInput.atEnd()) {
      if (!d->copyOutput.flush()) { d->fail(QStringLiteral("无法保存原始素材。")); return; }
      d->copyInput.close(); d->copyOutput.close();
      if (++d->copyIndex == d->copySources.size()) {
        if (d->copyDone != d->copyTotal || QFileInfo::exists(d->target) || !QDir().rename(d->temporary, d->target)) {
          d->fail(QStringLiteral("素材复制期间发生变化或无法发布导出文件夹。")); return;
        }
        d->temporary.clear(); d->clear(); d->busy = false; d->progress = 1;
        d->last = QUrl::fromLocalFile(d->target); d->status = QStringLiteral("原始素材导出完成");
      }
    }
    emit changed();
  });
  timer->start(0); emit changed();
}

void ExportController::cancel() {
  if (!d->busy || d->cancelling)
    return;
  d->cancelling = true;
  // Stop receiving and invalidate pending rendering work before releasing the
  // recorder. Windows can keep its file handle until deferred destruction.
  d->busy = false;
  ++d->generation;
  d->status = QStringLiteral("正在取消导出…");
  d->error.clear();
  const QString cleanup = d->temporary;
  d->clear();
  // Keep the operation busy until its exact private path has been removed, so
  // callers cannot exit on a false completion before the cleanup callback runs.
  d->busy = true;
  auto *cleanupTimer = new QTimer(this);
  cleanupTimer->setInterval(50);
  connect(cleanupTimer, &QTimer::timeout, this,
          [this, cleanup, cleanupTimer, attempts = 0]() mutable {
    const QFileInfo info(cleanup);
    const bool removed = cleanup.isEmpty() || !info.exists() ||
        (info.isDir() ? QDir(cleanup).removeRecursively() : QFile::remove(cleanup));
    if (!removed && ++attempts < 100) return;
    cleanupTimer->stop();
    cleanupTimer->deleteLater();
    if (removed) d->temporary.clear();
    else d->error = QStringLiteral("导出已停止，但临时文件仍被占用，无法清理。");
    d->status = QStringLiteral("导出已取消");
    d->cancelling = false;
    d->busy = false;
    if (d->measure) {
      d->metrics["cancellationCleanupSucceeded"] = removed;
      d->metrics["wallTimeMs"] = d->measurementClock.elapsed();
    }
    emit changed();
  });
  cleanupTimer->start();
  emit changed();
}
