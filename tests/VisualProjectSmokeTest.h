#pragma once
#include <QString>
class QGuiApplication;
int runVisualProjectSmokeTest(QGuiApplication &, const QString &directory, const QString &sourcePath = {});
