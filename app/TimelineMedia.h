#pragma once
#include <QObject>
#include <QQuickImageProvider>
#include <QThread>
#include <QUrl>
#include <QVariantList>
#include <memory>
struct TimelineCache;
class TimelineMedia final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int revision READ revision NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QVariantList waveform READ waveform NOTIFY changed)
public:
  explicit TimelineMedia(QObject *parent = nullptr);
  ~TimelineMedia() override;
  QQuickImageProvider *imageProvider();
  Q_INVOKABLE void setSource(QUrl source, double duration);
  // Synchronously destroys worker decoders before moving project media on Windows.
  Q_INVOKABLE void releaseMedia();
  Q_INVOKABLE QString thumbnail(double sourceSeconds) const;
  Q_INVOKABLE double amplitude(double sourceSeconds) const;
  int thumbnailCount() const;
  int revision() const { return m_revision; }
  bool busy() const { return m_busy; }
  QVariantList waveform() const { return m_waveform; }
signals:
  void changed();

private:
  std::shared_ptr<TimelineCache> m_cache;
  QThread m_thread;
  QObject *m_worker = nullptr;
  QUrl m_source;
  double m_duration = 0;
  int m_revision = 0, m_generation = 0, m_sampleCount = 6;
  bool m_busy = false;
  QVariantList m_waveform;
};
