#pragma once
#include <QString>
int runCursorSmokeTest(const QString &directory);

class QGuiApplication;
int runCursorExportSmokeTest(QGuiApplication &, const QString &source,
                             const QString &directory);
