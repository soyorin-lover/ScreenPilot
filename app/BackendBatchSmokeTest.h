#pragma once
#include <QString>
class QGuiApplication;
int runBackendBatchSmokeTest(QGuiApplication &, const QString &source, const QString &directory);
