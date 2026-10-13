#pragma once
#include <QObject>
#include <QVariantMap>
#include <QUrl>
class AppSettings final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString effectiveTheme READ effectiveTheme NOTIFY changed)
  Q_PROPERTY(QString effectiveLanguage READ effectiveLanguage NOTIFY changed)
  Q_PROPERTY(QString localeName READ localeName NOTIFY changed)
  Q_PROPERTY(QUrl projectDirectoryUrl READ projectDirectoryUrl NOTIFY changed)
  Q_PROPERTY(QUrl exportDirectoryUrl READ exportDirectoryUrl NOTIFY changed)
public:
  explicit AppSettings(QObject *parent = nullptr);
  QVariantMap values() const;
  QString error() const { return m_error; }
  QString effectiveTheme() const;
  QString effectiveLanguage() const;
  QString localeName() const;
  QUrl projectDirectoryUrl() const;
  QUrl exportDirectoryUrl() const;
  Q_INVOKABLE bool setValue(QString key, QVariant value);
  Q_INVOKABLE bool reset();
  Q_INVOKABLE QUrl suggestedProjectUrl(QString name = QString());
  Q_INVOKABLE QUrl suggestedExportUrl(QString name = QString(), QString suffix = "mp4");
  Q_INVOKABLE bool notifyCompletion();
signals:
  void changed();
protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
private:
  static QVariantMap defaults();
  QUrl suggest(const QString &directory, QString name, const QString &suffix);
  QString m_error;
};
