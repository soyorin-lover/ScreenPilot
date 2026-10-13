#include "RecordingController.h"
#include "RecordingProfile.h"
#include <QUuid>
#include "WindowsAudioCapture.h"
#include <QAudioBufferInput>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QQueue>
#include <QScreen>
#include <QScreenCapture>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoFrameInput>
#include <QImage>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <QAbstractVideoBuffer>
#include <QVideoSink>
#include <QWindowCapture>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

struct RecordingBufferMetrics {
  std::atomic<qint64> maps{0}, readbackNs{0}, readbackMaxNs{0}, transformNs{0}, transformMaxNs{0};
  static void add(std::atomic<qint64> &total, std::atomic<qint64> &maximum, qint64 value) {
    total.fetch_add(value, std::memory_order_relaxed);
    qint64 previous = maximum.load(std::memory_order_relaxed);
    while (previous < value && !maximum.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {}
  }
};
namespace {
// A new public video-buffer wrapper gives scheduled output independent PTS
// without changing the explicitly shared capture frame. Mapping happens only
// when the encoder consumes it, not on the GUI scheduling callback.
class RecordingVideoBuffer final : public QAbstractVideoBuffer {
public:
  RecordingVideoBuffer(QVideoFrame source, QRect crop, QSize size,
                       std::shared_ptr<RecordingBufferMetrics> metrics)
      : m_source(std::move(source)), m_crop(crop), m_size(size), m_metrics(std::move(metrics)),
        m_native(crop == QRect(QPoint(), m_source.size()) && size == m_source.size()),
        m_format(m_native ? m_source.surfaceFormat() : QVideoFrameFormat(size, QVideoFrameFormat::Format_RGBA8888)) {}
  ~RecordingVideoBuffer() override { unmap(); }
  QVideoFrameFormat format() const override { return m_format; }
  MapData map(QVideoFrame::MapMode mode) override {
    if (mode != QVideoFrame::ReadOnly) return {};
    QElapsedTimer clock;
    if (m_metrics) clock.start();
    MapData data;
    if (m_native) {
      if (!m_source.map(QVideoFrame::ReadOnly)) return {};
      m_mapped = true;
      data.planeCount = m_source.planeCount();
      for (int plane = 0; plane < data.planeCount && plane < 4; ++plane) {
        data.data[plane] = m_source.bits(plane);
        data.bytesPerLine[plane] = m_source.bytesPerLine(plane);
        data.dataSize[plane] = m_source.mappedBytes(plane);
      }
      if (m_metrics) RecordingBufferMetrics::add(m_metrics->readbackNs, m_metrics->readbackMaxNs, clock.nsecsElapsed());
    } else {
      if (m_image.isNull()) {
        m_image = m_source.toImage();
        if (m_metrics) {
          RecordingBufferMetrics::add(m_metrics->readbackNs, m_metrics->readbackMaxNs, clock.nsecsElapsed());
          clock.restart();
        }
        if (m_image.isNull()) return {};
        if (m_crop != m_image.rect()) m_image = m_image.copy(m_crop);
        if (m_image.size() != m_size) m_image = m_image.scaled(m_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_image = m_image.convertToFormat(QImage::Format_RGBA8888);
        m_source = {};
        if (m_metrics) RecordingBufferMetrics::add(m_metrics->transformNs, m_metrics->transformMaxNs, clock.nsecsElapsed());
      }
      data.planeCount = 1;
      data.data[0] = const_cast<uchar *>(m_image.constBits());
      data.bytesPerLine[0] = m_image.bytesPerLine();
      data.dataSize[0] = int(m_image.sizeInBytes());
    }
    if (m_metrics) m_metrics->maps.fetch_add(1, std::memory_order_relaxed);
    return data;
  }
  void unmap() override {
    if (m_mapped) { m_source.unmap(); m_mapped = false; }
  }
private:
  QVideoFrame m_source;
  QRect m_crop;
  QSize m_size;
  QImage m_image;
  std::shared_ptr<RecordingBufferMetrics> m_metrics;
  bool m_native = false, m_mapped = false;
  QVideoFrameFormat m_format;
};
}

RecordingController::RecordingController(QObject *parent)
    : QObject(parent), m_session(new QMediaCaptureSession(this)),
      m_sourceSession(new QMediaCaptureSession(this)),
      m_videoInput(new QVideoFrameInput(this)), m_videoTimer(new QTimer(this)),
      m_screen(new QScreenCapture(this)), m_window(new QWindowCapture(this)),
      m_sink(new QVideoSink(this)), m_watchdog(new QTimer(this)),
      m_pointerTimer(new QTimer(this)) {
  m_videoTimer->setSingleShot(true);
  m_videoTimer->setTimerType(Qt::PreciseTimer);
  connect(m_videoTimer, &QTimer::timeout, this, &RecordingController::scheduleVideoFrame);
  connect(m_videoInput, &QVideoFrameInput::readyToSendVideoFrame,
          this, &RecordingController::pumpVideoFrame);
  QSettings settings;
  const int savedRate = settings.value("recording/frameRate", 60).toInt();
  if (savedRate == 30 || savedRate == 60) m_frameRate = savedRate;
  const QString savedResolution = settings.value("recording/resolutionMode", "source").toString();
  if (savedResolution == "source" || savedResolution == "4k" || savedResolution == "1080p")
    m_resolutionMode = savedResolution;
  const QString savedCodec = settings.value("recording/videoCodec", "h264").toString();
  if (savedCodec == "h264" || savedCodec == "hevc")
    m_videoCodec = savedCodec;
  m_nativeAudio = new WindowsAudioCapture(this);
  // Qt 6.8.3 initializes AudioEncoder::m_canPushFrame=false. With a format
  // hint no first buffer reaches addBuffer() to update it, so readiness never
  // becomes true. Let the pending-format initializer accept the first PCM
  // buffer; that primes the encoder and its normal backpressure signals.
  m_bufferInput = new QAudioBufferInput(this);
  m_systemAudio = QSettings().value("recording/systemAudio", true).toBool();
  const QString scope = settings.value("recording/systemAudioScope", "all").toString();
  if (scope == "all" || scope == "application") m_systemAudioScope = scope;
  auto pending = std::make_shared<QQueue<QAudioBuffer>>();
  auto pump = [this, pending] {
    if (m_paused)
      return;
    while (!pending->isEmpty() &&
           m_bufferInput->sendAudioBuffer(pending->head()))
      pending->dequeue();
  };
  connect(m_bufferInput, &QAudioBufferInput::readyToSendAudioBuffer, this,
          pump);
  connect(m_nativeAudio, &WindowsAudioCapture::bufferReady, this,
          [this, pending, pump](const QAudioBuffer &buffer) {
            if (!m_busy || m_stopRequested || m_paused)
              return;
            // Explicit input timestamps use the same compact timeline as
            // sidecars and scheduled video, including across manual pauses.
            pending->enqueue(QAudioBuffer(QByteArray(buffer.constData<char>(), buffer.byteCount()),
                                           buffer.format(), m_mixedAudioFrames * 1000000 / 48000));
            m_mixedAudioFrames += buffer.frameCount();
            pump();
            if (pending->size() > 200)
              fail(tr("音频编码速度不足，录制已停止。"));
          });
  connect(this, &RecordingController::changed, this, [this, pending, pump] {
    if (!m_busy)
      pending->clear();
    else if (!m_paused)
      pump();
  });
  auto writeSource = [this](PcmWaveWriter &writer, const QAudioBuffer &buffer) {
    if (!m_busy || m_stopRequested || m_paused || !writer.isOpen()) return;
    QString error;
    if (!writer.append(buffer, &error)) {
      // WASAPI is iterating its stream list while emitting this buffer.
      // Defer stopping it until the current tick has released its references.
      QMetaObject::invokeMethod(this, [this, error] {
        if (m_busy && !m_stopRequested) fail(tr("Independent audio write failed: %1").arg(error));
      }, Qt::QueuedConnection);
    }
  };
  connect(m_nativeAudio, &WindowsAudioCapture::systemBufferReady, this,
          [this, writeSource](const QAudioBuffer &buffer) { writeSource(m_systemWriter, buffer); });
  connect(m_nativeAudio, &WindowsAudioCapture::microphoneBufferReady, this,
          [this, writeSource](const QAudioBuffer &buffer) { writeSource(m_microphoneWriter, buffer); });
  connect(m_nativeAudio, &WindowsAudioCapture::levelChanged, this,
          [this](double level) {
            m_microphoneLevel = level;
            emit audioLevelChanged();
          });
  connect(m_nativeAudio, &WindowsAudioCapture::failed, this,
          [this](const QString &error) { fail(error); });
  m_pointerTimer->setTimerType(Qt::PreciseTimer);
  m_pointerTimer->setInterval(
      33); // Approximately 30 Hz; UI stalls delay samples.
  connect(m_pointerTimer, &QTimer::timeout, this,
          &RecordingController::samplePointer);
  m_directory =
      QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
          .filePath(QStringLiteral("ScreenPilot"));
  m_sourceSession->setVideoSink(m_sink);
  m_session->setVideoFrameInput(m_videoInput);
  m_watchdog->setSingleShot(true);
  createRecorder();
  connect(m_screen, &QScreenCapture::errorOccurred, this,
          [this](QScreenCapture::Error, const QString &message) {
            if (m_busy)
              fail(tr("屏幕采集失败：%1").arg(message));
          });
  connect(m_window, &QWindowCapture::errorOccurred, this,
          [this](QWindowCapture::Error, const QString &message) {
            if (m_busy)
              fail(tr("窗口采集失败：%1").arg(message));
          });
  connect(m_watchdog, &QTimer::timeout, this, [this] {
    if (m_waitingFrame) {
      fail(tr("未收到采集画面，请检查窗口是否可见及系统采集权限。"));
      return;
    }
    if (!m_busy)
      return;
    m_failed = true;
    m_error = tr("编码器未能及时结束，文件可能不完整。请重新录制。");
    // A failed backend must not leave the UI permanently busy.
    m_session->setRecorder(nullptr);
    delete m_recorder;
    m_recorder = nullptr;
    createRecorder();
    finish();
  });
  connect(m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            if (!m_busy || m_stopRequested || !frame.isValid()) return;
            if (m_measurePerformance) {
              const qint64 now = m_performanceClock.nsecsElapsed();
              if (m_lastCaptureNs >= 0) timingMetric("captureInterval", now - m_lastCaptureNs);
              m_lastCaptureNs = now;
              metric("capturedFrames");
              m_performanceMetrics["rawCaptureWidth"] = frame.width();
              m_performanceMetrics["rawCaptureHeight"] = frame.height();
            }
            m_latestVideoFrame = frame;
            if (!m_waitingFrame) return;
            m_waitingFrame = false;
            m_watchdog->stop();
            const QSize size = RecordingProfile::outputSize(capturePixels(frame.size()).size(), m_resolutionMode);
            if (size.isEmpty()) {
              fail(tr("采集画面尺寸不足，无法编码。"));
              return;
            }
            m_effectiveVideoSize = size;
            m_recorder->setVideoResolution(size);
            m_recorder->setVideoBitRate(RecordingProfile::videoBitRate(size));
            emit changed();
            if (!m_busy || m_stopRequested || m_failed)
              return;
            beginPointerCapture();
            if (m_systemAudio || m_microphone) {
              QString error;
              const QString id = m_microphone && m_selectedMicrophone >= 0
                                     ? m_microphones.at(m_selectedMicrophone)
                                           .toMap()
                                           .value("id")
                                           .toString()
                                     : QString();
              quint32 processId = 0;
              if (m_systemAudio && m_systemAudioScope == "application") {
                processId = m_audioProcessId;
#ifdef Q_OS_WIN
                if (!processId && m_pointerWindow) {
                  DWORD windowProcess = 0;
                  if (GetWindowThreadProcessId(reinterpret_cast<HWND>(m_pointerWindow), &windowProcess))
                    processId = windowProcess;
                }
#endif
                if (!processId) {
                  fail(tr("Application audio requires an explicit process or a uniquely matched capture window."));
                  return;
                }
              }
              if (!m_nativeAudio->start(m_systemAudio, id, &error, processId)) {
                fail(error);
                return;
              }
            }
            // Process-loopback activation may wait for its async callback.
            // Start the visible video/pointer timeline after that wait, beside
            // the WASAPI epoch, rather than baking startup delay into video.
            m_pointerClock.restart();
            m_pendingPointerSamples.clear();
            samplePointer();
            emit mediaStarted(m_directory);
            if (!m_busy || m_stopRequested || m_failed)
              return;
            m_recorder->record();
            if (m_busy && !m_stopRequested) scheduleVideoFrame();
            if (m_busy && !m_recording)
              m_watchdog->start(15000);
          });
  connect(qGuiApp, &QGuiApplication::screenAdded, this,
          [this] { refreshSources(); });
  connect(qGuiApp, &QGuiApplication::screenRemoved, this,
          [this](QScreen *screen) {
            if (m_busy && m_screen->screen() == screen)
              fail(tr("正在录制的屏幕已断开。"));
            refreshSources();
          });
  refreshSources();
}
RecordingController::~RecordingController() {
  stopCapture();
  m_recorder->stop();
  m_systemWriter.discard();
  m_microphoneWriter.discard();
}
QObject *RecordingController::previewSink() const { return m_preview.data(); }
void RecordingController::setPreviewSink(QObject *value) {
  m_preview = qobject_cast<QVideoSink *>(value);
  emit changed();
}

void RecordingController::setFrameRate(int value) {
  if (m_busy || (value != 30 && value != 60) || value == m_frameRate) return;
  m_frameRate = value;
  QSettings().setValue("recording/frameRate", value);
  emit changed();
}
void RecordingController::setCaptureRegion(const QRectF &value) {
  if (m_busy || !std::isfinite(value.x()) || !std::isfinite(value.y()) ||
      !std::isfinite(value.width()) || !std::isfinite(value.height()) ||
      value.width() <= 0 || value.height() <= 0 || value.x() < 0 || value.y() < 0 ||
      value.right() > 1 || value.bottom() > 1 || value == m_captureRegion) return;
  m_captureRegion = value;
  m_effectiveVideoSize = {};
  emit changed();
}
void RecordingController::clearCaptureRegion() { setCaptureRegion(QRectF(0, 0, 1, 1)); }
QRect RecordingController::capturePixels(const QSize &size) const {
  const int left = qBound(0, int(std::floor(m_captureRegion.left() * size.width())), size.width());
  const int top = qBound(0, int(std::floor(m_captureRegion.top() * size.height())), size.height());
  const int right = qBound(left, int(std::ceil(m_captureRegion.right() * size.width())), size.width());
  const int bottom = qBound(top, int(std::ceil(m_captureRegion.bottom() * size.height())), size.height());
  return QRect(left, top, right - left, bottom - top);
}
QVariantMap RecordingController::performanceMetrics() const {
  QVariantMap result = m_performanceMetrics;
  result["enabled"] = m_measurePerformance;
  result["requestedFps"] = m_frameRate;
  if (m_bufferMetrics) {
    result["bufferMapCount"] = m_bufferMetrics->maps.load(std::memory_order_relaxed);
    result["readbackTotalNs"] = m_bufferMetrics->readbackNs.load(std::memory_order_relaxed);
    result["readbackMaxNs"] = m_bufferMetrics->readbackMaxNs.load(std::memory_order_relaxed);
    result["cropScaleTotalNs"] = m_bufferMetrics->transformNs.load(std::memory_order_relaxed);
    result["cropScaleMaxNs"] = m_bufferMetrics->transformMaxNs.load(std::memory_order_relaxed);
    result["conversionOnConsumption"] = true;
  }
  return result;
}
void RecordingController::metric(const QString &key, qint64 value) {
  if (m_measurePerformance) m_performanceMetrics[key] = m_performanceMetrics.value(key).toLongLong() + value;
}
void RecordingController::timingMetric(const QString &key, qint64 nanoseconds) {
  if (!m_measurePerformance) return;
  metric(key + "Count");
  metric(key + "TotalNs", nanoseconds);
  const QString maximum = key + "MaxNs";
  m_performanceMetrics[maximum] = std::max(m_performanceMetrics.value(maximum).toLongLong(), nanoseconds);
}
void RecordingController::pumpVideoFrame() {
  if (m_pumpingVideo || !m_busy || m_stopRequested || m_paused || !m_pendingVideoFrame.isValid()) return;
  m_pumpingVideo = true;
  if (m_videoInput->sendVideoFrame(m_pendingVideoFrame)) {
    if (m_measurePerformance) {
      metric("sentFrames");
      const qint64 pts = m_pendingVideoFrame.startTime();
      if (m_lastSentUs >= 0) {
        timingMetric("sentPtsInterval", (pts - m_lastSentUs) * 1000);
        if (pts - m_lastSentUs > 1000000 / m_frameRate + 1) metric("sentPtsGaps");
      }
      m_lastSentUs = pts;
      m_performanceMetrics["lastSentPtsUs"] = pts;
    }
    m_pendingVideoFrame = {};
  } else metric("sendBackpressureCount");
  m_pumpingVideo = false;
}
void RecordingController::scheduleVideoFrame() {
  if (!m_busy || m_stopRequested || m_paused || !m_pointerClock.isValid() || !m_latestVideoFrame.isValid()) return;
  // A single latest capture and pending output bound memory under backpressure.
  // Missed slots are dropped, never emitted in a catch-up burst.
  const qint64 now = qMax<qint64>(0, (m_pointerClock.nsecsElapsed() - m_pausedNsecs) / 1000);
  if (now * m_frameRate >= m_nextVideoIndex * 1000000) {
    const qint64 index = m_nextVideoIndex == 0 ? 0 : qMax(m_nextVideoIndex, now * m_frameRate / 1000000);
    metric("missedScheduleSlots", index - m_nextVideoIndex);
    metric("scheduledFrames");
    const QRect crop = capturePixels(m_latestVideoFrame.size());
    if (crop.width() < 2 || crop.height() < 2) { fail(tr("Capture region is too small.")); return; }
    const bool native = crop == QRect(QPoint(), m_latestVideoFrame.size()) &&
        m_effectiveVideoSize == m_latestVideoFrame.size();
    QVideoFrame output;
    if (native) {
      output = QVideoFrame(std::make_unique<RecordingVideoBuffer>(
          m_latestVideoFrame, crop, m_effectiveVideoSize, m_bufferMetrics));
    } else {
      // Keep the proven transformed path: serializing conversion with encoding
      // reduced 1080p capture throughput in the same-machine regression.
      QImage image = m_latestVideoFrame.toImage();
      if (image.isNull()) { fail(tr("Unable to read the captured video frame.")); return; }
      if (crop != image.rect()) image = image.copy(crop);
      if (image.size() != m_effectiveVideoSize)
        image = image.scaled(m_effectiveVideoSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      output = QVideoFrame(image);
    }
    metric(native ? "nativeMappedFrames" : "transformedFrames");
    const qint64 sourceStart = m_latestVideoFrame.startTime(), sourceEnd = m_latestVideoFrame.endTime();
    output.setStartTime(index * 1000000 / m_frameRate);
    output.setEndTime((index + 1) * 1000000 / m_frameRate);
    if (m_latestVideoFrame.startTime() != sourceStart || m_latestVideoFrame.endTime() != sourceEnd) {
      fail(tr("Scheduled video timestamps changed the capture frame.")); return;
    }
    if (m_pendingVideoFrame.isValid()) metric("overwrittenPendingFrames");
    m_pendingVideoFrame = output;
    // Native capture already has the exact preview pixels. Preserve its GPU
    // handle for the preview rather than forcing it through the CPU wrapper.
    if (m_preview) m_preview->setVideoFrame(native ? m_latestVideoFrame : output);
    m_nextVideoIndex = index + 1;
    pumpVideoFrame();
  }
  const qint64 after = qMax<qint64>(0, (m_pointerClock.nsecsElapsed() - m_pausedNsecs) / 1000);
  const qint64 delay = m_nextVideoIndex * 1000000 / m_frameRate - after;
  if (m_busy && !m_stopRequested && !m_paused)
    m_videoTimer->start(int(qMax<qint64>(1, (delay + 999) / 1000)));
}

void RecordingController::setSelectedSource(int value) {
  if (!m_busy && value >= 0 && value < m_targets.size() &&
      value != m_selected) {
    m_selected = value;
    m_captureRegion = QRectF(0,0,1,1);
    m_audioProcessId = 0;
    m_effectiveVideoSize = {};
    emit changed();
  }
}
void RecordingController::setResolutionMode(const QString &value) {
  if (m_busy || value == m_resolutionMode ||
      (value != "source" && value != "4k" && value != "1080p"))
    return;
  m_resolutionMode = value;
  m_effectiveVideoSize = {};
  QSettings().setValue("recording/resolutionMode", value);
  emit changed();
}
void RecordingController::setVideoCodec(const QString &value) {
  if (m_busy || value == m_videoCodec || (value != "h264" && value != "hevc"))
    return;
  // Preserve the explicit choice. start() reports unavailable configurations;
  // it must never silently substitute another codec.
  m_videoCodec = value;
  QSettings().setValue("recording/videoCodec", value);
  emit changed();
}
QVariantList RecordingController::availableVideoCodecs() const {
  QVariantList result;
  for (const auto codec : {QMediaFormat::VideoCodec::H264, QMediaFormat::VideoCodec::H265}) {
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(codec);
    format.setAudioCodec((m_microphone || m_systemAudio)
                             ? QMediaFormat::AudioCodec::AAC
                             : QMediaFormat::AudioCodec::Unspecified);
    if (format.isSupported(QMediaFormat::Encode))
      result.append(QVariantMap{{"label", codec == QMediaFormat::VideoCodec::H264 ? "H.264" : "HEVC"},
                                {"value", codec == QMediaFormat::VideoCodec::H264 ? "h264" : "hevc"}});
  }
  return result;
}
QVariantMap RecordingController::capabilities() const {
  return {{"sourceTypes",QStringList{"screen","window","region"}},
          {"frameRates",QVariantList{30,60}},
          {"resolutions",QStringList{"source","4k","1080p"}},
          {"videoCodecs",availableVideoCodecs()},
          {"applicationAudio",WindowsAudioCapture::supportsApplicationAudio()},
          {"nativeCursorRemoval",false},{"independentCursorShapes",false},
          {"proResRecording",false}};
}
QVariantMap RecordingController::readiness() const {
  QString reason;
  if(m_busy) reason=tr("录制仍在进行或收尾");
  else if(m_selected<0 || m_selected>=m_targets.size()) reason=tr("请选择有效的屏幕或窗口");
  else if(m_microphone && (m_selectedMicrophone<0 || m_selectedMicrophone>=m_microphones.size())) reason=tr("请选择可用麦克风");
  else if(m_systemAudio && m_systemAudioScope=="application" && !WindowsAudioCapture::supportsApplicationAudio()) reason=tr("当前系统不支持应用声音采集");
  else if(m_systemAudio && m_systemAudioScope=="application" && !m_audioProcessId && m_targets[m_selected].isScreen) reason=tr("应用声音需要选择窗口或有效进程");
  else {
    bool codecAvailable=false;
    for(const auto &entry:availableVideoCodecs())
      codecAvailable=codecAvailable || entry.toMap().value("value").toString()==m_videoCodec;
    if(!codecAvailable) reason=tr("当前录制编码不可用");
  }
  return {{"canStart",reason.isEmpty()},{"reason",reason},{"busy",m_busy},
          {"hasSelectedSource",m_selected>=0 && m_selected<m_targets.size()}};
}
void RecordingController::setOutputDirectory(const QString &value) {
  const QUrl url(value);
  const QString path = url.isLocalFile() ? url.toLocalFile() : value;
  if (!m_busy && !path.trimmed().isEmpty() && path != m_directory) {
    m_directory = path;
    emit changed();
  }
}
void RecordingController::setMicrophoneEnabled(bool value) {
  if (!m_busy && value != m_microphone) {
    m_microphone = value;
    emit changed();
  }
}
void RecordingController::setSystemAudioScope(const QString &scope) {
  if (m_busy || (scope != "all" && scope != "application") || scope == m_systemAudioScope) return;
  m_systemAudioScope = scope;
  QSettings().setValue("recording/systemAudioScope", scope);
  emit changed();
}
void RecordingController::setAudioProcessId(quint32 processId) {
  if (m_busy || processId == m_audioProcessId) return;
  // Process IDs are session choices, never restored after a process exits.
  m_audioProcessId = processId;
  emit changed();
}
void RecordingController::setSystemAudioEnabled(bool value) {
  if (m_busy || value == m_systemAudio)
    return;
  m_systemAudio = value;
  QSettings().setValue("recording/systemAudio", value);
  emit changed();
}
void RecordingController::setSelectedMicrophone(int value) {
  if (!m_busy && value >= 0 && value < m_microphones.size()) {
    m_selectedMicrophone = value;
    emit changed();
  }
}
void RecordingController::createRecorder() {
  m_recorder = new QMediaRecorder(this);
  m_session->setRecorder(m_recorder);
  connect(m_recorder, &QMediaRecorder::durationChanged, this,
          [this](qint64 value) {
            m_duration = value;
            emit changed();
          });
  connect(m_recorder, &QMediaRecorder::errorOccurred, this,
          [this](QMediaRecorder::Error, const QString &message) {
            if (m_busy)
              fail(tr("录制失败：%1").arg(message));
          });
  connect(m_recorder, &QMediaRecorder::recorderStateChanged, this,
          [this](QMediaRecorder::RecorderState state) {
            if (!m_busy)
              return;
            m_recording =
                state == QMediaRecorder::RecordingState && !m_stopRequested;
            m_paused = state == QMediaRecorder::PausedState && !m_stopRequested;
            if (m_paused)
              m_status = tr("已暂停");
            if (state == QMediaRecorder::RecordingState) {
              m_watchdog->stop();
              m_status = tr("正在录制");
            }
            if (state == QMediaRecorder::StoppedState) {
              finish();
              return;
            }
            emit changed();
          });
}
void RecordingController::refreshSources() {
  if (m_busy)
    return;
  const bool firstScan=m_targets.isEmpty();
  const bool hadSelection=m_selected>=0 && m_selected<m_targets.size();
  const Source previous=hadSelection?m_targets[m_selected]:Source{nullptr,{},true};
  const QString previousMicrophone =
      m_selectedMicrophone >= 0 && m_selectedMicrophone < m_microphones.size()
          ? m_microphones.at(m_selectedMicrophone)
                .toMap()
                .value("id")
                .toString()
          : QString();
  m_microphones = WindowsAudioCapture::microphones();
  m_selectedMicrophone = m_microphones.isEmpty() || !previousMicrophone.isEmpty() ? -1 : 0;
  for (int i = 0; i < m_microphones.size(); ++i)
    if (m_microphones.at(i).toMap().value("id").toString() ==
        previousMicrophone)
      m_selectedMicrophone = i;
  m_sources.clear();
  m_targets.clear();
  for (auto *screen : QGuiApplication::screens()) {
    const int id = m_targets.size();
    m_targets.append({screen, {}, true});
    m_sources.append(QVariantMap{
        {"label", tr("屏幕 %1 · %2").arg(id + 1).arg(screen->name())},
        {"kind", "screen"},
        {"screenIndex", id},
        {"devicePixelRatio", screen->devicePixelRatio()},
        {"pixelWidth", qRound(screen->size().width()*screen->devicePixelRatio())},
        {"pixelHeight", qRound(screen->size().height()*screen->devicePixelRatio())},
        {"id", QString::number(id)}});
  }
  for (const auto &window : QWindowCapture::capturableWindows()) {
    if (!window.isValid() ||
        window.description().contains(QStringLiteral("ScreenPilot"),
                                      Qt::CaseInsensitive))
      continue;
    const int id = m_targets.size();
    m_targets.append({nullptr, window, false});
    m_sources.append(QVariantMap{{"label", window.description()},
                                 {"kind", "window"},
                                 {"id", QString::number(id)}});
  }
  m_selected = firstScan && !m_targets.isEmpty() ? 0 : -1;
  if(hadSelection) {
    for(int i=0;i<m_targets.size();++i) {
      const auto &candidate=m_targets[i];
      if(candidate.isScreen==previous.isScreen &&
         (candidate.isScreen ? candidate.screen==previous.screen : candidate.window==previous.window)) {
        m_selected=i;break;
      }
    }
    if(m_selected<0) {
      m_captureRegion=QRectF(0,0,1,1);m_audioProcessId=0;
      m_status=tr("原录制来源已不可用，请重新选择");
    }
  }
  emit changed();
}
void RecordingController::start() {
  if (m_busy)
    return;
  m_measurePerformance = qEnvironmentVariableIntValue("SCREENPILOT_RECORDING_METRICS") != 0;
  m_performanceMetrics.clear();
  m_bufferMetrics = m_measurePerformance ? std::make_shared<RecordingBufferMetrics>() : nullptr;
  m_lastCaptureNs = m_lastSentUs = -1;
  if (m_measurePerformance) m_performanceClock.start();
  m_pendingPointerSamples.clear();
  m_pendingMarkers.clear();
  m_pointerClock.invalidate();
  m_error.clear();
  m_failed = false;
  m_stopRequested = false;
  m_paused = false;
  m_discard = false;
  m_restart = false;
  m_pausedNsecs = 0;
  m_duration = 0;
  m_nextVideoIndex = 0;
  m_mixedAudioFrames = 0;
  m_latestVideoFrame = {};
  m_pendingVideoFrame = {};
  m_effectiveVideoSize = {};
  if (m_selected < 0 || m_selected >= m_targets.size()) {
    fail(tr("请先选择可用的屏幕或窗口。"));
    return;
  }
  const auto target = m_targets.at(m_selected);
  if ((target.isScreen && !target.screen) ||
      (!target.isScreen && !target.window.isValid())) {
    fail(tr("采集来源已不可用，请刷新来源。"));
    return;
  }
  if (m_microphone && (m_selectedMicrophone < 0 ||
                       m_selectedMicrophone >= m_microphones.size())) {
    fail(tr("未检测到麦克风。请关闭麦克风选项或连接设备。"));
    return;
  }
  QMediaFormat format(QMediaFormat::MPEG4);
  format.setVideoCodec(m_videoCodec == "hevc" ? QMediaFormat::VideoCodec::H265
                                               : QMediaFormat::VideoCodec::H264);
  format.setAudioCodec((m_microphone || m_systemAudio)
                           ? QMediaFormat::AudioCodec::AAC
                           : QMediaFormat::AudioCodec::Unspecified);
  if (!format.isSupported(QMediaFormat::Encode)) {
    fail(tr("当前媒体后端不支持所选 MP4/%1 编码配置。")
             .arg(m_videoCodec == "hevc" ? "HEVC" : "H.264"));
    return;
  }
  QDir dir(m_directory);
  if (!QDir().mkpath(dir.absolutePath()) ||
      !QFileInfo(dir.absolutePath()).isDir()) {
    fail(tr("无法创建输出目录。"));
    return;
  }
  m_pending = dir.absoluteFilePath(
      QStringLiteral("ScreenPilot-%1-%2.mp4")
          .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz"),
               QUuid::createUuid().toString(QUuid::WithoutBraces)));
  // Reserve a unique destination atomically; never truncate an existing
  // recording.
  QFile reservation(m_pending);
  if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    fail(tr("输出目录不可写，或目标文件已存在。"));
    return;
  }
  reservation.close();
  m_pendingSystemAudio = m_systemAudio ? m_pending + ".system.wav" : QString();
  m_pendingMicrophoneAudio = m_microphone ? m_pending + ".microphone.wav" : QString();
  QString audioError;
  if ((!m_pendingSystemAudio.isEmpty() && !m_systemWriter.open(m_pendingSystemAudio, &audioError)) ||
      (!m_pendingMicrophoneAudio.isEmpty() && !m_microphoneWriter.open(m_pendingMicrophoneAudio, &audioError))) {
    m_systemWriter.discard(); m_microphoneWriter.discard();
    QFile::remove(m_pending); m_pending.clear();
    fail(tr("Independent audio creation failed: %1").arg(audioError)); return;
  }
  m_recorder->setMediaFormat(format);
  m_recorder->setVideoFrameRate(m_frameRate);
  m_recorder->setAutoStop(false);
  m_recorder->setQuality(QMediaRecorder::HighQuality);
  m_recorder->setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
  m_recorder->setAudioBitRate(192000);
  m_recorder->setOutputLocation(QUrl::fromLocalFile(m_pending));
  m_session->setAudioInput(nullptr);
  m_session->setAudioBufferInput((m_microphone || m_systemAudio) ? m_bufferInput
                                                                 : nullptr);
  m_sourceSession->setScreenCapture(target.isScreen ? m_screen : nullptr);
  m_sourceSession->setWindowCapture(target.isScreen ? nullptr : m_window);
  m_busy = true;
  m_waitingFrame = true;
  m_status = tr("正在启动采集…");
  emit changed();
  m_watchdog->start(10000);
  if (target.isScreen) {
    m_screen->setScreen(target.screen);
    m_screen->start();
  } else {
    m_window->setWindow(target.window);
    m_window->start();
  }
}
void RecordingController::beginPointerCapture() {
#ifdef Q_OS_WIN
  // Zero is first valid frame / record() request, not an encoder PTS.
  // Monotonic approximation only: compositor-to-media sync is not verified.
  m_pointerWindow = 0;
  const auto &target = m_targets.at(m_selected);
  if (!target.isScreen) {
    // QCapturableWindow has no public native handle. Match only an exact,
    // unique visible title; duplicate/missing titles disable spatial mapping.
    struct Match {
      QString title;
      HWND window = nullptr;
      int count = 0;
    } match{target.window.description()};
    const bool enumerated = EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
          auto &match = *reinterpret_cast<Match *>(data);
          if (!IsWindowVisible(window))
            return TRUE;
          const int length = GetWindowTextLengthW(window);
          if (length <= 0)
            return TRUE;
          QString title(length + 1, QChar());
          const int copied = GetWindowTextW(
              window, reinterpret_cast<LPWSTR>(title.data()), title.size());
          title.resize(copied);
          if (title == match.title) {
            match.window = window;
            ++match.count;
          }
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&match));
    if (enumerated && match.count == 1)
      m_pointerWindow = reinterpret_cast<quintptr>(match.window);
  }
  m_pointerClock.start();
  m_leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  samplePointer();
  m_pointerTimer->start();
#endif
}
void RecordingController::samplePointer() {
#ifdef Q_OS_WIN
  if (!m_busy || m_stopRequested || m_paused || !m_pointerClock.isValid() ||
      m_selected < 0 || m_selected >= m_targets.size())
    return;
  const double seconds = (m_pointerClock.nsecsElapsed() - m_pausedNsecs) / 1.0e9;
  // Polling detects sampled down edges; a full press/release between ticks
  // can be missed, and event-loop stalls increase this blind interval.
  const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  const bool click = down && !m_leftButtonDown;
  m_leftButtonDown = down;
  POINT cursor{};
  if (!GetCursorPos(&cursor))
    return;
  RECT bounds{};
  const auto &target = m_targets.at(m_selected);
  bool valid = false;
  if (target.isScreen && target.screen) {
    // QScreen geometry is logical; matching display settings give physical
    // bounds without multiplying mixed-DPI desktop origins by one DPR.
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    const QString name = target.screen->name();
    if (EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(name.utf16()),
                             ENUM_CURRENT_SETTINGS, &mode)) {
      bounds = {mode.dmPosition.x, mode.dmPosition.y,
                mode.dmPosition.x + static_cast<LONG>(mode.dmPelsWidth),
                mode.dmPosition.y + static_cast<LONG>(mode.dmPelsHeight)};
      valid = true;
    }
  } else if (!target.isScreen) {
    // Refresh each tick to follow movement. GetWindowRect includes non-client
    // and invisible resize borders; Qt capture may crop these. Exact alignment
    // needs backend-specific calibration; no pixel error bound is claimed.
    const auto window = reinterpret_cast<HWND>(m_pointerWindow);
    valid = window && IsWindow(window) && GetWindowRect(window, &bounds);
    if (!valid)
      m_pointerWindow = 0; // Do not rematch a disappeared window mid-recording.
  }
  const double width = bounds.right - bounds.left;
  const double height = bounds.bottom - bounds.top;
  const bool usable = valid && width > 0 && height > 0;
  const double sourceX = usable ? (cursor.x - bounds.left) / width : 0.0;
  const double sourceY = usable ? (cursor.y - bounds.top) / height : 0.0;
  const double x = (sourceX - m_captureRegion.x()) / m_captureRegion.width();
  const double y = (sourceY - m_captureRegion.y()) / m_captureRegion.height();
  m_pendingPointerSamples.append(
      QVariantMap{{"time", seconds},
                  {"x", qBound(0.0, x, 1.0)},
                  {"y", qBound(0.0, y, 1.0)},
                  {"click", click},
                  {"inside", usable && x >= 0 && x < 1 && y >= 0 && y < 1}});
#endif
}
void RecordingController::stopCapture() {
  m_pointerTimer->stop();
  m_videoTimer->stop();
  m_latestVideoFrame = {};
  m_pendingVideoFrame = {};
  m_nativeAudio->stop();
  m_screen->stop();
  m_window->stop();
}
void RecordingController::stop() {
  if (!m_busy || m_stopRequested)
    return;
  m_nativeAudio->flush();
  m_stopRequested = true;
  emit mediaStopping();
  m_recording = false;
  m_status = tr("正在完成文件…");
  if (m_waitingFrame) {
    m_waitingFrame = false;
    finish();
    return;
  }
  m_watchdog->start(15000);
  m_videoInput->sendVideoFrame(QVideoFrame());
  m_recorder->stop();
  stopCapture();
  if (m_busy && m_recorder->recorderState() == QMediaRecorder::StoppedState)
    finish();
  emit changed();
}
void RecordingController::pause() {
  if (!m_recording || m_stopRequested)
    return;
  m_nativeAudio->flush();
  m_pauseStarted = m_pointerClock.nsecsElapsed();
  // Pause the explicit input timeline ourselves. Calling recorder.pause()
  // as well would make the backend adjust already-compacted video timestamps.
  m_paused = true;
  m_recording = false;
  m_status = tr("已暂停");
  m_pointerTimer->stop();
  m_videoTimer->stop();
  m_pendingVideoFrame = {};
  emit changed();
  emit mediaPaused(true);
}
void RecordingController::resume() {
  if (!m_paused || m_stopRequested)
    return;
  // Drain the 60 ms WASAPI arrival allowance while still paused so paused
  // PCM cannot leak into the resumed file. Keep device anchors unchanged.
  m_nativeAudio->flush();
  m_pausedNsecs += m_pointerClock.nsecsElapsed() - m_pauseStarted;
  m_paused = false;
  m_recording = true;
  m_status = tr("正在录制");
  emit changed();
#ifdef Q_OS_WIN
  m_leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
#endif
  samplePointer();
  m_pointerTimer->start();
  scheduleVideoFrame();
  emit mediaPaused(false);
}
void RecordingController::discard() {
  if (!m_busy || m_stopRequested)
    return;
  m_discard = true;
  emit mediaDiscarding();
  stop();
}
void RecordingController::restart() {
  if (!m_busy || m_stopRequested)
    return;
  m_restart = true;
  discard();
}
void RecordingController::fail(const QString &message) {
  m_error = message;
  m_failed = true;
  if (m_busy) {
    m_waitingFrame = false;
    m_stopRequested = true;
    emit mediaStopping();
    m_recording = false;
    m_watchdog->start(15000);
    m_recorder->stop();
    stopCapture();
    if (m_busy && m_recorder->recorderState() == QMediaRecorder::StoppedState)
      finish();
  } else {
    m_status = tr("无法录制");
    emit changed();
  }
}
void RecordingController::finish() {
  m_watchdog->stop();
  m_waitingFrame = false;
  stopCapture();
  m_session->setAudioInput(nullptr);
  m_session->setAudioBufferInput(nullptr);
  const QFileInfo file(m_pending);
  bool audioValid = !m_failed && !m_discard && file.exists() && file.size() > 0;
  QString audioError;
  if (audioValid) {
    if (m_systemWriter.isOpen() && !m_systemWriter.finalize(&audioError)) audioValid = false;
    if (m_microphoneWriter.isOpen() && !m_microphoneWriter.finalize(&audioError)) audioValid = false;
    if (!audioValid) { m_failed = true; m_error = tr("Independent audio finalization failed: %1").arg(audioError); }
  }
  m_systemWriter.discard(); m_microphoneWriter.discard();
  if (!audioValid) {
    if (!m_pendingSystemAudio.isEmpty()) QFile::remove(m_pendingSystemAudio);
    if (!m_pendingMicrophoneAudio.isEmpty()) QFile::remove(m_pendingMicrophoneAudio);
  }
  if (m_discard) {
    // Only m_pending, atomically reserved by this active take, is owned here.
    // Never remove lastRecording or any project/user supplied source.
    if (file.exists() && !QFile::remove(m_pending))
      m_error = tr("无法删除本次未保存的录制文件。");
    m_status = tr("已放弃本次录制");
  } else if (!m_failed && file.exists() && file.size() > 0 &&
      m_recorder->recorderState() == QMediaRecorder::StoppedState) {
    m_last = QUrl::fromLocalFile(file.absoluteFilePath());
    m_lastSystemAudio = m_pendingSystemAudio.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_pendingSystemAudio);
    m_lastMicrophoneAudio = m_pendingMicrophoneAudio.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_pendingMicrophoneAudio);
    m_lastPointerSamples = m_pendingPointerSamples;
    m_lastMarkers = m_pendingMarkers;
    m_status = tr("已保存录制");
  } else {
    if (m_error.isEmpty())
      m_error = tr("录制未产生有效文件。");
    m_status = tr("录制未完成");
    if (file.exists() && file.size() == 0)
      QFile::remove(m_pending);
  }
  m_pendingPointerSamples.clear();
  m_pendingMarkers.clear();
  m_pointerClock.invalidate();
  m_pending.clear();
  m_pendingSystemAudio.clear(); m_pendingMicrophoneAudio.clear();
  m_busy = false;
  m_recording = false;
  m_paused = false;
  emit changed();
  if (m_discard)
    emit discarded();
  if (m_restart && !m_failed && m_error.isEmpty())
    emit restartRequested();
}
void RecordingController::openOutputDirectory() {
  if (!QDesktopServices::openUrl(
          QUrl::fromLocalFile(QDir(m_directory).absolutePath()))) {
    m_error = tr("无法打开输出目录。");
    emit changed();
  }
}
void RecordingController::openLastRecording() {
  if (m_last.isEmpty() || !QDesktopServices::openUrl(m_last)) {
    m_error = tr("无法打开上次录制。");
    emit changed();
  }
}

bool RecordingController::addRecordingMarker() {
  if (!m_recording || m_paused || m_stopRequested || !m_pointerClock.isValid())
    return false;
  const double time = qMax(0.0, (m_pointerClock.nsecsElapsed() - m_pausedNsecs) / 1.0e9);
  m_pendingMarkers.append(QVariantMap{
      {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
      {"sourceTime", time}, {"number", int(m_pendingMarkers.size()) + 1}});
  emit changed();
  return true;
}
