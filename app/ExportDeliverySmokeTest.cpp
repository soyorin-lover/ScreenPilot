#include "ExportDeliverySmokeTest.h"
#include "ExportController.h"
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QTimer>
#include <QVideoSink>
#include <QVideoFrame>
#include <functional>
namespace {
bool waitFor(const std::function<bool()> &done, int timeout = 45000) {
  QEventLoop loop; QTimer poll, deadline;
  poll.setInterval(10); deadline.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start(); deadline.start(timeout); if (!done()) loop.exec(); return done();
}
QByteArray hash(const QString &path) {
  QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash digest(QCryptographicHash::Sha256);
  if (!digest.addData(&file)) return {}; return digest.result();
}
}
int runExportDeliverySmokeTest(QGuiApplication &, const QString &directory, const QString &source) {
  QDir().mkpath(directory); QDir out(directory);
  ExportController exporter;
  const QString encoded = out.filePath("delivery-1080p-60.mp4");
  exporter.start(QUrl::fromLocalFile(source),
      {QVariantMap{{"sourceStart", 0.0}, {"sourceDuration", 0.5}, {"playbackRate", 1.0}}},
      QUrl::fromLocalFile(encoded), {}, {}, {{"resolution", "1080p"}, {"fps", 60}, {"audioMode", "muted"}});
  const bool encodedDone = waitFor([&] { return !exporter.busy(); });
  const QString encodeError = exporter.error();
  QMediaPlayer player; QVideoSink sink; player.setVideoSink(&sink);
  QSize decodedSize; int count = 0; qint64 previous = -1, delta = 0;
  QObject::connect(&sink, &QVideoSink::videoFrameChanged, &player, [&](const QVideoFrame &frame) {
    if (!frame.isValid()) return; decodedSize = frame.size(); ++count;
    if (previous >= 0) delta = frame.startTime() - previous;
    previous = frame.startTime();
  });
  player.setSource(QUrl::fromLocalFile(encoded)); player.play();
  const bool decoded = waitFor([&] { return count >= 3 || player.error() != QMediaPlayer::NoError; }, 15000);
  player.stop();
  exporter.startOriginal(QUrl::fromLocalFile(source), QUrl::fromLocalFile(out.absolutePath()), QUrl::fromLocalFile(source));
  const bool copyDone = waitFor([&] { return !exporter.busy(); });
  const QDir bundle(exporter.lastExport().toLocalFile());
  const QString suffix = QFileInfo(source).suffix();
  const QByteArray original = hash(source);
  const bool copied = copyDone && exporter.error().isEmpty() && !original.isEmpty() &&
      original == hash(bundle.filePath("screen." + suffix)) && original == hash(bundle.filePath("camera." + suffix));
  const QStringList beforeCancel = out.entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
  exporter.startOriginal(QUrl::fromLocalFile(source), QUrl::fromLocalFile(out.absolutePath()));
  const bool beganCopy = exporter.busy();
  exporter.cancel();
  QGuiApplication::processEvents();
  const bool cancelledCleanly = beganCopy && !exporter.busy() &&
      out.entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot) == beforeCancel &&
      original == hash(bundle.filePath("screen." + suffix));
  exporter.startOriginal(QUrl::fromLocalFile(source), QUrl::fromLocalFile(out.absolutePath()),
      QUrl::fromLocalFile(out.filePath("missing-camera.mp4")));
  const bool missingRejected = !exporter.busy() && !exporter.error().isEmpty() &&
      out.entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot) == beforeCancel;
  const bool videoOk = encodedDone && encodeError.isEmpty() && decoded && decodedSize == QSize(1920,1080) && delta >= 16000 && delta <= 17000;
  QJsonObject report{{"pass", videoOk && copied && cancelledCleanly && missingRejected}, {"encodedSize", QString("%1x%2").arg(decodedSize.width()).arg(decodedSize.height())},
      {"frameDeltaUs", double(delta)}, {"encodedError", encodeError}, {"sourceBundleBytesEqual", copied}, {"cancelCleanup", cancelledCleanly}, {"missingCameraRejectedWithoutOrphans", missingRejected}};
  QFile reportFile(out.filePath("export-delivery-smoke.json"));
  if (reportFile.open(QIODevice::WriteOnly)) reportFile.write(QJsonDocument(report).toJson());
  return videoOk && copied && cancelledCleanly && missingRejected ? 0 : 1;
}
