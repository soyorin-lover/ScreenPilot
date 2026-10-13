#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantList>

class RecentProjectsController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
  Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY projectsChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
  explicit RecentProjectsController(QObject *parent = nullptr, const QString &settingsFile = {});
  QVariantList projects() const;
  QVariantList recentProjects() const;
  QString error() const { return m_error; }
  Q_INVOKABLE bool remember(const QUrl &folder, const QString &title = {});
  Q_INVOKABLE bool removeEntry(const QUrl &folder);
  Q_INVOKABLE bool clearHistory();
  Q_INVOKABLE void refresh();
signals:
  void projectsChanged();
  void errorChanged();
private:
  struct Entry { QString path; QString title; };
  QString normalized(const QUrl &folder) const;
  bool persist(const QList<Entry> &entries);
  bool fail(const QString &message);
  void clearError();
  QList<Entry> m_entries;
  QString m_settingsFile;
  QString m_error;
};
