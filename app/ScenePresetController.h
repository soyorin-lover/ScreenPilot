#pragma once
#include <QObject>
#include <QVariantList>
class ProjectController;
class ScenePresetController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList presets READ presets NOTIFY changed)
  Q_PROPERTY(QString defaultPresetId READ defaultPresetId NOTIFY changed)
  Q_PROPERTY(QString activePresetId READ activePresetId NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
public:
  explicit ScenePresetController(ProjectController *project, QString storageDirectory = {}, QObject *parent = nullptr);
  QVariantList presets() const { return m_presets; }
  QString defaultPresetId() const { return m_default; }
  QString activePresetId() const { return m_active; }
  QString error() const { return m_error; }
  Q_INVOKABLE bool savePreset(QString name);
  Q_INVOKABLE bool updatePreset(QString id, QString name);
  Q_INVOKABLE bool deletePreset(QString id);
  Q_INVOKABLE bool setDefaultPreset(QString id);
  Q_INVOKABLE bool resetDefault() { return setDefaultPreset({}); }
  Q_INVOKABLE bool applyPreset(QString id);
  Q_INVOKABLE bool applyDefault();
signals:
  void changed();
private:
  ProjectController *m_project;
  QString m_directory, m_default, m_active, m_error;
  QVariantList m_presets;
  bool fail(QString message);
  bool persist(const QVariantList &presets, const QString &defaultId);
  bool capture(QString id, QString name);
  int indexOf(const QString &id) const;
};
