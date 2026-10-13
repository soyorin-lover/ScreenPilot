#include "TimelineMedia.h"
#include "FrameConversion.h"
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QColor>
#include <QMediaPlayer>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>
#include <cmath>
#include <functional>
struct TimelineCache {
  QMutex mutex;
  QVector<QImage> images;
};
namespace {

class Provider final : public QQuickImageProvider {
  std::shared_ptr<TimelineCache> cache;

public:
  explicit Provider(std::shared_ptr<TimelineCache> c)
      : QQuickImageProvider(Image), cache(std::move(c)) {}
  QImage requestImage(const QString &id, QSize *size, const QSize &) override {
    QMutexLocker lock(&cache->mutex);
    const int index = id.section('/', 0, 0).toInt();
    QImage result = cache->images.value(index);
    if (result.isNull()) {
      result = QImage(160, 90, QImage::Format_RGB32);
      result.fill(QColor("#25354a"));
    }
    if (size)
      *size = result.size();
    return result;
  }
};
} // namespace
TimelineMedia::TimelineMedia(QObject *parent)
    : QObject(parent), m_cache(std::make_shared<TimelineCache>()) {
  m_worker = new QObject;
  m_worker->moveToThread(&m_thread);
  connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
  m_thread.start(QThread::LowPriority);
}
TimelineMedia::~TimelineMedia() {
  m_thread.quit();
  m_thread.wait();
}
int TimelineMedia::thumbnailCount() const {
  QMutexLocker lock(&m_cache->mutex);
  return std::count_if(m_cache->images.cbegin(), m_cache->images.cend(),
                       [](const QImage &image) { return !image.isNull(); });
}
QQuickImageProvider *TimelineMedia::imageProvider() {
  return new Provider(m_cache);
}
QString TimelineMedia::thumbnail(double t) const {
  const int index = m_duration > 0
                        ? std::clamp(int(t / m_duration * m_sampleCount), 0,
                                     m_sampleCount - 1)
                        : 0;
  return QString("image://timeline/%1/%2").arg(index).arg(m_revision);
}
double TimelineMedia::amplitude(double t) const {
  if (m_duration <= 0 || m_waveform.isEmpty())
    return 0;
  return m_waveform
      .value(
          std::clamp(int(t / m_duration * m_sampleCount), 0, m_sampleCount - 1))
      .toDouble();
}
void TimelineMedia::releaseMedia() {
  setSource({}, 0);
  // setSource queues decoder destruction on its owning thread. A FIFO barrier
  // acknowledges that close before the caller renames the project directory.
  QMetaObject::invokeMethod(m_worker, [] {}, Qt::BlockingQueuedConnection);
}
void TimelineMedia::setSource(QUrl source, double duration) {
  if (source == m_source && duration == m_duration)
    return;
  m_source = source;
  m_duration = duration;
  m_sampleCount = std::isfinite(duration) && duration > 0
                      ? int(std::clamp(std::ceil(duration * 2), 6.0, 96.0))
                      : 6;
  const int count = m_sampleCount;
  const int generation = ++m_generation;
  ++m_revision;
  m_waveform.clear();
  for (int i = 0; i < count; ++i)
    m_waveform.append(0.0);
  m_pendingWaveform = m_waveform;
  m_progress = 0;
  emit progressChanged();
  {
    QMutexLocker lock(&m_cache->mutex);
    m_cache->images.clear();
    m_cache->images.resize(count);
  }
  m_busy = source.isLocalFile() && std::isfinite(duration) && duration > 0;
  emit changed();
  const bool valid = m_busy;
  QMetaObject::invokeMethod(m_worker, [this, source, duration, generation,
                                       valid, count] {
    // All decoder objects live in a low-priority thread. Replacing a source
    // destroys its player and disconnects every pending decode callback.
    const auto children = m_worker->children();
    for (auto *child : children)
      delete child;
    if (!valid)
      return;
    auto *job = new QObject(m_worker);
    auto *player = new QMediaPlayer(job);
    auto *sink = new QVideoSink(job);
    auto *audio = new QAudioBufferOutput(job);
    auto *timer = new QTimer(job);
    timer->setSingleShot(true);
    player->setVideoSink(sink);
    player->setAudioBufferOutput(audio);
    player->setAudioOutput(nullptr);
    struct State {
      int index = -1;
      qint64 target = 0;
      bool image = false, advancing = false;
      qint64 audioEnd = -1; // ms covered by received audio
      double peak = 0;
    };
    auto state = std::make_shared<State>();
    auto advance = std::make_shared<std::function<void()>>();
    *advance = [this, player, timer, job, state, duration, generation, count] {
      if (state->index >= 0) {
        const int index = state->index;
        const double peak = state->peak;
        QMetaObject::invokeMethod(this, [this, generation, index, peak, count] {
          if (generation != m_generation)
            return;
          m_pendingWaveform[index] = peak;
          m_progress = double(index + 1) / count;
          emit progressChanged();
        });
      }
      if (++state->index >= count) {
        player->stop();
        QMetaObject::invokeMethod(this, [this, generation] {
          if (generation == m_generation) {
            m_waveform = m_pendingWaveform;
            ++m_revision;
            m_busy = false;
            m_progress = 1;
            emit progressChanged();
            emit changed();
          }
        });
        job->deleteLater();
        return;
      }
      state->image = false;
      state->advancing = false;
      state->audioEnd = -1;
      state->peak = 0;
      state->target = qint64(duration * 1000 * (state->index + .5) / count);
      player->setPosition(state->target);
      player->play();
      timer->start(350);
    };
    connect(timer, &QTimer::timeout, job, [advance] { (*advance)(); });
    auto ready = std::make_shared<std::function<void()>>();
    *ready = [state, player, timer, advance, job] {
      const bool audioDone = !player->hasAudio() || state->audioEnd >= state->target + 150;
      if (state->advancing || !state->image || !audioDone) return;
      state->advancing = true;
      timer->stop();
      // Queued: never re-seek from inside a decoder callback.
      QMetaObject::invokeMethod(job, [advance] { (*advance)(); }, Qt::QueuedConnection);
    };
    connect(sink, &QVideoSink::videoFrameChanged, job,
            [this, state, generation, count, ready](const QVideoFrame &frame) {
              if (state->index < 0 || state->index >= count || state->image ||
                  !frame.isValid())
                return;
              if (frame.startTime() >= 0 &&
                  std::abs(frame.startTime() / 1000 - state->target) > 100)
                return;
              QImage image = FrameConversion::toImage(frame).scaled(
                  160, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation);
              if (image.isNull())
                return;
              state->image = true;
              // Stored now, shown with the single revision change at the end.
              QMetaObject::invokeMethod(
                  this, [this, generation, index = state->index, image] {
                    if (generation != m_generation)
                      return;
                    QMutexLocker lock(&m_cache->mutex);
                    m_cache->images[index] = image;
                  });
              (*ready)();
            });
    connect(audio, &QAudioBufferOutput::audioBufferReceived, job,
            [state, ready](const QAudioBuffer &buffer) {
              if (state->index < 0 || !buffer.isValid() ||
                  buffer.startTime() / 1000 < state->target - 100)
                return;
              const auto format = buffer.format();
              for (int i = 0; i < buffer.sampleCount(); ++i) {
                double sample = 0;
                switch (format.sampleFormat()) {
                case QAudioFormat::Float:
                  sample = buffer.constData<float>()[i];
                  break;
                case QAudioFormat::Int16:
                  sample = buffer.constData<qint16>()[i] / 32768.0;
                  break;
                case QAudioFormat::Int32:
                  sample = buffer.constData<qint32>()[i] / 2147483648.0;
                  break;
                case QAudioFormat::UInt8:
                  sample = (int(buffer.constData<quint8>()[i]) - 128) / 128.0;
                  break;
                default:
                  break;
                }
                state->peak =
                    std::max(state->peak, std::min(1.0, std::abs(sample)));
              }
              state->audioEnd = std::max(state->audioEnd, (buffer.startTime() + buffer.duration()) / 1000);
              (*ready)();
            });
    connect(player, &QMediaPlayer::mediaStatusChanged, job,
            [advance, state](QMediaPlayer::MediaStatus status) {
              if (status == QMediaPlayer::LoadedMedia && state->index < 0)
                (*advance)();
            });
    connect(player, &QMediaPlayer::errorOccurred, job, [this, generation, job] {
      QMetaObject::invokeMethod(this, [this, generation] {
        if (generation == m_generation) {
          m_busy = false;
          m_progress = 1;
          emit progressChanged();
          emit changed();
        }
      });
      job->deleteLater();
    });
    player->setSource(source);
  });
}
