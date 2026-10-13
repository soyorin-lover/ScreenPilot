#include "TimelineSmokeTest.h"
#include "TimelineMedia.h"
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QTimer>
int verifyTimelineMedia(QGuiApplication &app, const QString &sourcePath,
                        const QString &outputDirectory) {
  Q_UNUSED(app);
  const QUrl source = QUrl::fromLocalFile(sourcePath);
  QMediaPlayer probe;
  QEventLoop metadata;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &metadata, &QEventLoop::quit);
  QObject::connect(&probe, &QMediaPlayer::mediaStatusChanged, &metadata,
                   [&](QMediaPlayer::MediaStatus status) {
                     if (status == QMediaPlayer::LoadedMedia ||
                         status == QMediaPlayer::InvalidMedia)
                       metadata.quit();
                   });
  QObject::connect(&probe, &QMediaPlayer::errorOccurred, &metadata,
                   &QEventLoop::quit);
  probe.setSource(source);
  timeout.start(10000);
  if (probe.mediaStatus() != QMediaPlayer::LoadedMedia)
    metadata.exec();
  const double seconds = probe.duration() / 1000.0;
  const bool hasAudio = probe.hasAudio();
  probe.setSource({});
  if (seconds <= 0)
    return 2;
  TimelineMedia media;
  // Queue replacement before the initial job can publish: stale generations
  // must not alter the replacement cache or leave it permanently busy.
  media.setSource(source, seconds);
  media.setSource({}, 0);
  media.setSource(source, seconds);
  QEventLoop decoding;
  bool timedOut = false;
  QObject::connect(&media, &TimelineMedia::changed, &decoding, [&] {
    if (!media.busy())
      decoding.quit();
  });
  QObject::disconnect(&timeout, nullptr, &metadata, nullptr);
  QObject::connect(&timeout, &QTimer::timeout, &decoding, [&] {
    timedOut = true;
    decoding.quit();
  });
  timeout.start(45000);
  if (media.busy())
    decoding.exec();
  int nonzero = 0;
  for (const auto &value : media.waveform())
    if (value.toDouble() > .001)
      ++nonzero;
  const int thumbnails = media.thumbnailCount();
  const bool passed = !timedOut && thumbnails > 0 && (!hasAudio || nonzero > 0);
  const QJsonObject result{{"passed", passed},
                           {"thumbnailCount", thumbnails},
                           {"sampleCount", media.waveform().size()},
                           {"nonzeroAudioSamples", nonzero},
                           {"hasAudio", hasAudio},
                           {"sourceReplacementCompleted", !media.busy()},
                           {"timedOut", timedOut}};
  const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Indented);
  qInfo().noquote() << bytes;
  if (!outputDirectory.isEmpty()) {
    if (!QDir().mkpath(outputDirectory))
      return 3;
    QFile report(QDir(outputDirectory).filePath("timeline-smoke.json"));
    if (!report.open(QIODevice::WriteOnly) ||
        report.write(bytes) != bytes.size())
      return 3;
  }
  return passed ? 0 : 4;
}
