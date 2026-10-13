#include "AppSettings.h"
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace {
QString moviesDirectory() {
  auto path = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
  if (path.isEmpty()) path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  return path;
}
bool directoryValid(const QString &path) {
  if (path.trimmed().isEmpty() || !QDir::isAbsolutePath(path)) return false;
  QFileInfo info(QDir::cleanPath(path));
  if (info.exists()) return info.isDir() && info.isWritable();
  QFileInfo parent(info.absolutePath());
  while (!parent.exists()) {
    const QString next = parent.absolutePath();
    if (next == parent.absoluteFilePath()) return false;
    parent.setFile(next);
  }
  return parent.isDir() && parent.isWritable();
}
}
AppSettings::AppSettings(QObject *parent) : QObject(parent) {
  if (qApp) {
    qApp->installEventFilter(this);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { emit changed(); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { emit changed(); });
  }
}
QVariantMap AppSettings::defaults() {
  return {{"language","system"},{"theme","system"},{"previewQuality","high"},
    {"completionSound",true},{"autoCheckUpdates",true},
    {"projectDirectory",QDir(moviesDirectory()).filePath("ScreenPilot/Projects")},
    {"exportDirectory",moviesDirectory()}};
}
QVariantMap AppSettings::values() const {
  auto result=defaults(); QSettings settings;
  for(auto it=result.begin();it!=result.end();++it) {
    auto saved=settings.value("application/"+it.key(),it.value());
    // Preserve a user's selected destination if it temporarily becomes
    // unavailable; saving must report failure rather than silently relocate.
    if(it.key().endsWith("Directory") && (saved.toString().trimmed().isEmpty() || !QDir::isAbsolutePath(saved.toString()))) continue;
    if(it.key()=="language" && !QStringList{"system","zh","en"}.contains(saved.toString())) continue;
    if(it.key()=="theme" && !QStringList{"system","light","dark"}.contains(saved.toString())) continue;
    if(it.key()=="previewQuality" && !QStringList{"high","medium","low"}.contains(saved.toString())) continue;
    if((it.key()=="completionSound" || it.key()=="autoCheckUpdates") && saved.metaType().id()!=QMetaType::Bool) {
      // INI-backed settings can restore booleans as literal strings.
      if(saved.toString()=="true") saved=true;
      else if(saved.toString()=="false") saved=false;
      else continue;
    }
    it.value()=saved;
  }
  return result;
}
QString AppSettings::effectiveTheme() const {
  const auto theme=values().value("theme").toString();
  return theme=="system" ? (QGuiApplication::styleHints()->colorScheme()==Qt::ColorScheme::Dark ? "dark" : "light") : theme;
}
QString AppSettings::effectiveLanguage() const {
  const auto language=values().value("language").toString();
  return language=="system" ? (QLocale::system().language()==QLocale::Chinese ? "zh" : "en") : language;
}
QString AppSettings::localeName() const {
  return values().value("language").toString()=="system" ? QLocale::system().name() : (effectiveLanguage()=="zh" ? "zh_CN" : "en_US");
}
QUrl AppSettings::projectDirectoryUrl() const { return QUrl::fromLocalFile(values().value("projectDirectory").toString()); }
QUrl AppSettings::exportDirectoryUrl() const { return QUrl::fromLocalFile(values().value("exportDirectory").toString()); }
bool AppSettings::eventFilter(QObject *watched,QEvent *event) {
  if(event->type()==QEvent::LocaleChange || event->type()==QEvent::ThemeChange) emit changed();
  return QObject::eventFilter(watched,event);
}
bool AppSettings::setValue(QString key,QVariant value) {
  const auto known=defaults();
  if(!known.contains(key)) {m_error=tr("Unknown setting");emit changed();return false;}
  bool valid=true;
  if(key=="language") valid=QStringList{"system","zh","en"}.contains(value.toString());
  else if(key=="theme") valid=QStringList{"system","light","dark"}.contains(value.toString());
  else if(key=="previewQuality") valid=QStringList{"high","medium","low"}.contains(value.toString());
  else if(key.endsWith("Directory")) {
    QString path=value.toString();
    if(path.startsWith("file:")) path=QUrl(path).toLocalFile();
    valid=directoryValid(path); value=QDir::cleanPath(path);
  } else valid=value.metaType().id()==QMetaType::Bool;
  if(!valid) {m_error=tr("Invalid setting or unavailable directory");emit changed();return false;}
  QSettings settings;settings.setValue("application/"+key,value);settings.sync();
  if(settings.status()!=QSettings::NoError){m_error=tr("Could not save settings");emit changed();return false;}
  m_error.clear();emit changed();return true;
}
bool AppSettings::reset() {
  QSettings settings;settings.remove("application");settings.sync();
  m_error=settings.status()==QSettings::NoError?QString():tr("Could not reset settings");emit changed();return m_error.isEmpty();
}
QUrl AppSettings::suggest(const QString &directory,QString name,const QString &suffix) {
  if (!suffix.isEmpty() && name.endsWith("." + suffix, Qt::CaseInsensitive)) name.chop(suffix.size() + 1);
  if(name.isEmpty()) name="ScreenPilot";
  if(name=="." || name==".." || name.endsWith('.') || name.endsWith(' ') || name.contains(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]")) ||
      name.contains(QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption)) ||
      (!suffix.isEmpty() && !suffix.contains(QRegularExpression("^[A-Za-z0-9]{1,16}$")))) {
    m_error=tr("Invalid file name");emit changed();return {};
  }
  QDir dir(directory);
  if(!QDir().mkpath(directory)) {
    m_error=tr("Could not create the configured directory");emit changed();return {};
  }
  const QString extension=suffix.isEmpty()?QString():"."+suffix;
  QString candidate=dir.filePath(name+extension);
  for(int n=2;QFileInfo::exists(candidate);++n) {
    if(n>10000) {m_error=tr("No unused file name available");emit changed();return {};}
    candidate=dir.filePath(name+" ("+QString::number(n)+")"+extension);
  }
  m_error.clear();emit changed();return QUrl::fromLocalFile(candidate);
}
QUrl AppSettings::suggestedProjectUrl(QString name) {return suggest(values().value("projectDirectory").toString(),name,"spproj");}
QUrl AppSettings::suggestedExportUrl(QString name,QString suffix) {return suggest(values().value("exportDirectory").toString(),name,suffix);}
bool AppSettings::notifyCompletion() {
  if(!values().value("completionSound").toBool()) return false;
#ifdef Q_OS_WIN
  // MessageBeep follows the user's Windows sound scheme and current endpoint volume.
  return MessageBeep(MB_OK)!=FALSE;
#else
  return false;
#endif
}
