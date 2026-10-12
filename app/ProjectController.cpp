#include "ProjectController.h"
#include "AutoFraming.h"
#include "CanvasRenderer.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
namespace {
QString uid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validTime(double v) { return std::isfinite(v); }
} // namespace
ProjectController::ProjectController(QObject *parent)
    : QObject(parent), m_canvasSettings(CanvasRenderer::defaults()),
      m_baselineCanvasSettings(m_canvasSettings) {}

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
void ProjectController::setSelectedClip(int i) {
  if (i >= 0 && i < m_clips.size() && i != m_selected) {
    m_selected = i;
    emit changed();
  }
}
void ProjectController::commit(const Snapshot &s, int i) {
  pushHistory();
  if (m_undo.size() > 100)
    m_undo.removeFirst();
  m_redo.clear();
  m_clips = s;
  m_selected = i;
  m_error.clear();
  m_status = tr("剪辑已更新");
  emit changed();
}
bool ProjectController::newRecording(QUrl url, double seconds,
                                     QVariantList samples) {
  if (dirty())
    return fail(tr("请先保存或显式丢弃当前修改"));
  if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile() ||
      !validTime(seconds) || seconds <= 0)
    return fail(tr("录制文件或时长无效"));
  m_canvasSettings = CanvasRenderer::defaults();
  m_baselineCanvasSettings = m_canvasSettings;
  m_pointerSamples.clear();
  for (const auto &sample : samples)
    if (sample.toMap().value("time").toDouble() <= seconds)
      m_pointerSamples.append(sample);
  m_framing = AutoFraming::generate(m_pointerSamples, seconds);
  m_autoZoom = false;
  m_baselineAutoZoom = false;
  m_source = url;
  m_sourceDuration = seconds;
  m_clips = {{uid(), 0, seconds, 1}};
  m_baseline.clear();
  m_location = QUrl{};
  m_selected = 0;
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
  m_redo.append({m_clips, m_autoZoom, m_canvasSettings});
  auto state = m_undo.takeLast();
  m_clips = state.clips;
  m_autoZoom = state.autoZoom;
  m_canvasSettings = state.canvasSettings;
  m_selected = std::clamp(m_selected, 0, int(m_clips.size()) - 1);
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
  m_selected = std::clamp(m_selected, 0, int(m_clips.size()) - 1);
  m_error.clear();
  emit changed();
}
void ProjectController::discardChanges() {
  m_baseline = m_clips;
  m_baselineAutoZoom = m_autoZoom;
  m_baselineCanvasSettings = m_canvasSettings;
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
  canvas["backgroundImage"] = backgroundRelative;
  QJsonArray segments;
  for (const auto &c : m_clips)
    segments.append(QJsonObject{{"id", c.id},
                                {"sourceStart", c.start},
                                {"sourceDuration", c.length},
                                {"playbackRate", c.rate}});
  QJsonObject root{
      {"producer", "ScreenPilot"},
      {"formatVersion", 3},
      {"screenPilot",
       QJsonObject{
           {"sourceDuration", m_sourceDuration},
           {"pointerSamples", QJsonArray::fromVariantList(m_pointerSamples)},
           {"autoZoomEnabled", m_autoZoom},
           {"canvasSettings", QJsonObject::fromVariantMap(canvas)}}},
      {"media",
       QJsonObject{
           {"screen", QJsonObject{{"relativePath", "media/screen-0001.mp4"}}}}},
      {"timeline",
       QJsonObject{{"sourceSequence",
                    QJsonObject{{"kind", "edited"}, {"segments", segments}}}}}};
  QSaveFile f(QDir(dir).filePath("project.json"));
  if (!f.open(QIODevice::WriteOnly))
    return fail(f.errorString());
  auto data = QJsonDocument(root).toJson();
  if (f.write(data) != data.size() || !f.commit())
    return fail(f.errorString());
  return true;
}
void ProjectController::saved(const QString &dir) {
  m_location = QUrl::fromLocalFile(dir);
  m_source = QUrl::fromLocalFile(QDir(dir).filePath("media/screen-0001.mp4"));
  m_baseline = m_clips;
  m_baselineAutoZoom = m_autoZoom;
  m_baselineCanvasSettings = m_canvasSettings;
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
  if (!QFile::copy(m_source.toLocalFile(), media) ||
      !prepareBackground(dir, relative, copied) || !writeJson(dir, relative)) {
    if (!copied.isEmpty())
      QFile::remove(copied);
    QDir(dir).rmdir("assets");
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
  if (!f.open(QIODevice::ReadOnly))
    return fail(f.errorString());
  QJsonParseError pe;
  auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject())
    return fail(tr("工程 JSON 无效"));
  auto root = doc.object();
  if (root.value("producer").toString() != "ScreenPilot" ||
      (root.value("formatVersion").toDouble() != 1 &&
       root.value("formatVersion").toDouble() != 2 &&
       root.value("formatVersion").toDouble() != 3))
    return fail(tr("仅支持 ScreenPilot 内部工程版本 1/2/3；Mac "
                   "工程兼容尚未验收，拒绝编辑以保护未知字段"));
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
        c.length <= 0 || c.rate != 1 || !validTime(c.start + c.length) ||
        c.start + c.length > total + 1e-6)
      return fail(tr("片段时间、标识或速度无效"));
    if (!clips.isEmpty() &&
        c.start < clips.last().start + clips.last().length - 1e-6)
      return fail(tr("当前版本不支持重排或重叠源片段"));
    ids.insert(c.id);
    clips.append(c);
  }
  if (clips.isEmpty())
    return fail(tr("工程没有片段"));
  const auto settings = root.value("screenPilot").toObject();
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
    canvas = settings.value("canvasSettings").toObject().toVariantMap();
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
    QString validationError;
    if (!CanvasRenderer::validate(canvas, &validationError))
      return fail(validationError);
  }
  m_canvasSettings = canvas;
  m_pointerSamples = samples;
  m_framing = AutoFraming::generate(samples, total);
  m_autoZoom =
      settings.value("autoZoomEnabled").toBool(false) && !m_framing.isEmpty();
  m_baselineAutoZoom = m_autoZoom;
  m_baselineCanvasSettings = m_canvasSettings;
  m_clips = clips;
  m_baseline = clips;
  m_sourceDuration = total;
  m_source = QUrl::fromLocalFile(media);
  m_location = QUrl::fromLocalFile(dir);
  m_selected = 0;
  m_unsaved = false;
  m_undo.clear();
  m_redo.clear();
  m_error.clear();
  m_status = tr("内部工程已打开；Mac 兼容尚未验收");
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
  m_undo.append({m_clips, m_autoZoom, m_canvasSettings});
  if (m_undo.size() > 100)
    m_undo.removeFirst();
}
void ProjectController::setCanvasOption(QString key, QVariant value) {
  if (!hasProject())
    return;
  if (key == "backgroundImage") {
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
bool ProjectController::prepareBackground(const QString &dir, QString &relative,
                                          QString &copied) {
  QString error;
  if (!CanvasRenderer::validate(m_canvasSettings, &error))
    return fail(error);
  const QString source = m_canvasSettings.value("backgroundImage").toString();
  if (source.isEmpty())
    return true;
  QImageReader reader(source);
  if (reader.supportsAnimation() || reader.read().isNull())
    return fail(tr("背景图片缺失或无法解码，工程未保存"));
  if (!QDir().mkpath(QDir(dir).filePath("assets")))
    return fail(tr("无法创建背景资源目录"));
  const QString base = QFileInfo(dir).canonicalFilePath();
  const QString assets =
      QFileInfo(QDir(dir).filePath("assets")).canonicalFilePath();
  if (base.isEmpty() || assets.isEmpty() ||
      !assets.startsWith(base + "/", Qt::CaseInsensitive))
    return fail(tr("背景资源目录越出工程目录"));
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
  const QString previous = m_canvasSettings.value("backgroundImage").toString();
  auto remap = [&](QVariantMap &settings) {
    if (settings.value("backgroundImage").toString() == previous)
      settings["backgroundImage"] = path;
  };
  for (auto &state : m_undo)
    remap(state.canvasSettings);
  for (auto &state : m_redo)
    remap(state.canvasSettings);
  remap(m_baselineCanvasSettings);
  m_canvasSettings["backgroundImage"] = path;
}
