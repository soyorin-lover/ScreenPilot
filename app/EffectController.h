#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>
class ProjectController;

// All times supplied to editing commands are source seconds, except addAtOutput().
class EffectController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList tracks READ tracks CONSTANT)
  Q_PROPERTY(QVariantList screenMotionSegments READ screenMotionSegments NOTIFY changed)
  Q_PROPERTY(QVariantList blurRegions READ blurRegions NOTIFY changed)
  Q_PROPERTY(QVariantList cameraMotionSegments READ cameraMotionSegments NOTIFY changed)
  Q_PROPERTY(QVariantList syncPoints READ syncPoints NOTIFY changed)
  Q_PROPERTY(QVariantList stickers READ stickers NOTIFY changed)
  Q_PROPERTY(QVariantMap selectedIds READ selectedIds NOTIFY changed)
  Q_PROPERTY(QVariantMap selectedItems READ selectedItems NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
public:
  explicit EffectController(ProjectController *project, QObject *parent = nullptr);
  QVariantList tracks() const;
  Q_INVOKABLE QVariantList items(QString track) const;
  QVariantList screenMotionSegments() const { return items("screenMotionSegments"); }
  QVariantList blurRegions() const { return items("blurRegions"); }
  QVariantList cameraMotionSegments() const { return items("cameraMotionSegments"); }
  QVariantList syncPoints() const { return items("syncPoints"); }
  QVariantList stickers() const { return items("stickers"); }
  Q_INVOKABLE QString selectedId(QString track) const;
  Q_INVOKABLE QVariantMap selectedItem(QString track) const;
  Q_INVOKABLE bool select(QString track, QString id);
  Q_INVOKABLE QString add(QString track, double sourceSeconds, double duration = 1);
  Q_INVOKABLE QString addAtOutput(QString track, double outputSeconds, double sourceDuration = 1);
  Q_INVOKABLE QString addSyncAtOutput(double outputSeconds);
  Q_INVOKABLE QUrl localFileUrl(QString path) const { return QUrl::fromLocalFile(path); }
  Q_INVOKABLE QString addSticker(QUrl image, double sourceSeconds, double duration = 1);
  Q_INVOKABLE bool update(QString track, QString id, QVariantMap patch);
  Q_INVOKABLE bool updateSelected(QString track, QVariantMap patch);
  Q_INVOKABLE bool remove(QString track, QString id);
  Q_INVOKABLE bool removeSelected(QString track);
  Q_INVOKABLE double sourceTime(double outputSeconds) const;
  // Every visible occurrence is returned when clips repeat/reorder a source range.
  Q_INVOKABLE QVariantList outputTimes(double sourceSeconds) const;
  QString error() const { return m_error; }
  QVariantMap selectedIds() const;
  QVariantMap selectedItems() const;
signals:
  void changed();
private:
  ProjectController *m_project;
  QMap<QString, QString> m_selection;
  QString m_error;
  bool fail(QString message);
  bool write(QString track, QVariantList list);
  bool validTimes(QString track, const QVariantMap &item) const;
  void reconcile();
};
