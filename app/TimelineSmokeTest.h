#pragma once
#include <QString>
class QGuiApplication;
int verifyTimelineMedia(QGuiApplication &app, const QString &sourcePath,
                        const QString &outputDirectory = {});
