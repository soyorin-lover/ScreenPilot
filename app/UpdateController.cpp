#include "UpdateController.h"
#include "AppSettings.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
namespace {
constexpr qint64 MaxResponse = 256 * 1024;
struct Version { QList<quint64> numbers; QStringList prerelease; bool valid=false; };
Version parseVersion(QString text) {
  static const QRegularExpression pattern("^v?(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\\+[0-9A-Za-z.-]+)?$");
  auto match=pattern.match(text); Version result;
  if(!match.hasMatch()) return result;
  for(int i=1;i<=3;++i) {bool ok=false;auto n=match.captured(i).toULongLong(&ok);if(!ok)return {};result.numbers.append(n);}
  if(!match.captured(4).isEmpty()) {
    result.prerelease=match.captured(4).split('.');
    for(const auto &part:result.prerelease) {
      if(part.isEmpty() || (part.size()>1 && part.startsWith('0') && part.contains(QRegularExpression("^[0-9]+$")))) return {};
    }
  }
  result.valid=true;return result;
}
int compare(const Version &a,const Version &b) {
  for(int i=0;i<3;++i) if(a.numbers[i]!=b.numbers[i]) return a.numbers[i]>b.numbers[i]?1:-1;
  if(a.prerelease.isEmpty()!=b.prerelease.isEmpty())return a.prerelease.isEmpty()?1:-1;
  for(int i=0;i<qMin(a.prerelease.size(),b.prerelease.size());++i) {
    const auto x=a.prerelease[i],y=b.prerelease[i];if(x==y)continue;
    const bool xn=x.contains(QRegularExpression("^[0-9]+$")),yn=y.contains(QRegularExpression("^[0-9]+$"));
    if(xn!=yn)return xn?-1:1;
    if(xn && x.size()!=y.size())return x.size()>y.size()?1:-1;
    return x>y?1:-1;
  }
  return a.prerelease.size()==b.prerelease.size()?0:(a.prerelease.size()>b.prerelease.size()?1:-1);
}
}
UpdateController::UpdateController(AppSettings *settings,bool autoChecksEnabled,QObject *parent)
  : QObject(parent),m_settings(settings),m_network(this),m_autoChecksEnabled(autoChecksEnabled) {
  connect(settings,&AppSettings::changed,this,&UpdateController::maybeAutoCheck);
  QTimer::singleShot(0,this,&UpdateController::maybeAutoCheck);
}
void UpdateController::maybeAutoCheck() {
  if(!m_autoChecksEnabled || busy() || !m_settings->values().value("autoCheckUpdates").toBool())return;
  const auto now=QDateTime::currentSecsSinceEpoch();
  const auto last=QSettings().value("updates/lastAttempt",0).toLongLong();
  if(last>0 && now-last<24*60*60)return;
  checkForUpdates();
}
void UpdateController::checkForUpdates() {
  if(busy())return;
  // Persist attempts, including failures, so transient outages do not cause request loops.
  QSettings settings;settings.setValue("updates/lastAttempt",QDateTime::currentSecsSinceEpoch());settings.sync();
  m_status="checking";m_error.clear();m_latestVersion.clear();m_releaseUrl=QUrl();
  QNetworkRequest request(QUrl("https://api.github.com/repos/soyorin-lover/ScreenPilot/releases/latest"));
  request.setRawHeader("Accept","application/vnd.github+json");
  request.setRawHeader("User-Agent","ScreenPilot-update-check");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::SameOriginRedirectPolicy);
  request.setMaximumRedirectsAllowed(3);
  request.setTransferTimeout(10000);
  m_reply=m_network.get(request);
  auto reply=m_reply;
  connect(reply,&QNetworkReply::downloadProgress,this,[reply](qint64 received,qint64 total) {
    if(received>MaxResponse || total>MaxResponse) {reply->setProperty("oversized",true);reply->abort();}
  });
  connect(reply,&QIODevice::readyRead,this,[reply] {
    if(reply->bytesAvailable()>MaxResponse) {reply->setProperty("oversized",true);reply->abort();}
  });
  // Absolute deadline also bounds responses that keep trickling bytes.
  QTimer::singleShot(15000,reply,[reply] {if(!reply->isFinished()){reply->setProperty("deadline",true);reply->abort();}});
  connect(reply,&QNetworkReply::finished,this,[this,reply]{finish(reply);});
  emit changed();
}
void UpdateController::finish(QNetworkReply *reply) {
  const auto http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto body=reply->read(MaxResponse+1);
  m_reply=nullptr;
  auto fail=[this](QString message){m_status="error";m_error=message;};
  if(reply->property("oversized").toBool() || body.size()>MaxResponse)fail(tr("Update response exceeded the size limit"));
  else if(reply->property("deadline").toBool())fail(tr("Update check timed out"));
  else if(http==404) {m_status="unavailable";m_error=tr("No published stable release is available, or the repository is unavailable");}
  else if(reply->error()!=QNetworkReply::NoError || http!=200)fail(tr("Update check failed (HTTP %1): %2").arg(http).arg(reply->errorString()));
  else {
    QJsonParseError jsonError;const auto document=QJsonDocument::fromJson(body,&jsonError);const auto object=document.object();
    const auto tag=object.value("tag_name").toString();const auto latest=parseVersion(tag);
    QString currentText=QCoreApplication::applicationVersion();if(currentText.isEmpty())currentText="0.1.0-dev";
    const auto current=parseVersion(currentText);const QUrl url(object.value("html_url").toString());
    if(jsonError.error!=QJsonParseError::NoError || !document.isObject() || !latest.valid || !current.valid ||
        object.value("draft").toBool(true) || object.value("prerelease").toBool(true) ||
        !latest.prerelease.isEmpty() || url.scheme()!="https" || url.host()!="github.com" ||
        !url.path().startsWith("/soyorin-lover/ScreenPilot/releases/tag/") || !url.userInfo().isEmpty() || url.port(-1)!=-1)
      fail(tr("Release metadata is invalid or not an eligible stable release"));
    else {m_latestVersion=tag;m_releaseUrl=url;m_status=compare(latest,current)>0?"available":"upToDate";}
  }
  reply->deleteLater();emit changed();
}
