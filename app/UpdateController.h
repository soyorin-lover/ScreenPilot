#pragma once
#include <QObject>
#include <QUrl>
#include <QNetworkAccessManager>
class AppSettings;
class QNetworkReply;
class UpdateController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
  Q_PROPERTY(QUrl releaseUrl READ releaseUrl NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
  explicit UpdateController(AppSettings *settings, bool autoChecksEnabled = true, QObject *parent = nullptr);
  QString status() const { return m_status; }
  QString latestVersion() const { return m_latestVersion; }
  QUrl releaseUrl() const { return m_releaseUrl; }
  QString error() const { return m_error; }
  bool busy() const { return m_reply != nullptr; }
  Q_INVOKABLE void checkForUpdates();
signals:
  void changed();
private:
  void maybeAutoCheck();
  void finish(QNetworkReply *reply);
  AppSettings *m_settings;
  QNetworkAccessManager m_network;
  QNetworkReply *m_reply = nullptr;
  QString m_status = "idle", m_latestVersion, m_error;
  QUrl m_releaseUrl;
  bool m_autoChecksEnabled;
};
