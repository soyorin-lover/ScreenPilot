#include "MemoSmokeTest.h"
#include "MemoController.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <limits>
int runMemoSmokeTest(const QString &directory) {
  QTemporaryDir isolated;
  if (!isolated.isValid()) return 2;
  const QString settingsPath = isolated.filePath("memo.ini");
  bool defaults = false, persisted = false, clamped = false, geometry = false;
  bool excluded = false;
  {
    MemoController memo(settingsPath);
    defaults = memo.text().isEmpty() && memo.fontSize() == 22 && memo.backgroundOpacity() == .92;
    memo.setText(QStringLiteral("Generated memo smoke text"));
    memo.setFontSize(100); memo.setBackgroundOpacity(-1);
    clamped = memo.fontSize() == 40 && memo.backgroundOpacity() == .55;
    memo.setFontSize(std::numeric_limits<double>::quiet_NaN());
    memo.setBackgroundOpacity(std::numeric_limits<double>::infinity());
    clamped = clamped && memo.fontSize() == 22 && memo.backgroundOpacity() == .92;
    memo.setFontSize(28); memo.setBackgroundOpacity(.8);
    QWindow window;
    memo.prepareWindow(&window);
    excluded = memo.captureExcluded();
    geometry = window.width() >= 280 && window.height() >= 240;
    memo.saveWindow(&window);
  }
  {
    MemoController memo(settingsPath);
    persisted = memo.text() == QStringLiteral("Generated memo smoke text") && memo.fontSize() == 28 && memo.backgroundOpacity() == .8 && !memo.editing();
  }
  const bool passed = defaults && persisted && clamped && geometry;
  QDir().mkpath(directory);
  QFile report(QDir(directory).filePath("memo-smoke-report.json"));
  if (!report.open(QIODevice::WriteOnly)) return 2;
  report.write(QJsonDocument(QJsonObject{{"passed",passed},{"defaults",defaults},{"persistence",persisted},{"bounds",clamped},{"geometry",geometry},{"captureExclusionConfirmed",excluded}}).toJson());
  return passed ? 0 : 1;
}
