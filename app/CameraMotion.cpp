#include "CameraMotion.h"
#include <QVariantList>
#include <QStringList>
#include <algorithm>
#include <cmath>
namespace {
bool number(const QVariantMap &map,const QString &key,double low,double high) {
  bool ok=false; const double value=map.value(key).toDouble(&ok);
  return ok && std::isfinite(value) && value>=low && value<=high;
}
bool shape(const QString &value) {
  return QStringList{"circle","rounded","rectangle","landscape","portrait","source"}.contains(value);
}
double smooth(double value) {
  value=std::clamp(value,0.0,1.0); return value*value*(3-2*value);
}
}
namespace CameraMotion {
bool validate(const QVariantMap &settings,QString *error) {
  auto fail=[&](const char *message) { if(error) *error=QString::fromLatin1(message); return false; };
  if(settings.contains("motionSegments")) {
    if(settings.value("motionSegments").metaType().id()!=QMetaType::QVariantList) return fail("Invalid camera motion segments");
    double previousEnd=-1;
    for(const auto &value:settings.value("motionSegments").toList()) {
      if(value.metaType().id()!=QMetaType::QVariantMap) return fail("Invalid camera motion segment");
      const auto segment=value.toMap();
      if(!number(segment,"start",0,86400000) || !number(segment,"end",0,86400000) ||
         !number(segment,"x",0,1) || !number(segment,"y",0,1) || !number(segment,"size",.001,1) ||
         !number(segment,"enter",0,60) || !number(segment,"exit",0,60) ||
         segment.value("fullscreen").metaType().id()!=QMetaType::Bool || !shape(segment.value("shape").toString()))
        return fail("Invalid camera motion parameters");
      const double start=segment.value("start").toDouble(),end=segment.value("end").toDouble();
      if(end<=start || start<previousEnd || segment.value("enter").toDouble()+segment.value("exit").toDouble()>end-start)
        return fail("Camera motion segments must be ordered, non-overlapping, and have valid transitions");
      previousEnd=end;
    }
  }
  if(settings.contains("syncPoints")) {
    if(settings.value("syncPoints").metaType().id()!=QMetaType::QVariantList) return fail("Invalid camera synchronization points");
    double previousSource=-1,previousCamera=-1;
    for(const auto &value:settings.value("syncPoints").toList()) {
      if(value.metaType().id()!=QMetaType::QVariantMap) return fail("Invalid camera synchronization point");
      const auto point=value.toMap();
      if(!number(point,"sourceTime",0,86400000) || !number(point,"cameraTime",0,86400000)) return fail("Invalid camera synchronization time");
      const double source=point.value("sourceTime").toDouble(),camera=point.value("cameraTime").toDouble();
      if(source<=previousSource || camera<=previousCamera) return fail("Camera synchronization points must increase monotonically");
      previousSource=source; previousCamera=camera;
    }
  }
  return true;
}
QVariantMap evaluate(const QVariantMap &settings,double time) {
  auto result=settings;
  if(!std::isfinite(time) || !validate(settings)) return result;
  for(const auto &value:settings.value("motionSegments").toList()) {
    const auto segment=value.toMap();
    const double start=segment.value("start").toDouble(),end=segment.value("end").toDouble();
    if(time<start || time>=end) continue;
    const double enter=segment.value("enter").toDouble(),exit=segment.value("exit").toDouble();
    double weight=1;
    if(enter>0) weight=std::min(weight,smooth((time-start)/enter));
    if(exit>0) weight=std::min(weight,smooth((end-time)/exit));
    for(const auto &key:{"x","y","width"}) {
      const double base=settings.value(key,QString(key)=="width"?.29:.68).toDouble();
      const double target=segment.value(QString(key)=="width"?"size":key).toDouble();
      result[key]=base+(target-base)*weight;
    }
    const double baseFullscreen=settings.value("fullscreen",false).toBool()?1:0;
    const double targetFullscreen=segment.value("fullscreen").toBool()?1:0;
    result["fullscreen"]=false;
    result["fullscreenProgress"]=baseFullscreen+(targetFullscreen-baseFullscreen)*weight;
    if(weight>=.5) result["shape"]=segment.value("shape");
    return result;
  }
  return result;
}
double sourceToCameraTime(const QVariantMap &settings,double time) {
  if(!std::isfinite(time)) return 0;
  double offset=settings.value("offset",0.0).toDouble();
  if(!std::isfinite(offset)) offset=0;
  if(!validate(settings)) return time-offset;
  const auto points=settings.value("syncPoints").toList();
  if(points.isEmpty()) return time-offset;
  const auto first=points.first().toMap(),last=points.last().toMap();
  if(time<=first.value("sourceTime").toDouble()) return first.value("cameraTime").toDouble()+time-first.value("sourceTime").toDouble()-offset;
  if(time>=last.value("sourceTime").toDouble()) return last.value("cameraTime").toDouble()+time-last.value("sourceTime").toDouble()-offset;
  for(int i=1;i<points.size();++i) {
    const auto left=points[i-1].toMap(),right=points[i].toMap();
    const double start=left.value("sourceTime").toDouble(),end=right.value("sourceTime").toDouble();
    if(time<=end) return left.value("cameraTime").toDouble()+(time-start)/(end-start)*(right.value("cameraTime").toDouble()-left.value("cameraTime").toDouble())-offset;
  }
  return time-offset;
}
}
