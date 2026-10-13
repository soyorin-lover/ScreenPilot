#pragma once
#include <QString>
class QGuiApplication;
class QObject;
class ProjectController;
class RecentProjectsController;
int runRecentUiSmokeTest(QGuiApplication &, QObject *, ProjectController &,
                         RecentProjectsController &, const QString &,
                         const QString &);
