#include "RecoveryController.h"
#include "ProjectPackage.h"
#include "ProjectController.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

RecoveryController::RecoveryController(ProjectController *project, QString directory, QObject *parent)
    : QObject(parent), m_project(project), m_directory(directory.isEmpty()
          ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("recovery")
          : QDir(directory).absolutePath()) {
  m_enabled = QSettings().value("recovery/enabled", true).toBool();
  m_timer.setSingleShot(true);
  m_timer.setInterval(60000);
  connect(&m_timer, &QTimer::timeout, this, [this] { snapshotNow(); });
  connect(project, &ProjectController::changed, this, [this] {
    if (m_writing) return;
    if (m_enabled && !m_blocked && m_project->dirty()) m_timer.start();
    else m_timer.stop();
  });
}
void RecoveryController::setEnabled(bool value) {
  if (m_enabled == value) return;
  m_enabled = value;
  QSettings().setValue("recovery/enabled", value);
  if (value && !m_blocked && m_project->dirty()) m_timer.start();
  else m_timer.stop();
  emit changed();
}
void RecoveryController::setBlocked(bool value) {
  if (m_blocked == value) return;
  m_blocked = value;
  if (value) m_timer.stop();
  else if (m_enabled && m_project->dirty()) m_timer.start();
  emit changed();
}
bool RecoveryController::owns(const QString &path) const {
  const QFileInfo info(path);
  const QString base = QFileInfo(m_directory).canonicalFilePath();
  const QString name = info.fileName();
  if (base.isEmpty() || info.isSymLink() || !info.isDir() ||
      info.canonicalPath().compare(base, Qt::CaseInsensitive) != 0 ||
      ProjectPackage::suffixOf(name).isEmpty()) return false;
  const QString id = name.chopped(ProjectPackage::suffixOf(name).size());
  if (QUuid(id).isNull() || QUuid(id).toString(QUuid::WithoutBraces) != id) return false;
  QFile marker(QDir(path).filePath(".screenpilot-recovery"));
  return marker.open(QIODevice::ReadOnly) && marker.readAll() == id.toUtf8();
}
QVariantList RecoveryController::snapshots() const {
  QVariantList result;
  const auto entries = QDir(m_directory).entryInfoList({"*" + ProjectPackage::suffix(), "*" + ProjectPackage::legacySuffix()}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
  for (const auto &entry : entries) {
    if (!owns(entry.absoluteFilePath()) || !QFileInfo(QDir(entry.absoluteFilePath()).filePath("project.json")).isFile()) continue;
    result.append(QVariantMap{{"url", QUrl::fromLocalFile(entry.absoluteFilePath())},
                              {"modified", entry.lastModified().toUTC().toString(Qt::ISODate)},
                              {"name", tr("自动恢复工程")}});
  }
  return result;
}
bool RecoveryController::snapshotNow() {
  if (m_blocked || m_writing || !m_project->hasProject()) return false;
  if (!QDir().mkpath(m_directory)) { m_error = tr("无法创建恢复目录"); emit changed(); return false; }
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString path = QDir(m_directory).filePath(id + ProjectPackage::suffix());
  m_writing = true;
  const bool saved = m_project->saveRecovery(QUrl::fromLocalFile(path));
  m_writing = false;
  if (!saved) { m_error = m_project->error(); emit changed(); return false; }
  QFile marker(QDir(path).filePath(".screenpilot-recovery"));
  if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly) || marker.write(id.toUtf8()) != id.size()) {
    m_error = tr("无法登记恢复快照"); emit changed(); return false;
  }
  marker.close();
  m_last = QUrl::fromLocalFile(path);
  m_error.clear();
  // Only owned snapshots are collected; unrelated directories are never touched.
  const auto entries = snapshots();
  for (int i = 3; i < entries.size(); ++i) removeSnapshot(entries[i].toMap().value("url").toUrl());
  emit changed();
  return true;
}
bool RecoveryController::restoreSnapshot(QUrl url) {
  if (m_blocked || !url.isLocalFile() || !owns(url.toLocalFile())) return false;
  // ProjectController enforces dirty-project protection and validates all media.
  const bool ok = m_project->openProject(url);
  m_error = ok ? QString() : m_project->error();
  emit changed();
  return ok;
}
bool RecoveryController::removeSnapshot(QUrl url) {
  if (!url.isLocalFile() || !owns(url.toLocalFile())) return false;
  if (m_project->projectLocation().isLocalFile() &&
      QFileInfo(m_project->projectLocation().toLocalFile()).canonicalFilePath().compare(
          QFileInfo(url.toLocalFile()).canonicalFilePath(), Qt::CaseInsensitive) == 0) return false;
  const bool ok = QDir(url.toLocalFile()).removeRecursively();
  if (!ok) m_error = tr("无法清理恢复快照");
  emit changed();
  return ok;
}
