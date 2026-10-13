#pragma once
#include <QString>
class QGuiApplication;
class QObject;
class ProjectController;
int verifyEditorShortcuts(QGuiApplication &app, QObject *root,
                          ProjectController &project, const QString &sourcePath,
                          const QString &outputDirectory = {});
