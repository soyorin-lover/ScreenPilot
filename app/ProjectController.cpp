#include "ProjectController.h"
#include "AutoFraming.h"
#include "CameraOverlay.h"
#include "CameraMotion.h"
#include "CanvasRenderer.h"
#include <QDir>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <cmath>
namespace {
QString uid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validTime(double v) { return std::isfinite(v); }
} // namespace
ProjectController::ProjectController(QObject *parent)
    : QObject(parent), m_canvasSettings(CanvasRenderer::defaults()),
      m_baselineCanvasSettings(m_canvasSettings),
      m_cameraSettings(CameraOverlay::defaults()),
      m_baselineCameraSettings(m_cameraSettings) {}

QVariantList ProjectController::clips() const {
  QVariantList r;
  double out = 0;
  for (const auto &c : m_clips) {
    r.append(QVariantMap{{"id", c.id},
                         {"sourceStart", c.start},
                         {"sourceDuration", c.length},
                         {"playbackRate", c.rate},
                         {"outputStart", out},
                         {"duration", c.length / c.rate}});
    out += c.length / c.rate;
  }
  return r;
}
double ProjectController::duration() const {
  double d = 0;
  for (const auto &c : m_clips)
    d += c.length / c.rate;
  return d;
}
QString ProjectController::title() const {
  return m_location.isEmpty()
             ? (hasProject() ? tr("未保存工程") : tr("无工程"))
             : QFileInfo(m_location.toLocalFile()).completeBaseName();
}
bool ProjectController::fail(const QString &e) {
  m_error = e;
  emit changed();
  return false;
}
void ProjectController::setSelectedClip(int i) { selectClip(i); }
void ProjectController::commit(const Snapshot &s, int i) {
  pushHistory();
  if (m_undo.size() > 100)
    m_undo.removeFirst();
  m_redo.clear();
  m_clips = s;
  m_selected = i;
  m_selectedIds = {m_clips[i].id};
  m_error.clear();
  m_status = tr("剪辑已更新");
  emit changed();
}
bool ProjectController::newRecording(QUrl url, double seconds,
                                     QVariantList samples, QVariantList markers) {
  if (dirty())
    return fail(tr("请先保存或显式丢弃当前修改"));
  if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile() ||
      !validTime(seconds) || seconds <= 0)
    return fail(tr("录制文件或时长无效"));
  QSet<QString> markerIds;
  for (auto &value : markers) {
    auto marker = value.toMap();
    const QString id = marker.value("id").toString();
    bool timeOk = false, numberOk = false;
    const double time = marker.value("sourceTime").toDouble(&timeOk);
    const double number = marker.value("number").toDouble(&numberOk);
    if (QUuid(id).isNull() || markerIds.contains(id) || !timeOk || !validTime(time) ||
        time < 0 || !numberOk || !validTime(number) ||
        number <= 0 || number > 2147483647 || number != std::floor(number))
      return fail(tr("Invalid recording marker"));
    markerIds.insert(id);
    // Capture startup/finalization can offset its wall clock from media duration.
    // Clamp capture handoff only; persisted project JSON stays strictly validated.
    marker["sourceTime"] = std::min(time, seconds);
    value = marker;
  }
  m_manualFraming.clear();
  m_selectedManual.clear();
  m_markers = markers;
  m_exportSettings = {{"resolution", "source"}, {"fps", 60}};
  m_canvasSettings = CanvasRenderer::defaults();
  m_baselineCanvasSettings = m_canvasSettings;
  m_baselineFramingSettings = m_framingSettings;
  m_baselineCameraSettings = m_cameraSettings;
  m_framingSettings = {
      {"scale", 1.6}, {"transition", .7}, {"follow", true}, {"spring", true}};
  m_baselineFramingSettings = m_framingSettings;
  m_baselineCameraSettings = m_cameraSettings;
  m_pointerSamples.clear();
  for (const auto &sample : samples)
    if (sample.toMap().value("time").toDouble() <= seconds)
      m_pointerSamples.append(sample);
  m_framing =
      AutoFraming::generate(m_pointerSamples, seconds, m_framingSettings);
  m_autoZoom = false;
  m_baselineAutoZoom = false;
  m_systemAudioSource = {};
  m_microphoneAudioSource = {};
  m_audioSettings = {{"systemVolume", .82}, {"microphoneVolume", .9}, {"systemMuted", false}, {"microphoneMuted", false}};
  m_cameraSource = {};
  m_cameraSettings = CameraOverlay::defaults();
  m_baselineCameraSettings = m_cameraSettings;
  m_baselineMarkers = m_markers;
  m_baselineExportSettings = m_exportSettings;
  m_baselineManualFraming = m_manualFraming;
  m_baselineAudioSettings = m_audioSettings;
  m_source = url;
  m_sourceDuration = seconds;
  m_clips = {{uid(), 0, seconds, 1}};
  m_baseline.clear();
  m_location = QUrl{};
  m_selected = 0;
  m_selectedIds = {m_clips[0].id};
  m_undo.clear();
  m_redo.clear();
  m_unsaved = true;
  m_error.clear();
  m_status = tr("录制已载入");
  emit changed();
  return true;
}
bool ProjectController::selectedPoint(double t, double &offset) const {
  if (!validTime(t) || m_selected < 0 || m_selected >= m_clips.size())
    return false;
  double begin = 0;
  for (int i = 0; i < m_selected; ++i)
    begin += m_clips[i].length / m_clips[i].rate;
  offset = t - begin;
  return offset > 0 &&
         offset < m_clips[m_selected].length / m_clips[m_selected].rate;
}
QVariantMap ProjectController::editingState(double seconds) const {
  double offset=0;
  const bool inside=selectedPoint(seconds,offset);
  const bool selected=m_selected>=0 && m_selected<m_clips.size();
  bool merge=false;
  if(selected && m_selected+1<m_clips.size()) {
    const auto &left=m_clips[m_selected], &right=m_clips[m_selected+1];
    merge=left.rate==right.rate && std::abs(left.start+left.length-right.start)<1e-6;
  }
  const int count=selectedClips().size();
  return {{"canSplit",inside},{"canTrimStart",inside},{"canTrimEnd",inside},
          {"canRemove",selected && m_clips.size()>1},{"canRemoveSelected",count>0 && count<m_clips.size()},
          {"canMergeNext",merge},{"selectedCount",count},{"canUndo",canUndo()},{"canRedo",canRedo()},
          {"canRestoreHead",selected && m_clips[m_selected].start>0},
          {"canRestoreTail",selected && m_clips[m_selected].start+m_clips[m_selected].length<m_sourceDuration-1e-6}};
}
bool ProjectController::split(double t) {
  double o;
  if (!selectedPoint(t, o))
    return fail(tr("分割点必须位于所选片段内部"));
  auto s = m_clips;
  auto &c = s[m_selected];
  double left = o * c.rate;
  Clip right{uid(), c.start + left, c.length - left, c.rate};
  c.length = left;
  s.insert(m_selected + 1, right);
  commit(s, m_selected + 1);
  return true;
}
bool ProjectController::trimStart(double t) {
  double o;
  if (!selectedPoint(t, o))
    return fail(tr("裁剪点必须位于所选片段内部"));
  auto s = m_clips;
  s[m_selected].start += o * s[m_selected].rate;
  s[m_selected].length -= o * s[m_selected].rate;
  commit(s, m_selected);
  return true;
}
bool ProjectController::trimEnd(double t) {
  double o;
  if (!selectedPoint(t, o))
    return fail(tr("裁剪点必须位于所选片段内部"));
  auto s = m_clips;
  s[m_selected].length = o * s[m_selected].rate;
  commit(s, m_selected);
  return true;
}
bool ProjectController::removeSelected() {
  if (m_clips.size() <= 1 || m_selected < 0 || m_selected >= m_clips.size())
    return fail(tr("不能删除最后一个片段"));
  auto s = m_clips;
  s.removeAt(m_selected);
  commit(s, std::min(m_selected, int(s.size()) - 1));
  return true;
}
void ProjectController::undo() {
  if (!canUndo())
    return;
  m_redo.append({m_clips, m_autoZoom, m_canvasSettings, m_framingSettings,
                 m_cameraSettings, m_markers, m_exportSettings, m_manualFraming, m_audioSettings});
  auto state = m_undo.takeLast();
  m_clips = state.clips;
  m_autoZoom = state.autoZoom;
  m_canvasSettings = state.canvasSettings;
  m_framingSettings = state.framingSettings;
  m_cameraSettings = state.cameraSettings;
  m_markers = state.markers;
  m_exportSettings = state.exportSettings;
  m_manualFraming = state.manualFraming;
  m_audioSettings = state.audioSettings;
  reconcileManualSelection();
  m_framing = AutoFraming::generate(m_pointerSamples, m_sourceDuration,
                                    m_framingSettings);
  m_selected = std::clamp(m_selected, 0, int(m_clips.size()) - 1);
  m_selectedIds = {m_clips[m_selected].id};
  m_error.clear();
  emit changed();
}
void ProjectController::redo() {
  if (!canRedo())
    return;
  pushHistory();
  auto state = m_redo.takeLast();
  m_clips = state.clips;
  m_autoZoom = state.autoZoom;
  m_canvasSettings = state.canvasSettings;
  m_framingSettings = state.framingSettings;
  m_cameraSettings = state.cameraSettings;
  m_markers = state.markers;
  m_exportSettings = state.exportSettings;
  m_manualFraming = state.manualFraming;
  m_audioSettings = state.audioSettings;
  reconcileManualSelection();
  m_framing = AutoFraming::generate(m_pointerSamples, m_sourceDuration,
                                    m_framingSettings);
  m_selected = std::clamp(m_selected, 0, int(m_clips.size()) - 1);
  m_selectedIds = {m_clips[m_selected].id};
  m_error.clear();
  emit changed();
}
void ProjectController::discardChanges() {
  reconcileManualSelection();
  m_baseline = m_clips;
  m_baselineAutoZoom = m_autoZoom;
  m_baselineCanvasSettings = m_canvasSettings;
  m_baselineFramingSettings = m_framingSettings;
  m_baselineCameraSettings = m_cameraSettings;
  m_baselineMarkers = m_markers;
  m_baselineExportSettings = m_exportSettings;
  m_baselineManualFraming = m_manualFraming;
  m_baselineAudioSettings = m_audioSettings;
  m_unsaved = false;
  m_error.clear();
  emit changed();
}
double ProjectController::sourceTime(double t) const {
  if (!validTime(t) || m_clips.isEmpty())
    return 0;
  t = std::clamp(t, 0.0, duration());
  double begin = 0;
  for (const auto &c : m_clips) {
    double d = c.length / c.rate;
    if (t < begin + d)
      return c.start + (t - begin) * c.rate;
    begin += d;
  }
  return m_clips.last().start + m_clips.last().length;
}
bool ProjectController::writeJson(const QString &dir,
                                  const QString &backgroundRelative) {
  auto canvas = m_canvasSettings;
  struct AssetCopies {
    QStringList paths;
    bool committed = false;
    ~AssetCopies() { if (!committed) for (const auto &path : paths) QFile::remove(path); }
  } stickerCopies;
  QVariantList stickers;
  if (!prepareStickerAssets(dir, stickers, stickerCopies.paths)) return false;
  canvas["stickers"] = stickers;
  const bool video = !canvas.value("backgroundVideo").toString().isEmpty();
  canvas["backgroundImage"] = video ? QString{} : backgroundRelative;
  canvas["backgroundVideo"] = video ? backgroundRelative : QString{};
  QJsonArray segments;
  for (const auto &c : m_clips)
    segments.append(QJsonObject{{"id", c.id},
                                {"sourceStart", c.start},
                                {"sourceDuration", c.length},
                                {"playbackRate", c.rate}});
  QJsonObject root{
      {"producer", "ScreenPilot"},
      {"formatVersion", 8},
      {"screenPilot",
       QJsonObject{
           {"sourceDuration", m_sourceDuration},
           {"pointerSamples", QJsonArray::fromVariantList(m_pointerSamples)},
           {"autoZoomEnabled", m_autoZoom},
           {"framingSettings", QJsonObject::fromVariantMap(m_framingSettings)},
           {"manualFraming", QJsonArray::fromVariantList(m_manualFraming)},
           {"canvasSettings", QJsonObject::fromVariantMap(canvas)}}},
      {"media",
       QJsonObject{
           {"screen", QJsonObject{{"relativePath", "media/screen-0001.mp4"}}}}},
      {"timeline",
       QJsonObject{{"sourceSequence",
                    QJsonObject{{"kind", "edited"}, {"segments", segments}}}}}};
  auto screenPilot = root.value("screenPilot").toObject();
  screenPilot["cameraSettings"] = QJsonObject::fromVariantMap(m_cameraSettings);
  screenPilot["recordingMarkers"] = QJsonArray::fromVariantList(m_markers);
  screenPilot["exportSettings"] = QJsonObject::fromVariantMap(m_exportSettings);
  screenPilot["audioSettings"] = QJsonObject::fromVariantMap(m_audioSettings);
  root["screenPilot"] = screenPilot;
  auto audioMedia = root.value("media").toObject();
  if (!m_systemAudioSource.isEmpty()) audioMedia["systemAudio"] = QJsonObject{{"relativePath", "media/system-0001.wav"}};
  if (!m_microphoneAudioSource.isEmpty()) audioMedia["microphoneAudio"] = QJsonObject{{"relativePath", "media/microphone-0001.wav"}};
  root["media"] = audioMedia;
  if (!m_cameraSource.isEmpty()) {
    auto media = root.value("media").toObject();
    media["camera"] = QJsonObject{{"relativePath", "media/camera-0001.mp4"}};
    root["media"] = media;
  }
  const QString projectFile = QDir(dir).filePath("project.json");
  QFile previous(projectFile);
  if (previous.exists() && previous.open(QIODevice::ReadOnly)) {
    const auto bytes = previous.readAll();
    previous.close();
    const auto old = QJsonDocument::fromJson(bytes);
    if (old.isObject() && old.object().value("producer").toString() == "ScreenPilot" &&
        old.object().value("formatVersion").toInt() >= 1 && old.object().value("formatVersion").toInt() <= 8) {
      QSaveFile backup(QDir(dir).filePath("project.previous.json"));
      if (!backup.open(QIODevice::WriteOnly) || backup.write(bytes) != bytes.size() || !backup.commit())
        return fail(tr("无法保存工程备份，当前工程未被覆盖"));
    }
  }
  QSaveFile f(projectFile);
  if (!f.open(QIODevice::WriteOnly))
    return fail(f.errorString());
  auto data = QJsonDocument(root).toJson();
  if (f.write(data) != data.size() || !f.commit())
    return fail(f.errorString());
  stickerCopies.committed = true;
  return true;
}
void ProjectController::saved(const QString &dir) {
  useSavedStickers(dir);
  m_location = QUrl::fromLocalFile(dir);
  if (!m_systemAudioSource.isEmpty()) m_systemAudioSource = QUrl::fromLocalFile(QDir(dir).filePath("media/system-0001.wav"));
  if (!m_microphoneAudioSource.isEmpty()) m_microphoneAudioSource = QUrl::fromLocalFile(QDir(dir).filePath("media/microphone-0001.wav"));
  m_source = QUrl::fromLocalFile(QDir(dir).filePath("media/screen-0001.mp4"));
  if (!m_cameraSource.isEmpty())
    m_cameraSource =
        QUrl::fromLocalFile(QDir(dir).filePath("media/camera-0001.mp4"));
  m_baseline = m_clips;
  m_baselineAutoZoom = m_autoZoom;
  m_baselineCanvasSettings = m_canvasSettings;
  m_baselineFramingSettings = m_framingSettings;
  m_baselineCameraSettings = m_cameraSettings;
  m_baselineMarkers = m_markers;
  m_baselineExportSettings = m_exportSettings;
  m_baselineManualFraming = m_manualFraming;
  m_baselineAudioSettings = m_audioSettings;
  m_unsaved = false;
  m_error.clear();
  m_status = tr("工程已保存（ScreenPilot 内部格式）");
  emit changed();
}
bool ProjectController::saveAs(QUrl url) {
  if (!hasProject())
    return fail(tr("没有可保存的工程"));
  if (!url.isLocalFile())
    return fail(tr("请选择本地工程目录"));
  QString dir = QDir::cleanPath(url.toLocalFile());
  if (!dir.endsWith(".dogscproject", Qt::CaseInsensitive))
    dir += ".dogscproject";
  if (!m_location.isEmpty() &&
      QFileInfo(dir).canonicalFilePath() ==
          QFileInfo(m_location.toLocalFile()).canonicalFilePath())
    return save();
  if (QFileInfo::exists(dir))
    return fail(tr("目标已经存在，拒绝覆盖其他工程"));
  if (!QDir().mkpath(QDir(dir).filePath("media")))
    return fail(tr("无法创建工程目录"));
  QString media = QDir(dir).filePath("media/screen-0001.mp4");
  QString relative, copied;
  const QString cameraFile = QDir(dir).filePath("media/camera-0001.mp4");
  QStringList audioCopied;
  const bool cameraCopied =
      m_cameraSource.isEmpty() ||
      QFile::copy(m_cameraSource.toLocalFile(), cameraFile);
  if (!cameraCopied || !copyAudio(dir, audioCopied) || !QFile::copy(m_source.toLocalFile(), media) ||
      !prepareBackground(dir, relative, copied) || !writeJson(dir, relative)) {
    for (const auto &path : audioCopied) QFile::remove(path);
    if (!copied.isEmpty())
      QFile::remove(copied);
    QDir(dir).rmdir("assets");
    if (!m_cameraSource.isEmpty() && cameraCopied)
      QFile::remove(cameraFile);
    QFile::remove(media);
    QDir(dir).rmdir("media");
    QDir().rmdir(dir);
    if (m_error.isEmpty())
      return fail(tr("复制录制媒体失败"));
    return false;
  }
  if (!relative.isEmpty())
    useSavedBackground(QDir(dir).filePath(relative));
  saved(dir);
  return true;
}
bool ProjectController::save() {
  if (m_location.isEmpty())
    return fail(tr("请先选择工程保存目录"));
  QString relative, copied;
  if (!prepareBackground(m_location.toLocalFile(), relative, copied))
    return false;
  if (!writeJson(m_location.toLocalFile(), relative)) {
    if (!copied.isEmpty())
      QFile::remove(copied);
    return false;
  }
  if (!relative.isEmpty())
    useSavedBackground(QDir(m_location.toLocalFile()).filePath(relative));
  saved(m_location.toLocalFile());
  return true;
}
bool ProjectController::openProject(QUrl url) {
  if (dirty())
    return fail(tr("请先保存或显式丢弃当前修改"));
  if (!url.isLocalFile())
    return fail(tr("请选择本地工程"));
  QFileInfo input(url.toLocalFile());
  QString dir = input.isDir() ? input.absoluteFilePath() : input.absolutePath();
  if (!input.isDir() && input.fileName() != "project.json")
    return fail(tr("请选择工程目录或 project.json"));
  QFile f(QDir(dir).filePath("project.json"));
  QJsonParseError pe;
  const bool opened = f.open(QIODevice::ReadOnly);
  auto doc = QJsonDocument::fromJson(opened ? f.readAll() : QByteArray(), &pe);
  // Only unreadable/malformed JSON may fall back. Valid future or foreign
  // projects continue through the strict version gate and are never downgraded.
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
    QFile backup(QDir(dir).filePath("project.previous.json"));
    if (!backup.open(QIODevice::ReadOnly)) return fail(tr("工程 JSON 无效且没有可用备份"));
    doc = QJsonDocument::fromJson(backup.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return fail(tr("工程及备份 JSON 无效"));
  }
  auto root = doc.object();
  if (root.value("producer").toString() != "ScreenPilot" ||
      (root.value("formatVersion").toDouble() != 1 &&
       root.value("formatVersion").toDouble() != 2 &&
       root.value("formatVersion").toDouble() != 3 &&
       root.value("formatVersion").toDouble() != 4 &&
       root.value("formatVersion").toDouble() != 5 &&
       root.value("formatVersion").toDouble() != 6 &&
       root.value("formatVersion").toDouble() != 7 &&
       root.value("formatVersion").toDouble() != 8))
    return fail(tr("仅支持 ScreenPilot 内部工程版本 1/2/3/4/5/6/7/8；Mac "
                   "工程不受支持，拒绝编辑以保护未知字段"));
  double total =
      root.value("screenPilot").toObject().value("sourceDuration").toDouble(-1);
  if (!validTime(total) || total <= 0)
    return fail(tr("源时长无效"));
  QString rel = root.value("media")
                    .toObject()
                    .value("screen")
                    .toObject()
                    .value("relativePath")
                    .toString();
  QString base = QFileInfo(dir).canonicalFilePath();
  QString media = QFileInfo(QDir(dir).filePath(rel)).canonicalFilePath();
  if (rel.isEmpty() || QDir::isAbsolutePath(rel) || base.isEmpty() ||
      media.isEmpty() || !media.startsWith(base + "/", Qt::CaseInsensitive) ||
      !QFileInfo(media).isFile())
    return fail(tr("媒体路径无效或越出工程目录"));
  auto seq =
      root.value("timeline").toObject().value("sourceSequence").toObject();
  if (seq.value("kind").toString() != "edited" ||
      !seq.value("segments").isArray())
    return fail(tr("剪辑序列无效"));
  Snapshot clips;
  QSet<QString> ids;
  for (const auto &v : seq.value("segments").toArray()) {
    auto o = v.toObject();
    Clip c{o.value("id").toString(), o.value("sourceStart").toDouble(-1),
           o.value("sourceDuration").toDouble(-1),
           o.value("playbackRate").toDouble(-1)};
    if (QUuid(c.id).isNull() || ids.contains(c.id) || !validTime(c.start) ||
        !validTime(c.length) || !validTime(c.rate) || c.start < 0 ||
        c.length <= 0 || c.rate <= 0 || !validTime(c.length / c.rate) ||
        c.length / c.rate <= 0 || !validTime(c.start + c.length) ||
        c.start + c.length > total + 1e-6)
      return fail(tr("片段时间、标识或速度无效"));
    ids.insert(c.id);
    clips.append(c);
  }
  double outputTotal = 0;
  for (const auto &clip : clips)
    outputTotal += clip.length / clip.rate;
  if (!validTime(outputTotal) || outputTotal <= 0)
    return fail(tr("时间线总时长无效"));
  if (clips.isEmpty())
    return fail(tr("工程没有片段"));
  const auto settings = root.value("screenPilot").toObject();
  QVariantList markers;
  QSet<QString> markerIds;
  if (settings.contains("recordingMarkers") && !settings.value("recordingMarkers").isArray())
    return fail(tr("Invalid recording markers"));
  for (const auto &value : settings.value("recordingMarkers").toArray()) {
    const auto marker = value.toObject();
    const QString id = marker.value("id").toString();
    const double time = marker.value("sourceTime").toDouble(-1);
    if (!value.isObject() || QUuid(id).isNull() || markerIds.contains(id) ||
        !validTime(time) || time < 0 || time > total ||
        !marker.value("number").isDouble() || marker.value("number").toInt() <= 0 ||
        marker.value("number").toDouble() != marker.value("number").toInt())
      return fail(tr("Invalid recording marker"));
    markerIds.insert(id);
    markers.append(marker.toVariantMap());
  }
  QVariantMap exportSettings{{"resolution", "source"}, {"fps", 60}};
  if (settings.contains("exportSettings")) {
    const auto supplied = settings.value("exportSettings").toObject();
    const QString resolution = supplied.value("resolution").toString();
    const double fps = supplied.value("fps").toDouble(-1);
    if (!settings.value("exportSettings").isObject() ||
        !QStringList{"source", "1080p", "1440p", "4k"}.contains(resolution) ||
        (fps != 30 && fps != 60))
      return fail(tr("Invalid export settings"));
    exportSettings = {{"resolution", resolution}, {"fps", int(fps)}};
  }
  const auto rawSamples = settings.value("pointerSamples");
  if (!rawSamples.isUndefined() && !rawSamples.isArray())
    return fail(tr("鼠标轨迹格式无效"));
  QVariantList samples;
  double previous = -1;
  for (const auto &value : rawSamples.toArray()) {
    if (!value.isObject())
      return fail(tr("鼠标轨迹样本无效"));
    const auto sample = value.toObject();
    const double t = sample.value("time").toDouble(-1);
    const double x = sample.value("x").toDouble(-1),
                 y = sample.value("y").toDouble(-1);
    if (!validTime(t) || t < previous || t < 0 || t > total + 0.15 ||
        !validTime(x) || !validTime(y) || x < 0 || x > 1 || y < 0 || y > 1 ||
        !sample.value("click").isBool() || !sample.value("inside").isBool())
      return fail(tr("鼠标轨迹时间或坐标无效"));
    previous = t;
    samples.append(sample.toVariantMap());
  }
  if (settings.contains("autoZoomEnabled") &&
      !settings.value("autoZoomEnabled").isBool())
    return fail(tr("自动运镜设置无效"));
  QVariantMap canvas = CanvasRenderer::defaults();
  if (root.value("formatVersion").toDouble() < 3) {
    canvas["ratio"] = "source";
    canvas["padding"] = 0;
    canvas["radius"] = 0;
    canvas["shadow"] = 0.0;
    canvas["backgroundColor"] = "#000000";
    canvas["backgroundImage"] = "";
  } else {
    if (!settings.value("canvasSettings").isObject())
      return fail(tr("画布设置格式无效"));
    const auto suppliedCanvas = settings.value("canvasSettings").toObject().toVariantMap();
    for (auto it = suppliedCanvas.cbegin(); it != suppliedCanvas.cend(); ++it)
      canvas[it.key()] = it.value();
    const QString background = canvas.value("backgroundImage").toString();
    if (!background.isEmpty()) {
      const QString resolved =
          QFileInfo(QDir(dir).filePath(background)).canonicalFilePath();
      if (QDir::isAbsolutePath(background) || resolved.isEmpty() ||
          !resolved.startsWith(base + "/", Qt::CaseInsensitive) ||
          !QFileInfo(resolved).isFile())
        return fail(tr("背景图片缺失或路径越出工程目录"));
      QImageReader reader(resolved);
      if (reader.supportsAnimation() || reader.read().isNull())
        return fail(tr("背景图片无法解码或不是静态图片"));
      canvas["backgroundImage"] = resolved;
    }
    const QString video = canvas.value("backgroundVideo").toString();
    if (!video.isEmpty()) {
      const QString resolved = QFileInfo(QDir(dir).filePath(video)).canonicalFilePath();
      if (QDir::isAbsolutePath(video) || resolved.isEmpty() ||
          !resolved.startsWith(base + "/", Qt::CaseInsensitive) || !QFileInfo(resolved).isFile())
        return fail(tr("背景视频缺失或路径越出工程目录"));
      canvas["backgroundVideo"] = resolved;
    }
    if (canvas.contains("stickers") && canvas.value("stickers").metaType().id()!=QMetaType::QVariantList)
      return fail(tr("Invalid sticker list"));
    auto stickers = canvas.value("stickers").toList();
    for (auto &value : stickers) {
      auto sticker = value.toMap();
      const QString relative = sticker.value("imagePath").toString();
      const QString resolved = QFileInfo(QDir(dir).filePath(relative)).canonicalFilePath();
      QImageReader reader(resolved);
      if (relative.isEmpty() || QDir::isAbsolutePath(relative) || resolved.isEmpty() ||
          !resolved.startsWith(base + "/", Qt::CaseInsensitive) || !QFileInfo(resolved).isFile() ||
          reader.supportsAnimation() || reader.read().isNull())
        return fail(tr("Sticker image missing or outside project"));
      sticker["imagePath"] = resolved;
      const double start=sticker.value("sourceStart").toDouble(), length=sticker.value("sourceDuration").toDouble();
      if (!validTime(start) || !validTime(length) || start<0 || length<=0 || start+length>total+1e-6)
        return fail(tr("Invalid sticker time"));
      value = sticker;
    }
    canvas["stickers"] = stickers;
    QString validationError;
    if (!CanvasRenderer::validate(canvas, &validationError))
      return fail(validationError);
  }
  QVariantMap framing{
      {"scale", 1.6}, {"transition", .7}, {"follow", true}, {"spring", true}};
  if (settings.contains("framingSettings")) {
    if (!settings.value("framingSettings").isObject())
      return fail(tr("Invalid framing settings"));
    const auto raw = settings.value("framingSettings").toObject();
    for (auto it = raw.begin(); it != raw.end(); ++it) {
      if (!framing.contains(it.key()))
        continue;
      const auto v = it.value();
      if ((it.key() == "follow" || it.key() == "spring")
              ? !v.isBool()
              : (!v.isDouble() || !validTime(v.toDouble()) ||
                 v.toDouble() < (it.key() == "scale" ? 1 : 0) ||
                 v.toDouble() > (it.key() == "scale" ? 6 : 5)))
        return fail(tr("Invalid framing settings"));
      framing[it.key()] = v.toVariant();
    }
  }
  QUrl cameraSource;
  QVariantMap cameraSettings = CameraOverlay::defaults();
  if (settings.contains("cameraSettings")) {
    if (!settings.value("cameraSettings").isObject())
      return fail(tr("摄像头布局无效"));
    const auto supplied =
        settings.value("cameraSettings").toObject().toVariantMap();
    for (auto it = supplied.cbegin(); it != supplied.cend(); ++it)
      cameraSettings[it.key()] = it.value();
  }
  QString cameraError;
  if (!CameraOverlay::validate(cameraSettings, &cameraError))
    return fail(cameraError);
  bool offsetOk = false;
  const double offset = cameraSettings.value("offset", 0.0).toDouble(&offsetOk);
  if (!offsetOk || !validTime(offset) || std::abs(offset) > 10)
    return fail(tr("摄像头同步偏移无效"));
  if (root.value("media").toObject().contains("camera")) {
    const QString cameraRel = root.value("media")
                                  .toObject()
                                  .value("camera")
                                  .toObject()
                                  .value("relativePath")
                                  .toString();
    const QString cameraPath =
        QFileInfo(QDir(dir).filePath(cameraRel)).canonicalFilePath();
    if (cameraRel.isEmpty() || QDir::isAbsolutePath(cameraRel) ||
        cameraPath.isEmpty() ||
        !cameraPath.startsWith(base + "/", Qt::CaseInsensitive) ||
        !QFileInfo(cameraPath).isFile())
      return fail(tr("摄像头素材缺失或越出工程目录"));
    cameraSource = QUrl::fromLocalFile(cameraPath);
  }
  QVariantList manual;
  if (settings.contains("manualFraming")) {
    if (!settings.value("manualFraming").isArray()) return fail(tr("Invalid manual framing"));
    QSet<QString> manualIds;
    for (const auto &value : settings.value("manualFraming").toArray()) {
      const auto segment = value.toObject().toVariantMap();
      const QString id = segment.value("id").toString();
      if (!value.isObject() || QUuid(id).isNull() || manualIds.contains(id) ||
          !AutoFraming::validateManual(segment, total)) return fail(tr("Invalid manual framing"));
      manualIds.insert(id); manual.append(segment);
    }
  }
  QVariantMap audio{{"systemVolume", .82}, {"microphoneVolume", .9}, {"systemMuted", false}, {"microphoneMuted", false}};
  if (settings.contains("audioSettings")) {
    if (!settings.value("audioSettings").isObject()) return fail(tr("Invalid audio settings"));
    const auto supplied = settings.value("audioSettings").toObject().toVariantMap();
    for (auto it = supplied.cbegin(); it != supplied.cend(); ++it) {
      if (!audio.contains(it.key())) return fail(tr("Unknown audio option"));
      if (it.key().endsWith("Muted")) {
        if (it.value().metaType().id() != QMetaType::Bool) return fail(tr("Invalid audio mute"));
      } else {
        bool ok = false; const double volume = it.value().toDouble(&ok);
        if (!ok || !validTime(volume) || volume < 0 || volume > 1) return fail(tr("Invalid audio volume"));
      }
      audio[it.key()] = it.value();
    }
  }
  QUrl systemAudio, microphoneAudio;
  auto loadAudio = [&](const QString &key, QUrl &target) {
    const auto object = root.value("media").toObject();
    if (!object.contains(key)) return true;
    const QString relative = object.value(key).toObject().value("relativePath").toString();
    const QString path = QFileInfo(QDir(dir).filePath(relative)).canonicalFilePath();
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || path.isEmpty() ||
        !path.startsWith(base + "/", Qt::CaseInsensitive) || !QFileInfo(path).isFile()) return false;
    target = QUrl::fromLocalFile(path); return true;
  };
  if (!loadAudio("systemAudio", systemAudio) || !loadAudio("microphoneAudio", microphoneAudio)) return fail(tr("Audio media missing or outside project"));
  m_audioSettings = audio; m_systemAudioSource = systemAudio; m_microphoneAudioSource = microphoneAudio;
  m_manualFraming = manual;
  m_selectedManual.clear();
  m_markers = markers;
  m_exportSettings = exportSettings;
  m_cameraSource = cameraSource;
  m_cameraSettings = cameraSettings;
  m_framingSettings = framing;
  m_canvasSettings = canvas;
  m_pointerSamples = samples;
  m_framing = AutoFraming::generate(samples, total, m_framingSettings);
  m_autoZoom =
      settings.value("autoZoomEnabled").toBool(false) && !m_framing.isEmpty();
  m_baselineAutoZoom = m_autoZoom;
  m_baselineCanvasSettings = m_canvasSettings;
  m_baselineFramingSettings = m_framingSettings;
  m_baselineCameraSettings = m_cameraSettings;
  m_baselineMarkers = m_markers;
  m_baselineExportSettings = m_exportSettings;
  m_baselineManualFraming = m_manualFraming;
  m_baselineAudioSettings = m_audioSettings;
  m_clips = clips;
  m_baseline = clips;
  m_sourceDuration = total;
  m_source = QUrl::fromLocalFile(media);
  m_location = QUrl::fromLocalFile(dir);
  m_selected = 0;
  m_selectedIds = {m_clips[0].id};
  m_unsaved = false;
  m_undo.clear();
  m_redo.clear();
  m_error.clear();
  m_status = tr("ScreenPilot 工程已打开");
  emit changed();
  return true;
}

void ProjectController::setAutoZoomEnabled(bool enabled) {
  if (!hasProject() || enabled == m_autoZoom)
    return;
  if (enabled && m_framing.isEmpty()) {
    fail(tr("未检测到画面内点击，请重新录制并在目标画面内点击"));
    return;
  }
  pushHistory();
  if (m_undo.size() > 100)
    m_undo.removeFirst();
  m_redo.clear();
  m_autoZoom = enabled;
  m_error.clear();
  m_status = enabled ? tr("自动运镜已启用") : tr("自动运镜已关闭");
  emit changed();
}
QVariantMap ProjectController::framingAt(double outputSeconds) const {
  const auto state =
      AutoFraming::evaluate(framingSegments(), sourceTime(outputSeconds));
  return {{"scale", state.scale},
          {"centerX", state.centerX},
          {"centerY", state.centerY}};
}

void ProjectController::pushHistory() {
  m_undo.append({m_clips, m_autoZoom, m_canvasSettings, m_framingSettings,
                 m_cameraSettings, m_markers, m_exportSettings, m_manualFraming, m_audioSettings});
  if (m_undo.size() > 100)
    m_undo.removeFirst();
}
void ProjectController::setCanvasOption(QString key, QVariant value) {
  if (!hasProject())
    return;
  if (key == "backgroundImage" || key == "backgroundVideo" || key == "stickers") {
    fail(tr("请使用背景图片选择器更改图片"));
    return;
  }
  if (!m_canvasSettings.contains(key)) {
    fail(tr("未知画布选项"));
    return;
  }
  auto settings = m_canvasSettings;
  settings[key] = value;
  QString error;
  if (!CanvasRenderer::validate(settings, &error)) {
    fail(error);
    return;
  }
  if (settings == m_canvasSettings)
    return;
  pushHistory();
  m_redo.clear();
  m_canvasSettings = settings;
  m_error.clear();
  m_status = tr("画布已更新");
  emit changed();
}
bool ProjectController::setBackgroundImage(QUrl url) {
  if (!hasProject())
    return fail(tr("请先载入录制"));
  if (!url.isLocalFile())
    return fail(tr("请选择本地静态图片"));
  const QString path = QFileInfo(url.toLocalFile()).canonicalFilePath();
  QImageReader reader(path);
  if (path.isEmpty() || !QFileInfo(path).isFile() ||
      reader.supportsAnimation() || reader.read().isNull())
    return fail(tr("背景图片无法解码或不是静态图片"));
  auto settings = m_canvasSettings;
  settings["backgroundImage"] = path;
  settings["backgroundVideo"] = "";
  QString error;
  if (!CanvasRenderer::validate(settings, &error))
    return fail(error);
  if (settings == m_canvasSettings)
    return true;
  pushHistory();
  m_redo.clear();
  m_canvasSettings = settings;
  m_error.clear();
  m_status = tr("背景图片已更新");
  emit changed();
  return true;
}
void ProjectController::clearBackgroundImage() {
  if (!hasProject() ||
      m_canvasSettings.value("backgroundImage").toString().isEmpty())
    return;
  pushHistory();
  m_redo.clear();
  m_canvasSettings["backgroundImage"] = "";
  m_error.clear();
  m_status = tr("背景图片已清除");
  emit changed();
}
bool ProjectController::setBackgroundVideo(QUrl url) {
  if (!hasProject()) return fail(tr("请先载入录制"));
  if (!url.isLocalFile()) return fail(tr("请选择本地背景视频"));
  const QString path = QFileInfo(url.toLocalFile()).canonicalFilePath();
  if (path.isEmpty() || !QFileInfo(path).isFile()) return fail(tr("背景视频不存在"));
  auto settings = m_canvasSettings;
  settings["backgroundVideo"] = path;
  settings["backgroundImage"] = "";
  QString error;
  if (!CanvasRenderer::validate(settings, &error)) return fail(error);
  if (settings == m_canvasSettings) return true;
  pushHistory(); m_redo.clear(); m_canvasSettings = settings;
  m_error.clear(); m_status = tr("背景视频已更新"); emit changed(); return true;
}
void ProjectController::clearBackgroundVideo() {
  if (!hasProject() || m_canvasSettings.value("backgroundVideo").toString().isEmpty()) return;
  pushHistory(); m_redo.clear(); m_canvasSettings["backgroundVideo"] = "";
  m_error.clear(); m_status = tr("背景视频已清除"); emit changed();
}
bool ProjectController::prepareBackground(const QString &dir, QString &relative,
                                          QString &copied) {
  QString error;
  if (!CanvasRenderer::validate(m_canvasSettings, &error))
    return fail(error);
  const bool video = !m_canvasSettings.value("backgroundVideo").toString().isEmpty();
  const QString source = m_canvasSettings.value(video ? "backgroundVideo" : "backgroundImage").toString();
  if (source.isEmpty())
    return true;
  QImageReader reader(source);
  if (!QFileInfo(source).isFile() || (!video && (reader.supportsAnimation() || reader.read().isNull())))
    return fail(tr("背景资源缺失或无法解码，工程未保存"));
  if (!QDir().mkpath(QDir(dir).filePath("assets")))
    return fail(tr("无法创建背景资源目录"));
  const QString base = QFileInfo(dir).canonicalFilePath();
  const QString assets =
      QFileInfo(QDir(dir).filePath("assets")).canonicalFilePath();
  if (base.isEmpty() || assets.isEmpty() ||
      !assets.startsWith(base + "/", Qt::CaseInsensitive))
    return fail(tr("背景资源目录越出工程目录"));
  const QString canonicalSource = QFileInfo(source).canonicalFilePath();
  if (canonicalSource.startsWith(assets + "/", Qt::CaseInsensitive)) {
    relative = QDir(dir).relativeFilePath(canonicalSource);
    return true;
  }
  const QString suffix = QFileInfo(source).suffix().toLower();
  relative = "assets/background-" + uid() +
             (suffix.isEmpty() ? QString{} : "." + suffix);
  copied = QDir(dir).filePath(relative);
  if (!QFile::copy(source, copied)) {
    copied.clear();
    return fail(tr("复制背景图片失败，工程未保存"));
  }
  return true;
}

void ProjectController::useSavedBackground(const QString &path) {
  const QString key = m_canvasSettings.value("backgroundVideo").toString().isEmpty() ? "backgroundImage" : "backgroundVideo";
  const QString previous = m_canvasSettings.value(key).toString();
  auto remap = [&](QVariantMap &settings) {
    if (settings.value(key).toString() == previous)
      settings[key] = path;
  };
  for (auto &state : m_undo)
    remap(state.canvasSettings);
  for (auto &state : m_redo)
    remap(state.canvasSettings);
  remap(m_baselineCanvasSettings);
  m_canvasSettings[key] = path;
}

bool ProjectController::moveClip(int from, int to) {
  if (from < 0 || to < 0 || from >= m_clips.size() || to >= m_clips.size())
    return fail(tr("片段位置无效"));
  if (from == to)
    return true;
  auto clips = m_clips;
  const auto selectedId = m_clips.value(m_selected).id;
  clips.move(from, to);
  int selected = 0;
  for (int i = 0; i < clips.size(); ++i)
    if (clips[i].id == selectedId)
      selected = i;
  commit(clips, selected);
  return true;
}
bool ProjectController::setClipRate(int index, double rate) {
  if (index < 0 || index >= m_clips.size() ||
      !validTime(rate) || rate < 1 || rate > 100)
    return fail(tr("片段或播放速度无效"));
  if (m_clips[index].rate == rate)
    return true;
  auto clips = m_clips;
  clips[index].rate = rate;
  commit(clips, index);
  return true;
}
bool ProjectController::setClipOutputDuration(int index, double seconds) {
  if (index < 0 || index >= m_clips.size() || !validTime(seconds) || seconds <= 0)
    return fail(tr("片段输出时长无效"));
  return setClipRate(index, m_clips[index].length / seconds);
}
void ProjectController::setFramingOption(QString key, QVariant value) {
  if (!hasProject() || !m_framingSettings.contains(key))
    return;
  if (key == "follow" || key == "spring") {
    if (value.metaType().id() != QMetaType::Bool) {
      fail(tr("运镜开关无效"));
      return;
    }
  } else {
    bool ok = false;
    const double n = value.toDouble(&ok);
    if (!ok || !validTime(n) || n < (key == "scale" ? 1 : 0) ||
        n > (key == "scale" ? 6 : 5)) {
      fail(tr("运镜参数无效"));
      return;
    }
    value = n;
  }
  if (m_framingSettings.value(key) == value)
    return;
  pushHistory();
  m_redo.clear();
  m_framingSettings[key] = value;
  m_framing = AutoFraming::generate(m_pointerSamples, m_sourceDuration,
                                    m_framingSettings);
  m_error.clear();
  emit changed();
}

bool ProjectController::setCameraMedia(QUrl source, double offset,
                                       bool mirror) {
  if (!hasProject() || !source.isLocalFile() ||
      !QFileInfo(source.toLocalFile()).isFile() || !validTime(offset) ||
      std::abs(offset) > 10)
    return fail(tr("摄像头素材或同步时间无效"));
  if (!m_location.isEmpty())
    return fail(tr("请在新录制工程中关联摄像头素材"));
  m_cameraSource = source;
  m_cameraSettings = CameraOverlay::defaults();
  m_cameraSettings["offset"] = offset;
  m_cameraSettings["mirror"] = mirror;
  m_unsaved = true;
  m_error.clear();
  emit changed();
  return true;
}
void ProjectController::setCameraOption(QString key, QVariant value) {
  if (!hasProject() || m_cameraSource.isEmpty())
    return;
  auto settings = m_cameraSettings;
  if (!CameraOverlay::defaults().contains(key) && key != "offset")
    return;
  settings[key] = value;
  QString error;
  bool ok = false;
  const double offset = settings.value("offset", 0.0).toDouble(&ok);
  if (!CameraOverlay::validate(settings, &error) || !ok || !validTime(offset) ||
      std::abs(offset) > 10) {
    fail(error.isEmpty() ? tr("摄像头同步偏移无效") : error);
    return;
  }
  if (settings == m_cameraSettings)
    return;
  pushHistory();
  m_redo.clear();
  m_cameraSettings = settings;
  m_error.clear();
  emit changed();
}

QVariantList ProjectController::timelineMarkers() const {
  QVariantList result;
  for (const auto &value : m_markers) {
    auto marker = value.toMap();
    const double time = marker.value("sourceTime").toDouble();
    double output = 0;
    bool visible = false;
    for (int i = 0; i < m_clips.size(); ++i) {
      const auto &clip = m_clips[i];
      if (time >= clip.start && (time < clip.start + clip.length ||
          (time == m_sourceDuration && time == clip.start + clip.length))) {
        marker["outputTime"] = output + (time - clip.start) / clip.rate;
        marker["clipIndex"] = i;
        visible = true;
        break;
      }
      output += clip.length / clip.rate;
    }
    marker["visible"] = visible;
    if (!visible) marker["outputTime"] = -1.0;
    result.append(marker);
  }
  return result;
}
double ProjectController::markerTime(QString id) const {
  for (const auto &value : timelineMarkers()) {
    const auto marker = value.toMap();
    if (marker.value("id").toString() == id)
      return marker.value("outputTime").toDouble();
  }
  return -1;
}
bool ProjectController::removeRecordingMarker(QString id) {
  for (int i = 0; i < m_markers.size(); ++i) {
    if (m_markers[i].toMap().value("id").toString() != id) continue;
    pushHistory();
    m_redo.clear();
    m_markers.removeAt(i);
    m_error.clear();
    emit changed();
    return true;
  }
  return false;
}
void ProjectController::setExportOption(QString key, QVariant value) {
  if (!hasProject()) return;
  if (key == "resolution") {
    if (!QStringList{"source", "1080p", "1440p", "4k"}.contains(value.toString())) {
      fail(tr("Invalid export resolution")); return;
    }
    value = value.toString();
  } else if (key == "fps") {
    bool ok = false;
    const double fps = value.toDouble(&ok);
    if (!ok || (fps != 30 && fps != 60)) { fail(tr("Invalid export fps")); return; }
    value = int(fps);
  } else { fail(tr("Unknown export option")); return; }
  if (m_exportSettings.value(key) == value) return;
  pushHistory();
  m_redo.clear();
  m_exportSettings[key] = value;
  m_error.clear();
  emit changed();
}
QVariantList ProjectController::framingSegments() const {
  QVariantList result = m_autoZoom ? m_framing : QVariantList{};
  for (const auto &value : m_manualFraming) { auto segment=value.toMap(); segment["manual"]=true; result.append(segment); }
  return result;
}
void ProjectController::setSelectedManualFraming(QString id) {
  if (m_selectedManual == id) return;
  for (const auto &v : m_manualFraming) if (v.toMap().value("id").toString()==id) {
    m_selectedManual=id; emit changed(); return;
  }
  if (id.isEmpty()) { m_selectedManual.clear(); emit changed(); }
}
QString ProjectController::addManualFraming(double outputSeconds) {
  if (!hasProject() || !validTime(outputSeconds)) return {};
  const double start=sourceTime(outputSeconds);
  if (start >= m_sourceDuration) { fail(tr("请将播放头移到素材结束之前")); return {}; }
  const QString id=uid();
  QVariantMap segment{{"id",id},{"start",start},{"end",std::min(start+2,m_sourceDuration)},
    {"scale",1.6},{"centerX",.5},{"centerY",.5},{"enter",.7},{"exit",.7},
    {"curve","spring"},{"x1",.65},{"y1",0.0},{"x2",.35},{"y2",1.0}};
  pushHistory(); m_redo.clear(); m_manualFraming.append(segment); m_selectedManual=id;
  m_error.clear(); emit changed(); return id;
}
bool ProjectController::updateManualFraming(QString id, QVariantMap changes) {
  for (int i=0;i<m_manualFraming.size();++i) {
    auto segment=m_manualFraming[i].toMap();
    if (segment.value("id").toString()!=id) continue;
    for(auto it=changes.cbegin();it!=changes.cend();++it) {
      if (it.key()=="id" || !segment.contains(it.key())) return fail(tr("Unknown manual framing option"));
      segment[it.key()]=it.value();
    }
    if (!AutoFraming::validateManual(segment,m_sourceDuration)) return fail(tr("Invalid manual framing parameters"));
    if(segment==m_manualFraming[i].toMap()) return true;
    pushHistory(); m_redo.clear(); m_manualFraming[i]=segment; m_error.clear(); emit changed(); return true;
  }
  return fail(tr("Manual framing clip not found"));
}
bool ProjectController::removeManualFraming(QString id) {
  for(int i=0;i<m_manualFraming.size();++i) if(m_manualFraming[i].toMap().value("id").toString()==id) {
    pushHistory(); m_redo.clear(); m_manualFraming.removeAt(i);
    if(m_selectedManual==id) m_selectedManual.clear(); m_error.clear(); emit changed(); return true;
  }
  return false;
}
bool ProjectController::splitManualFraming(double outputSeconds) {
  if (!validTime(outputSeconds) || outputSeconds < 0 || outputSeconds >= duration()) return false;
  const double point = sourceTime(outputSeconds);
  for (int i=0; i<m_manualFraming.size(); ++i) {
    auto left=m_manualFraming[i].toMap();
    if (left.value("id").toString()!=m_selectedManual) continue;
    if (point-left.value("start").toDouble()<.16 || left.value("end").toDouble()-point<.16) return false;
    auto right=left; right["id"]=uid(); right["start"]=point; left["end"]=point;
    left["exit"]=0.0; right["enter"]=std::max(right.value("enter").toDouble(),.15);
    if (!AutoFraming::validateManual(left,m_sourceDuration) || !AutoFraming::validateManual(right,m_sourceDuration)) return false;
    pushHistory(); m_redo.clear(); m_manualFraming[i]=left; m_manualFraming.append(right);
    m_selectedManual=right.value("id").toString(); m_error.clear(); emit changed(); return true;
  }
  return false;
}
bool ProjectController::copyManualFraming() {
  for (const auto &value:m_manualFraming) if (value.toMap().value("id").toString()==m_selectedManual) {
    m_manualClipboard=value.toMap(); emit changed(); return true;
  }
  return false;
}
QString ProjectController::pasteManualFraming(double outputSeconds) {
  if (!hasProject() || !hasManualClipboard() || !validTime(outputSeconds) || outputSeconds<0 || outputSeconds>=duration()) return {};
  auto segment=m_manualClipboard;
  const double start=sourceTime(outputSeconds);
  const double length=segment.value("end").toDouble()-segment.value("start").toDouble();
  segment["id"]=uid(); segment["start"]=start; segment["end"]=std::min(start+length,m_sourceDuration);
  if (!AutoFraming::validateManual(segment,m_sourceDuration)) return {};
  pushHistory(); m_redo.clear(); m_manualFraming.append(segment); m_selectedManual=segment.value("id").toString();
  m_error.clear(); emit changed(); return m_selectedManual;
}
bool ProjectController::pasteManualFramingProperties() {
  if (!hasManualClipboard()) return false;
  auto changes=m_manualClipboard; changes.remove("id"); changes.remove("start"); changes.remove("end");
  return updateManualFraming(m_selectedManual,changes);
}
bool ProjectController::setCanvasCrop(double x,double y,double width,double height) {
  if(!hasProject()) return false;
  auto canvas=m_canvasSettings;
  canvas["cropX"]=x; canvas["cropY"]=y; canvas["cropWidth"]=width; canvas["cropHeight"]=height;
  QString error; if(!CanvasRenderer::validate(canvas,&error)) return fail(error);
  if(canvas==m_canvasSettings) return true;
  pushHistory(); m_redo.clear(); m_canvasSettings=canvas; m_error.clear(); emit changed(); return true;
}
QVariantMap ProjectController::sceneSettings() const {
  auto camera=m_cameraSettings; camera.remove("offset");
  return {{"canvas",m_canvasSettings},{"framing",m_framingSettings},{"camera",camera},{"autoZoom",m_autoZoom},{"audio",m_audioSettings}};
}
bool ProjectController::applySceneSettings(QVariantMap settings) {
  if(!hasProject()) return fail(tr("请先载入录制"));
  const QStringList keys{"canvas","framing","camera","autoZoom","audio"};
  for(auto it=settings.cbegin();it!=settings.cend();++it) if(!keys.contains(it.key())) return fail(tr("Unknown scene setting"));
  for(const auto &key: {"canvas","framing","camera"})
    if(!settings.contains(key) || settings.value(key).metaType().id()!=QMetaType::QVariantMap) return fail(tr("Invalid scene settings"));
  if(settings.value("autoZoom").metaType().id()!=QMetaType::Bool) return fail(tr("Invalid automatic framing flag"));
  auto canvas=m_canvasSettings, framing=m_framingSettings, camera=m_cameraSettings;
  auto merge=[](QVariantMap &target,const QVariantMap &values,bool cameraMap) {
    for(auto it=values.cbegin();it!=values.cend();++it) {
      if(cameraMap && it.key()=="offset") continue;
      if(!target.contains(it.key())) return false;
      target[it.key()]=it.value();
    } return true;
  };
  if(!merge(canvas,settings.value("canvas").toMap(),false) ||
     !merge(framing,settings.value("framing").toMap(),false) ||
     !merge(camera,settings.value("camera").toMap(),true)) return fail(tr("Unknown scene option"));
  QString error;
  if(!CanvasRenderer::validate(canvas,&error) || !CameraOverlay::validate(camera,&error)) return fail(error);
  for(const auto &key:{"follow","spring"}) if(framing.value(key).metaType().id()!=QMetaType::Bool) return fail(tr("Invalid framing flag"));
  for(const auto &key:{"scale","transition"}) {
    bool ok=false; double n=framing.value(key).toDouble(&ok);
    if(!ok || !validTime(n) || n<(QString(key)=="scale"?1:0) || n>(QString(key)=="scale"?6:5)) return fail(tr("Invalid framing value"));
  }
  for(const auto &key:{"enabled","mirror","shadow","fullscreen"}) if(camera.value(key).metaType().id()!=QMetaType::Bool) return fail(tr("Invalid camera flag"));
  if(!QColor(camera.value("borderColor").toString()).isValid()) return fail(tr("Invalid camera border color"));
  auto audio = m_audioSettings;
  if (settings.contains("audio")) {
    if (settings.value("audio").metaType().id()!=QMetaType::QVariantMap) return fail(tr("Invalid audio settings"));
    const auto supplied=settings.value("audio").toMap();
    for(auto it=supplied.cbegin();it!=supplied.cend();++it) {
      if(!audio.contains(it.key())) return fail(tr("Unknown audio option"));
      if(it.key().endsWith("Muted")) {
        if(it.value().metaType().id()!=QMetaType::Bool) return fail(tr("Invalid audio mute"));
      } else { bool ok=false; double n=it.value().toDouble(&ok);
        if(!ok || !validTime(n) || n<0 || n>1) return fail(tr("Invalid audio volume")); }
      audio[it.key()]=it.value();
    }
  }
  const bool autoZoom=settings.value("autoZoom").toBool();
  if(canvas==m_canvasSettings && framing==m_framingSettings && camera==m_cameraSettings && audio==m_audioSettings && autoZoom==m_autoZoom) return true;
  pushHistory(); m_redo.clear(); m_canvasSettings=canvas; m_framingSettings=framing; m_cameraSettings=camera; m_audioSettings=audio; m_autoZoom=autoZoom;
  m_framing=AutoFraming::generate(m_pointerSamples,m_sourceDuration,m_framingSettings);
  m_error.clear(); emit changed(); return true;
}

void ProjectController::reconcileManualSelection() {
  for (const auto &value : m_manualFraming)
    if (value.toMap().value("id").toString() == m_selectedManual) return;
  m_selectedManual.clear();
}

QVariantList ProjectController::selectedClips() const {
  QVariantList result;
  for (int i=0;i<m_clips.size();++i)
    if (m_selectedIds.contains(m_clips[i].id)) result.append(i);
  if (result.isEmpty() && m_selected>=0 && m_selected<m_clips.size()) result.append(m_selected);
  return result;
}
void ProjectController::selectClip(int index, bool extend, bool toggle) {
  if (index<0 || index>=m_clips.size()) return;
  if (extend && m_selected>=0 && m_selected<m_clips.size()) {
    for(int i=std::min(index,m_selected);i<=std::max(index,m_selected);++i) m_selectedIds.insert(m_clips[i].id);
  } else if (toggle) {
    const QString id=m_clips[index].id;
    if(m_selectedIds.contains(id) && m_selectedIds.size()>1) m_selectedIds.remove(id);
    else m_selectedIds.insert(id);
  } else m_selectedIds={m_clips[index].id};
  m_selected=index;
  if(!m_selectedIds.contains(m_clips[index].id))
    for(int i=0;i<m_clips.size();++i) if(m_selectedIds.contains(m_clips[i].id)) { m_selected=i; break; }
  m_selectedManual.clear(); emit changed();
}
bool ProjectController::removeSelectedClips() {
  const auto selected=selectedClips();
  if(selected.isEmpty() || selected.size()>=m_clips.size()) return fail(tr("Cannot delete the last clip"));
  auto clips=m_clips;
  for(int i=selected.size()-1;i>=0;--i) clips.removeAt(selected[i].toInt());
  commit(clips,std::min(selected.first().toInt(),int(clips.size())-1)); return true;
}
bool ProjectController::setSelectedClipsRate(double rate) {
  if(!validTime(rate) || rate<1 || rate>100 || !hasProject()) return fail(tr("Invalid playback rate"));
  auto clips=m_clips; const auto selected=selectedClips();
  for(const auto &index:selected) clips[index.toInt()].rate=rate;
  if(clips==m_clips) return true;
  pushHistory(); m_redo.clear(); m_clips=clips; m_error.clear();
  m_status=tr("Clip rates updated"); emit changed(); return true;
}
bool ProjectController::trimClipBoundary(int index,double start,double end) {
  if(index<0 || index>=m_clips.size() || !validTime(start) || !validTime(end) || start<0 || end>m_sourceDuration || end<=start)
    return fail(tr("Invalid clip boundary"));
  auto clips=m_clips; clips[index].start=start; clips[index].length=end-start;
  if(clips==m_clips) return true;
  commit(clips,index); return true;
}
bool ProjectController::restoreClipHead(int index) {
  if(index<0 || index>=m_clips.size()) return false;
  return trimClipBoundary(index,0,m_clips[index].start+m_clips[index].length);
}
bool ProjectController::restoreClipTail(int index) {
  if(index<0 || index>=m_clips.size()) return false;
  return trimClipBoundary(index,m_clips[index].start,m_sourceDuration);
}
bool ProjectController::mergeAdjacent(int index) {
  if(index<0 || index+1>=m_clips.size()) return fail(tr("No adjacent clip"));
  const auto &left=m_clips[index]; const auto &right=m_clips[index+1];
  if(left.rate!=right.rate || std::abs(left.start+left.length-right.start)>1e-6)
    return fail(tr("Clips must be contiguous and use the same rate"));
  auto clips=m_clips; clips[index].length+=right.length; clips.removeAt(index+1); commit(clips,index); return true;
}
QVariantList ProjectController::clipsInRange(double start,double end) const {
  QVariantList result;
  if(!validTime(start) || !validTime(end) || start<0 || end<=start || end>duration()+1e-6) return result;
  double output=0,rangeOutput=0;
  for(const auto &clip:m_clips) {
    const double stop=output+clip.length/clip.rate;
    const double begin=std::max(start,output),finish=std::min(end,stop);
    if(finish>begin) {
      const double length=(finish-begin)*clip.rate;
      result.append(QVariantMap{{"id",clip.id},{"sourceStart",clip.start+(begin-output)*clip.rate},
        {"sourceDuration",length},{"playbackRate",clip.rate},{"outputStart",rangeOutput},{"duration",finish-begin}});
      rangeOutput+=finish-begin;
    }
    output=stop;
  }
  return result;
}
bool ProjectController::setAudioMedia(QUrl system,QUrl microphone) {
  if(!hasProject() || !m_location.isEmpty()) return fail(tr("Associate audio with a new recording"));
  for(const auto &url:{system,microphone}) {
    if(url.isEmpty()) continue;
    if(!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile()) return fail(tr("Invalid audio media"));
    QFile file(url.toLocalFile());
    if(!file.open(QIODevice::ReadOnly)) return fail(tr("Cannot read audio media"));
    const QByteArray header=file.read(12);
    if(header.size()!=12 || header.left(4)!="RIFF" || header.mid(8,4)!="WAVE") return fail(tr("Audio media must be WAV"));
  }
  m_systemAudioSource=system; m_microphoneAudioSource=microphone; m_unsaved=true; m_error.clear(); emit changed(); return true;
}
void ProjectController::setAudioOption(QString key,QVariant value) {
  if(!hasProject()) return;
  if(!m_audioSettings.contains(key)) { fail(tr("Unknown audio option")); return; }
  if(key.endsWith("Muted")) {
    if(value.metaType().id()!=QMetaType::Bool) { fail(tr("Invalid audio mute")); return; }
  } else {
    bool ok=false; const double volume=value.toDouble(&ok);
    if(!ok || !validTime(volume) || volume<0 || volume>1) { fail(tr("Invalid audio volume")); return; }
    value=volume;
  }
  if(m_audioSettings.value(key)==value) return;
  pushHistory(); m_redo.clear(); m_audioSettings[key]=value; m_error.clear(); emit changed();
}
bool ProjectController::copyAudio(const QString &directory,QStringList &copied) {
  const QString base=QFileInfo(directory).canonicalFilePath();
  const QString media=QFileInfo(QDir(directory).filePath("media")).canonicalFilePath();
  if(base.isEmpty() || media.isEmpty() || !media.startsWith(base+"/",Qt::CaseInsensitive)) return fail(tr("Invalid audio media directory"));
  auto copy=[&](const QUrl &source,const QString &name) {
    if(source.isEmpty()) return true;
    const QString target=QDir(media).filePath(name);
    if(QFileInfo(source.toLocalFile()).canonicalFilePath()==QFileInfo(target).canonicalFilePath()) return true;
    if(QFileInfo::exists(target) || !QFile::copy(source.toLocalFile(),target)) return false;
    copied.append(target); return true;
  };
  return (copy(m_systemAudioSource,"system-0001.wav") && copy(m_microphoneAudioSource,"microphone-0001.wav")) || fail(tr("Cannot copy audio media"));
}
bool ProjectController::saveRecovery(QUrl directory) {
  if(!hasProject() || !directory.isLocalFile() || QFileInfo::exists(directory.toLocalFile())) return false;
  // An isolated model reuses all project validation and atomic serialization.
  // Saving it cannot move the live project or acknowledge its edits as saved.
  ProjectController snapshot;
  snapshot.m_clips=m_clips; snapshot.m_source=m_source; snapshot.m_sourceDuration=m_sourceDuration;
  snapshot.m_cameraSource=m_cameraSource; snapshot.m_cameraSettings=m_cameraSettings;
  snapshot.m_systemAudioSource=m_systemAudioSource; snapshot.m_microphoneAudioSource=m_microphoneAudioSource;
  snapshot.m_audioSettings=m_audioSettings; snapshot.m_canvasSettings=m_canvasSettings;
  snapshot.m_framingSettings=m_framingSettings; snapshot.m_pointerSamples=m_pointerSamples;
  snapshot.m_autoZoom=m_autoZoom; snapshot.m_manualFraming=m_manualFraming;
  snapshot.m_markers=m_markers; snapshot.m_exportSettings=m_exportSettings;
  if (!snapshot.saveAs(directory))
    return fail(snapshot.error());
  return true;
}
bool ProjectController::renameProject(QString name) {
  name=name.trimmed();
  if(m_location.isEmpty() || name.isEmpty() || name=="." || name==".." || name.contains(QRegularExpression("[<>:\"/\\\\|?*]")) || name.endsWith('.')) return fail(tr("Invalid project name"));
  if(!name.endsWith(".dogscproject",Qt::CaseInsensitive)) name+=".dogscproject";
  const QString old=m_location.toLocalFile(); const QString target=QDir(QFileInfo(old).absolutePath()).filePath(name);
  if(QDir::cleanPath(old)==QDir::cleanPath(target)) return true;
  if(QFileInfo::exists(target) || !QDir().rename(old,target)) return fail(tr("Cannot rename project"));
  auto remapUrl=[&](QUrl &url) { if(url.isLocalFile() && url.toLocalFile().startsWith(old+"/",Qt::CaseInsensitive)) url=QUrl::fromLocalFile(target+url.toLocalFile().mid(old.size())); };
  remapUrl(m_source); remapUrl(m_cameraSource); remapUrl(m_systemAudioSource); remapUrl(m_microphoneAudioSource);
  auto remapCanvas=[&](QVariantMap &canvas) {
    for(const auto &key:{"backgroundImage","backgroundVideo"}) { const QString path=canvas.value(key).toString(); if(path.startsWith(old+"/",Qt::CaseInsensitive)) canvas[key]=target+path.mid(old.size()); }
    auto stickers=canvas.value("stickers").toList();
    for(auto &value:stickers) { auto sticker=value.toMap(); const QString path=sticker.value("imagePath").toString(); if(path.startsWith(old+"/",Qt::CaseInsensitive)) sticker["imagePath"]=target+path.mid(old.size()); value=sticker; }
    if(canvas.contains("stickers")) canvas["stickers"]=stickers;
  };
  remapCanvas(m_canvasSettings); remapCanvas(m_baselineCanvasSettings);
  for(auto &state:m_undo) remapCanvas(state.canvasSettings);
  for(auto &state:m_redo) remapCanvas(state.canvasSettings);
  m_location=QUrl::fromLocalFile(target); m_error.clear(); emit changed(); return true;
}

double ProjectController::cameraTimeAt(double seconds) const {
  return CameraMotion::sourceToCameraTime(m_cameraSettings, sourceTime(seconds));
}
double ProjectController::cameraRateAt(double seconds) const {
  const double source = sourceTime(seconds);
  return std::max(.01, (CameraMotion::sourceToCameraTime(m_cameraSettings, source + .001) -
                        CameraMotion::sourceToCameraTime(m_cameraSettings, source)) / .001);
}

QString ProjectController::addSticker(QUrl image,double sourceStart,double length) {
  if(!hasProject() || !image.isLocalFile() || !validTime(sourceStart) || !validTime(length) ||
     sourceStart<0 || length<=0 || sourceStart+length>m_sourceDuration+1e-6 || !m_stickerCache.isValid()) {
    fail(tr("Invalid sticker image or time")); return {};
  }
  QImageReader reader(image.toLocalFile());
  const QImage decoded=reader.read();
  if(reader.supportsAnimation() || decoded.isNull()) { fail(tr("Sticker must be a static image")); return {}; }
  const QString id=uid();
  const QString cached=QDir(m_stickerCache.path()).filePath(id+".png");
  if(!decoded.save(cached,"PNG")) { fail(tr("Cannot cache sticker image")); return {}; }
  QVariantMap sticker{{"id",id},{"imagePath",cached},{"sourceStart",sourceStart},{"sourceDuration",length},
    {"x",.5},{"y",.5},{"width",.38},{"rotation",0.0},{"opacity",1.0},{"radius",0.0},
    {"borderWidth",0.0},{"borderColor","#ffffff"},{"shadow",true},{"enterDuration",std::min(.3,length*.5)},{"exitDuration",std::min(.2,length*.5)}};
  auto canvas=m_canvasSettings; auto stickers=canvas.value("stickers").toList(); stickers.append(sticker); canvas["stickers"]=stickers;
  QString error;
  if(!CanvasRenderer::validate(canvas,&error)) { QFile::remove(cached); fail(error); return {}; }
  pushHistory(); m_redo.clear(); m_canvasSettings=canvas; m_error.clear(); emit changed(); return id;
}
bool ProjectController::updateSticker(QString id,QVariantMap changes) {
  auto canvas=m_canvasSettings; auto stickers=canvas.value("stickers").toList();
  for(auto &value:stickers) {
    auto sticker=value.toMap(); if(sticker.value("id").toString()!=id) continue;
    for(auto it=changes.cbegin();it!=changes.cend();++it) {
      if(it.key()=="id" || it.key()=="imagePath" || !sticker.contains(it.key())) return fail(tr("Unknown or immutable sticker option"));
      sticker[it.key()]=it.value();
    }
    bool startOk=false,lengthOk=false;
    const double start=sticker.value("sourceStart").toDouble(&startOk),length=sticker.value("sourceDuration").toDouble(&lengthOk);
    if(!startOk || !lengthOk || !validTime(start) || !validTime(length) || start<0 || length<=0 || start+length>m_sourceDuration+1e-6)
      return fail(tr("Invalid sticker time"));
    if(sticker==value.toMap()) return true;
    value=sticker; canvas["stickers"]=stickers; QString error;
    if(!CanvasRenderer::validate(canvas,&error)) return fail(error);
    pushHistory(); m_redo.clear(); m_canvasSettings=canvas; m_error.clear(); emit changed(); return true;
  }
  return fail(tr("Sticker not found"));
}
bool ProjectController::removeSticker(QString id) {
  auto stickers=m_canvasSettings.value("stickers").toList();
  for(int i=0;i<stickers.size();++i) if(stickers[i].toMap().value("id").toString()==id) {
    pushHistory(); m_redo.clear(); stickers.removeAt(i); m_canvasSettings["stickers"]=stickers;
    m_error.clear(); emit changed(); return true;
  }
  return false;
}
bool ProjectController::prepareStickerAssets(const QString &directory,QVariantList &stickers,QStringList &copied) {
  stickers=m_canvasSettings.value("stickers").toList();
  if(stickers.isEmpty()) return true;
  const QString base=QFileInfo(directory).canonicalFilePath();
  if(!QDir().mkpath(QDir(directory).filePath("assets"))) return fail(tr("Cannot create sticker assets directory"));
  const QString assets=QFileInfo(QDir(directory).filePath("assets")).canonicalFilePath();
  if(base.isEmpty() || assets.isEmpty() || !assets.startsWith(base+"/",Qt::CaseInsensitive)) return fail(tr("Sticker assets directory outside project"));
  for(auto &value:stickers) {
    auto sticker=value.toMap(); const QString id=sticker.value("id").toString();
    const QString source=QFileInfo(sticker.value("imagePath").toString()).canonicalFilePath();
    QImageReader reader(source);
    if(QUuid(id).isNull() || source.isEmpty() || !QFileInfo(source).isFile() || reader.supportsAnimation() || reader.read().isNull()) return fail(tr("Invalid sticker image"));
    // All imported assets are normalized to PNG in the owned immutable cache.
    // Older/internal image formats keep their extension when copied.
    const QString suffix=QFileInfo(source).suffix().toLower();
    const QString relative="assets/sticker-"+id+(suffix.isEmpty()?QString{}:"."+suffix);
    const QString target=QDir(directory).filePath(relative);
    if(source!=QFileInfo(target).canonicalFilePath()) {
      if(QFileInfo::exists(target) || !QFile::copy(source,target)) return fail(tr("Cannot copy sticker image"));
      copied.append(target);
    }
    sticker["imagePath"]=relative; value=sticker;
  }
  return true;
}
void ProjectController::useSavedStickers(const QString &directory) {
  auto remap=[&](QVariantMap &canvas) {
    if(!canvas.contains("stickers")) return;
    auto stickers=canvas.value("stickers").toList();
    for(auto &value:stickers) {
      auto sticker=value.toMap(); const QString source=sticker.value("imagePath").toString();
      const QString suffix=QFileInfo(source).suffix().toLower();
      const QString relative="assets/sticker-"+sticker.value("id").toString()+(suffix.isEmpty()?QString{}:"."+suffix);
      const QString target=QDir(directory).filePath(relative);
      if(QFileInfo(target).isFile()) sticker["imagePath"]=QFileInfo(target).canonicalFilePath();
      value=sticker;
    }
    canvas["stickers"]=stickers;
  };
  remap(m_canvasSettings); remap(m_baselineCanvasSettings);
  for(auto &state:m_undo) remap(state.canvasSettings);
  for(auto &state:m_redo) remap(state.canvasSettings);
}
