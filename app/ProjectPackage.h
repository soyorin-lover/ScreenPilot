#pragma once
#include <QString>

// A project is a folder package. New packages use ".spproj"; folders saved by
// earlier builds as ".dogscproject" are still recognised and opened in place.
namespace ProjectPackage {
inline QString suffix() { return QStringLiteral(".spproj"); }
inline QString legacySuffix() { return QStringLiteral(".dogscproject"); }
// The package suffix the name already carries, or an empty string.
inline QString suffixOf(const QString &name) {
  if (name.endsWith(suffix(), Qt::CaseInsensitive)) return suffix();
  if (name.endsWith(legacySuffix(), Qt::CaseInsensitive)) return legacySuffix();
  return {};
}
inline QString withSuffix(const QString &name) {
  return suffixOf(name).isEmpty() ? name + suffix() : name;
}
} // namespace ProjectPackage
