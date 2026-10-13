#pragma once
#include <QObject>
#include <QImage>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QTimer>
#include <functional>

// A silent, one-frame cache. Requests are asynchronous and memory is independent
// of video duration. Times are on the edited output timeline, looping the asset.
class BackgroundVideo final : public QObject {
  Q_OBJECT
public:
  explicit BackgroundVideo(QObject *parent = nullptr);
  void setPath(const QString &path);
  void request(double outputSeconds, std::function<void(QImage, QString)> callback);
  QString path() const { return m_path; }
private:
  void seek();
  void complete(const QImage &image, const QString &error = {});
  QMediaPlayer m_player;
  QVideoSink m_sink;
  QTimer m_timeout;
  QString m_path, m_error;
  QImage m_image;
  qint64 m_begin = -1, m_end = -1, m_target = 0;
  double m_seconds = 0;
  bool m_loaded = false;
  std::function<void(QImage, QString)> m_callback;
};
