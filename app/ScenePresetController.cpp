#include "ScenePresetController.h"
#include "ProjectController.h"
#include "CanvasRenderer.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validId(const QString &id) { return !QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id; }
// Publish a complete asset under a fresh name, without overwriting existing files.
bool copyAsset(const QString &source, const QString &target) {
  if (QFileInfo::exists(target)) return QFileInfo(target).isFile();
  QFile input(source);
  if (!input.open(QIODevice::ReadOnly)) return false;
  const QString staging = target + "." + uuid() + ".tmp";
  QFile output(staging);
  if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
  bool ok = true;
  while (!input.atEnd()) {
    const auto bytes = input.read(1024 * 1024);
    if (bytes.isEmpty() && input.error() != QFile::NoError) { ok = false; break; }
    if (output.write(bytes) != bytes.size()) { ok = false; break; }
  }
  ok = output.flush() && ok;
  output.close();
  if (ok) ok = output.rename(target);
  if (!ok) QFile::remove(staging);
  return ok;
}
struct NewAssetCopies {
  QStringList paths;
  bool keep = false;
  ~NewAssetCopies() { if (!keep) for (const auto &path : paths) QFile::remove(path); }
};
bool copyStickerAssets(QVariantMap &canvas, const QString &folder, bool hashed,
                       QStringList &created) {
  if (canvas.contains("stickers") && canvas.value("stickers").metaType().id()!=QMetaType::QVariantList) return false;
  auto stickers = canvas.value("stickers").toList();
  if (stickers.isEmpty()) return true;
  if (!QDir().mkpath(folder) || QFileInfo(folder).isSymLink()) return false;
  const QString base = QFileInfo(QFileInfo(folder).absolutePath()).canonicalFilePath();
  const QString canonical = QFileInfo(folder).canonicalFilePath();
  if (base.isEmpty() || canonical.isEmpty() || !canonical.startsWith(base+"/", Qt::CaseInsensitive)) return false;
  for (auto &value : stickers) {
    auto sticker = value.toMap();
    const QString source = sticker.value("imagePath").toString();
    QImageReader reader(source);
    if (!QFileInfo(source).isFile() || reader.supportsAnimation() || reader.read().isNull()) return false;
    QString name = uuid();
    if (hashed) {
      QFile input(source);
      if (!input.open(QIODevice::ReadOnly)) return false;
      QCryptographicHash hash(QCryptographicHash::Sha256);
      if (!hash.addData(&input)) return false;
      name = QString::fromLatin1(hash.result().toHex());
    }
    const QString suffix = QFileInfo(source).suffix().toLower();
    const QString target = QDir(canonical).filePath(name + (suffix.isEmpty()?QString{}:"."+suffix));
    const bool existed = QFileInfo::exists(target);
    if (QFileInfo(target).isSymLink() || !copyAsset(source, target)) return false;
    if (!existed) created.append(target);
    sticker["imagePath"] = target; value = sticker;
  }
  canvas["stickers"] = stickers;
  return true;
}
}
ScenePresetController::ScenePresetController(ProjectController *project, QString directory, QObject *parent)
    : QObject(parent), m_project(project), m_directory(directory.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("scene-presets") : directory) {
  QFile file(QDir(m_directory).filePath("presets.json"));
  if (!file.exists()) return;
  if (!file.open(QIODevice::ReadOnly)) { m_error = tr("无法读取场景预设。"); return; }
  QJsonParseError parse;
  const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject()) { m_error = tr("场景预设文件无效。"); return; }
  const auto root = doc.object().toVariantMap();
  for (const auto &entry : root.value("presets").toList())
    if (validId(entry.toMap().value("id").toString())) m_presets.append(entry);
  const QString selected = root.value("defaultId").toString();
  if (indexOf(selected) >= 0) m_default = selected;
}
int ScenePresetController::indexOf(const QString &id) const {
  for (int i = 0; i < m_presets.size(); ++i) if (m_presets[i].toMap().value("id").toString() == id) return i;
  return -1;
}
bool ScenePresetController::fail(QString message) { m_error = message; emit changed(); return false; }
bool ScenePresetController::persist(const QVariantList &presets, const QString &defaultId) {
  if (!QDir().mkpath(m_directory)) return fail(tr("无法创建场景预设目录。"));
  QSaveFile file(QDir(m_directory).filePath("presets.json"));
  const auto bytes = QJsonDocument(QJsonObject::fromVariantMap({{"version", 1}, {"defaultId", defaultId}, {"presets", presets}})).toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return fail(tr("无法保存场景预设。"));
  m_presets = presets; m_default = defaultId; m_error.clear(); emit changed(); return true;
}
bool ScenePresetController::savePreset(QString name) { return capture(uuid(), name); }
bool ScenePresetController::updatePreset(QString id, QString name) {
  if (indexOf(id) < 0) return fail(tr("场景预设已不存在。"));
  return capture(id, name);
}
bool ScenePresetController::capture(QString id, QString name) {
  name = name.trimmed();
  if (!m_project || !m_project->hasProject()) return fail(tr("请先载入录制再保存场景。"));
  if (name.isEmpty()) return fail(tr("请输入场景名称。"));
  for (const auto &entry : m_presets) {
    const auto row = entry.toMap();
    if (row.value("id").toString() != id && row.value("name").toString().compare(name, Qt::CaseInsensitive) == 0) return fail(tr("已存在同名场景。"));
  }
  auto settings = m_project->sceneSettings();
  auto canvas = settings.value("canvas").toMap();
  const QString key = canvas.value("backgroundVideo").toString().isEmpty() ? "backgroundImage" : "backgroundVideo";
  const QString background = canvas.value(key).toString();
  QString copied;
  NewAssetCopies stickerCopies;
  if (!background.isEmpty()) {
    const QString folder = QDir(m_directory).filePath(id);
    if (!QDir().mkpath(folder)) return fail(tr("无法保存背景图片副本。"));
    copied = QDir(folder).filePath(uuid() + "." + QFileInfo(background).suffix());
    if (!copyAsset(background, copied)) return fail(tr("无法保存背景图片副本。"));
    canvas[key] = copied; settings["canvas"] = canvas;
  }
  if (!copyStickerAssets(canvas, QDir(m_directory).filePath(id), false, stickerCopies.paths)) {
    if (!copied.isEmpty()) QFile::remove(copied);
    return fail(tr("Cannot preserve scene sticker images"));
  }
  settings["canvas"] = canvas;
  auto list = m_presets;
  const QVariantMap row{{"id", id}, {"name", name}, {"settings", settings}};
  const int index = indexOf(id);
  if (index < 0) list.append(row); else list[index] = row;
  if (!persist(list, m_default)) { if (!copied.isEmpty()) QFile::remove(copied); return false; }
  stickerCopies.keep = true;
  // Old revisions remain until deletion so a failed save never invalidates the previous snapshot.
  m_active = id; emit changed(); return true;
}
bool ScenePresetController::deletePreset(QString id) {
  const int index = indexOf(id);
  if (index < 0 || !validId(id)) return fail(tr("场景预设已不存在。"));
  auto list = m_presets; list.removeAt(index);
  if (!persist(list, m_default == id ? QString{} : m_default)) return false;
  if (m_active == id) m_active.clear();
  const QString folder = QDir(m_directory).filePath(id);
  // Only the validated UUID child belonging to this record can be deleted.
  if (!QFileInfo(folder).isSymLink()) QDir(folder).removeRecursively();
  emit changed(); return true;
}
bool ScenePresetController::setDefaultPreset(QString id) {
  if (!id.isEmpty() && indexOf(id) < 0) return fail(tr("场景预设已不存在。"));
  return persist(m_presets, id);
}
bool ScenePresetController::applyDefault() { return m_default.isEmpty() || applyPreset(m_default); }
bool ScenePresetController::applyPreset(QString id) {
  const int index = indexOf(id);
  if (index < 0 || !m_project || !m_project->hasProject()) return fail(tr("请先载入录制并选择场景。"));
  auto settings = m_presets[index].toMap().value("settings").toMap();
  auto canvas = CanvasRenderer::defaults();
  const auto supplied = settings.value("canvas").toMap();
  for (auto it = supplied.cbegin(); it != supplied.cend(); ++it) canvas[it.key()] = it.value();
  settings["canvas"] = canvas;
  NewAssetCopies stickerCopies;
  const QString key = canvas.value("backgroundVideo").toString().isEmpty() ? "backgroundImage" : "backgroundVideo";
  const QString background = canvas.value(key).toString();
  if (!background.isEmpty()) {
    QFile input(background);
    if (!input.open(QIODevice::ReadOnly)) return fail(tr("场景背景图片不可用。"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input)) return fail(tr("无法读取场景背景图片。"));
    const QString cache = QDir(m_directory).filePath("applied-assets");
    if (!QDir().mkpath(cache)) return fail(tr("无法保存应用后的背景图片副本。"));
    const QString target = QDir(cache).filePath(QString::fromLatin1(hash.result().toHex()) + "." + QFileInfo(background).suffix());
    const bool existed = QFileInfo::exists(target);
    if (!copyAsset(background, target)) return fail(tr("无法保存应用后的背景图片副本。"));
    if (!existed) stickerCopies.paths.append(target);
    canvas[key] = target; settings["canvas"] = canvas;
  }
  if (!copyStickerAssets(canvas, QDir(m_directory).filePath("applied-assets"), true, stickerCopies.paths))
    return fail(tr("Cannot preserve applied sticker images"));
  settings["canvas"] = canvas;
  if (!m_project->applySceneSettings(settings)) return fail(m_project->error());
  stickerCopies.keep = true;
  m_active = id; m_error.clear(); emit changed(); return true;
}

