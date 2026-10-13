#pragma once
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
class ProjectController;
class RecoveryController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
  Q_PROPERTY(bool blocked READ blocked WRITE setBlocked NOTIFY changed)
  Q_PROPERTY(QVariantList snapshots READ snapshots NOTIFY changed)
  Q_PROPERTY(QUrl lastSnapshot READ lastSnapshot NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
public:
  explicit RecoveryController(ProjectController *, QString directory = {}, QObject *parent = nullptr);
  bool enabled() const { return m_enabled; }
  bool blocked() const { return m_blocked; }
  void setEnabled(bool);
  void setBlocked(bool);
  QVariantList snapshots() const;
  QUrl lastSnapshot() const { return m_last; }
  QString error() const { return m_error; }
  Q_INVOKABLE bool snapshotNow();
  Q_INVOKABLE bool restoreSnapshot(QUrl);
  Q_INVOKABLE bool removeSnapshot(QUrl);
signals:
  void changed();
private:
  bool owns(const QString &) const;
  ProjectController *m_project;
  QString m_directory, m_error;
  QUrl m_last;
  QTimer m_timer;
  bool m_enabled = true, m_blocked = false, m_writing = false;
};
