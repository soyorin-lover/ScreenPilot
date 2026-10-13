#include "RecentProjectsController.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

RecentProjectsController::RecentProjectsController(QObject *parent, const QString &settingsFile)
    : QObject(parent), m_settingsFile(settingsFile.isEmpty()
          ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("recent-projects.json")
          : settingsFile) {
  QFile file(m_settingsFile);
  if (!file.exists()) return;
  if (!file.open(QIODevice::ReadOnly)) { fail(tr("无法读取最近工程记录。")); return; }
  QJsonParseError parse;
  const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject() ||
      doc.object().value("version").toInt() != 1 || !doc.object().value("projects").isArray()) {
    fail(tr("最近工程记录格式无效。")); return;
  }
  QSet<QString> seen;
  for (const auto value : doc.object().value("projects").toArray()) {
    const auto object = value.toObject();
    const QString path = object.value("path").toString();
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) continue;
    const QString key = QDir::cleanPath(path).toCaseFolded();
    if (seen.contains(key)) continue;
    seen.insert(key);
    m_entries.append({QDir::cleanPath(path), object.value("title").toString()});
    if (m_entries.size() == 20) break;
  }
}

QString RecentProjectsController::normalized(const QUrl &folder) const {
  if (!folder.isLocalFile()) return {};
  const QFileInfo info(folder.toLocalFile());
  const QString canonical = info.canonicalFilePath();
  return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QVariantList RecentProjectsController::projects() const {
  QVariantList result;
  for (const auto &entry : m_entries) {
    const QFileInfo info(entry.path);
    const bool exists = info.isDir() && QFileInfo(QDir(entry.path).filePath("project.json")).isFile();
    result.append(QVariantMap{{"title", entry.title.isEmpty() ? info.fileName() : entry.title},
                             {"url", QUrl::fromLocalFile(entry.path)}, {"exists", exists}});
  }
  return result;
}
QVariantList RecentProjectsController::recentProjects() const { return projects().mid(0, 8); }
bool RecentProjectsController::fail(const QString &message) {
  if (m_error != message) { m_error = message; emit errorChanged(); }
  return false;
}
void RecentProjectsController::clearError() {
  if (!m_error.isEmpty()) { m_error.clear(); emit errorChanged(); }
}
bool RecentProjectsController::persist(const QList<Entry> &entries) {
  if (!QDir().mkpath(QFileInfo(m_settingsFile).absolutePath())) return fail(tr("无法创建最近工程记录目录。"));
  QJsonArray array;
  for (const auto &entry : entries) array.append(QJsonObject{{"path", entry.path}, {"title", entry.title}});
  QSaveFile file(m_settingsFile);
  const QByteArray data = QJsonDocument(QJsonObject{{"version", 1}, {"projects", array}}).toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    return fail(tr("无法保存最近工程记录。"));
  m_entries = entries;
  clearError();
  emit projectsChanged();
  return true;
}
bool RecentProjectsController::remember(const QUrl &folder, const QString &title) {
  const QString path = normalized(folder);
  if (path.isEmpty() || !QFileInfo(path).isDir()) return fail(tr("工程文件夹不可用。"));
  QFile file(QDir(path).filePath("project.json"));
  if (!file.open(QIODevice::ReadOnly)) return fail(tr("无法读取工程文件。"));
  QJsonParseError parse;
  const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
  const auto object = doc.object();
  const double version = object.value("formatVersion").toDouble(-1);
  if (parse.error != QJsonParseError::NoError || !doc.isObject() ||
      object.value("producer").toString() != "ScreenPilot" || version < 1 || version > 8 || version != int(version))
    return fail(tr("仅支持 ScreenPilot 版本 1–8 的工程。"));
  auto entries = m_entries;
  entries.removeIf([&](const Entry &entry) { return entry.path.compare(path, Qt::CaseInsensitive) == 0; });
  entries.prepend({path, title.trimmed().isEmpty() ? QFileInfo(path).fileName() : title.trimmed()});
  while (entries.size() > 20) entries.removeLast();
  return persist(entries);
}
bool RecentProjectsController::removeEntry(const QUrl &folder) {
  const QString path = normalized(folder);
  auto entries = m_entries;
  entries.removeIf([&](const Entry &entry) { return entry.path.compare(path, Qt::CaseInsensitive) == 0; });
  return persist(entries);
}
bool RecentProjectsController::clearHistory() { return persist({}); }
void RecentProjectsController::refresh() { emit projectsChanged(); }
