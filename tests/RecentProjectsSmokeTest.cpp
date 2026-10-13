#include "RecentProjectsSmokeTest.h"
#include "RecentProjectsController.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

int runRecentProjectsSmokeTest(QGuiApplication &, const QString &directory) {
  QDir().mkpath(directory);
  QTemporaryDir isolated(QDir(directory).filePath("recent-XXXXXX"));
  if (!isolated.isValid()) return 2;
  const QString registry = QDir(isolated.path()).filePath("history.json");
  const auto fixture = [&](int index, const QString &producer = "ScreenPilot", int version = 7) {
    const QString folder = QDir(isolated.path()).filePath(QString("project-%1").arg(index));
    QDir().mkpath(folder);
    QFile file(QDir(folder).filePath("project.json"));
    if (!file.open(QIODevice::WriteOnly)) return QUrl();
    file.write(QJsonDocument(QJsonObject{{"producer", producer}, {"formatVersion", version}}).toJson());
    return QUrl::fromLocalFile(folder);
  };
  RecentProjectsController store(nullptr, registry);
  bool registered = true;
  QList<QUrl> urls;
  for (int i = 0; i < 23; ++i) {
    urls.append(fixture(i));
    registered = store.remember(urls.last(), QString("Fixture %1").arg(i)) && registered;
  }
  const bool capped = store.projects().size() == 20 && store.recentProjects().size() == 8;
  const bool ordered = store.projects().first().toMap().value("url").toUrl() == urls.last();
  bool deduplicated = store.remember(urls[10], "Renamed") && store.projects().size() == 20 &&
      store.projects().first().toMap().value("title").toString() == "Renamed";
#ifdef Q_OS_WIN
  deduplicated = store.remember(QUrl::fromLocalFile(urls[10].toLocalFile().toUpper()), "Case") &&
      store.projects().size() == 20 && deduplicated;
#endif
  RecentProjectsController restarted(nullptr, registry);
  const bool persisted = restarted.projects() == store.projects();
  QFile::remove(QDir(urls[10].toLocalFile()).filePath("project.json"));
  restarted.refresh();
  const bool missingRetained = restarted.projects().size() == 20 &&
      !restarted.projects().first().toMap().value("exists").toBool();
  const bool rejected = !restarted.remember(fixture(30, "Other")) &&
      !restarted.remember(fixture(31, "ScreenPilot", 8)) && restarted.projects().size() == 20;
  const bool forgot = restarted.removeEntry(urls.last()) &&
      QFile::exists(QDir(urls.last().toLocalFile()).filePath("project.json"));
  const bool cleared = restarted.clearHistory() && restarted.projects().isEmpty() &&
      QFile::exists(QDir(urls[15].toLocalFile()).filePath("project.json"));
  RecentProjectsController emptyRestart(nullptr, registry);
  const bool emptyPersisted = emptyRestart.projects().isEmpty();
  const bool passed = registered && capped && ordered && deduplicated && persisted && missingRetained && rejected && forgot && cleared && emptyPersisted;
  QFile report(QDir(directory).filePath("recent-projects-smoke.json"));
  if (report.open(QIODevice::WriteOnly)) report.write(QJsonDocument(QJsonObject{
      {"passed", passed}, {"registration", registered}, {"limits20and8", capped}, {"mostRecentFirst", ordered},
      {"canonicalCaseDeduplication", deduplicated}, {"restart", persisted}, {"missingRetained", missingRetained},
      {"unsupportedRejected", rejected}, {"forgetPreservesFiles", forgot}, {"clearPreservesFiles", cleared},
      {"emptyRestart", emptyPersisted}}).toJson());
  return passed ? 0 : 1;
}
