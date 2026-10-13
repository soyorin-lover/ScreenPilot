#include "BackgroundVideo.h"
#include "FrameConversion.h"
#include <QFileInfo>
#include <QVideoFrame>
#include <cmath>
BackgroundVideo::BackgroundVideo(QObject *parent) : QObject(parent) {
  m_player.setVideoSink(&m_sink); // Never attach an audio output.
  m_timeout.setSingleShot(true);
  connect(&m_timeout, &QTimer::timeout, this, [this] {
    complete({}, QStringLiteral("动态背景解码超时。"));
  });
  connect(&m_player, &QMediaPlayer::errorOccurred, this,
          [this](QMediaPlayer::Error, const QString &error) {
    m_error = QStringLiteral("动态背景：") + error;
    complete({}, m_error);
  });
  connect(&m_player, &QMediaPlayer::mediaStatusChanged, this,
          [this](QMediaPlayer::MediaStatus status) {
    if (status == QMediaPlayer::LoadedMedia && !m_loaded) {
      m_loaded = true;
      if (!m_player.hasVideo() || m_player.duration() <= 0) {
        m_error = QStringLiteral("动态背景没有有效视频轨或时长。");
        complete({}, m_error);
      } else if (m_callback) seek();
    } else if (status == QMediaPlayer::EndOfMedia && m_callback) {
      complete({}, QStringLiteral("动态背景未产生所需时间的视频帧。"));
    }
  });
  connect(&m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
    if (!m_callback || !frame.isValid()) return;
    const qint64 begin = frame.startTime();
    const qint64 end = frame.endTime() > begin ? frame.endTime() : begin + 33333;
    if (begin < 0 || m_target < begin || m_target >= end) return;
    const QImage image = FrameConversion::toImage(frame);
    if (image.isNull()) return;
    m_begin = begin; m_end = end; m_image = image;
    complete(image);
  });
}
void BackgroundVideo::setPath(const QString &path) {
  if (m_path == path) return;
  m_callback = {}; m_timeout.stop(); m_player.stop();
  m_path = path; m_error.clear(); m_loaded = false;
  m_image = {}; m_begin = m_end = -1;
  if (path.isEmpty()) { m_player.setSource({}); return; }
  if (!QFileInfo(path).isFile()) m_error = QStringLiteral("动态背景视频不存在。");
  else m_player.setSource(QUrl::fromLocalFile(path));
}
void BackgroundVideo::request(double seconds, std::function<void(QImage, QString)> callback) {
  m_callback = std::move(callback); m_seconds = std::max(0.0, seconds);
  if (!m_error.isEmpty()) { complete({}, m_error); return; }
  if (m_path.isEmpty()) { complete({}); return; }
  m_timeout.start(10000);
  if (m_loaded) seek();
}
void BackgroundVideo::seek() {
  if (!m_callback || m_player.duration() <= 0) return;
  m_target = qint64(std::fmod(m_seconds * 1000000.0, double(m_player.duration()) * 1000.0));
  if (!m_image.isNull() && m_target >= m_begin && m_target < m_end) { complete(m_image); return; }
  m_player.pause();
  if (!m_callback) return;
  m_player.setPosition(m_target / 1000);
  if (m_callback) m_player.play();
}
void BackgroundVideo::complete(const QImage &image, const QString &error) {
  if (!m_callback) return;
  // Detach before calling the backend: pause may synchronously emit frame or
  // media-status signals. Those signals must not complete this request again.
  auto callback = std::move(m_callback); m_callback = {};
  m_timeout.stop(); m_player.pause();
  callback(image, error);
}
