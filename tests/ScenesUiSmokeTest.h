#pragma once
#include <QString>
class QGuiApplication;
class QObject;
class ProjectController;
class ScenePresetController;
int runScenesUiSmokeTest(QGuiApplication &, QObject *, ProjectController &,
                         ScenePresetController &, const QString &,
                         const QString &);
