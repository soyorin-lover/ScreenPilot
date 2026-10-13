#pragma once
#include <QString>
class QGuiApplication;
class QObject;
class ProjectController;
class ScenePresetController;
int runVisualUiSmokeTest(QGuiApplication &, QObject *, ProjectController &,
                         ScenePresetController &, const QString &,
                         const QString &);
