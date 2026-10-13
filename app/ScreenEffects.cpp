#include "ScreenEffects.h"
#include <QMatrix4x4>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStringList>
#include <QVector3D>
#include <QCache>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>

namespace {
const QStringList keys{"screenX", "screenY", "screenScale", "screenRotationX", "screenRotationY", "screenRotationZ", "screenPerspective"};
bool bounded(const QVariantMap &map, const QString &key, double low, double high, double fallback) {
  bool ok; const double value = map.value(key, fallback).toDouble(&ok);
  return ok && std::isfinite(value) && value >= low && value <= high;
}
bool poseValid(const QVariantMap &map) {
  return bounded(map,"screenX",-1,1,0) && bounded(map,"screenY",-1,1,0) &&
    bounded(map,"screenScale",.1,3,1) && bounded(map,"screenRotationX",-80,80,0) &&
    bounded(map,"screenRotationY",-80,80,0) && bounded(map,"screenRotationZ",-180,180,0) &&
    bounded(map,"screenPerspective",1,10,3);
}
bool interval(const QVariantMap &map) {
  return bounded(map,"sourceStart",0,86400,0) && bounded(map,"sourceDuration",.001,86400,1);
}
double smooth(double value) { value = std::clamp(value,0.,1.); return value*value*(3-2*value); }
QImage stickerImage(const QString &path) {
  static QMutex mutex;
  static QCache<QString, QImage> cache(64 * 1024); // decoded KiB
  QMutexLocker lock(&mutex);
  const QFileInfo info(path);
  const QString key=info.absoluteFilePath()+":"+QString::number(info.size())+":"+QString::number(info.lastModified().toMSecsSinceEpoch());
  if (const QImage *image=cache.object(key)) return *image;
  QImageReader reader(path); reader.setAutoTransform(true);
  const QSize dimensions=reader.size();
  if (!dimensions.isValid() || dimensions.width()>8192 || dimensions.height()>8192) return {};
  if (dimensions.width()>2048 || dimensions.height()>2048) {
    QSize decodeSize=dimensions; decodeSize.scale(2048,2048,Qt::KeepAspectRatio);
    reader.setScaledSize(decodeSize);
  }
  QImage image=reader.read();
  if (!image.isNull()) cache.insert(key,new QImage(image),int(std::max<qsizetype>(1,image.sizeInBytes()/1024)));
  return image;
}
}
QVariantMap ScreenEffects::defaults() {
  return {{"screenX",0.}, {"screenY",0.}, {"screenScale",1.},
    {"screenRotationX",0.}, {"screenRotationY",0.}, {"screenRotationZ",0.},
    {"screenPerspective",3.}, {"blurRegions",QVariantList{}}, {"screenMotionSegments",QVariantList{}}, {"stickers",QVariantList{}}};
}
bool ScreenEffects::validate(const QVariantMap &settings, QString *error) {
  auto fail = [error] { if (error) *error = QStringLiteral("Invalid screen transform or timed effect region."); return false; };
  if (!poseValid(settings)) return fail();
  if (settings.contains("stickers") && settings.value("stickers").metaType().id()!=QMetaType::QVariantList) return fail();
  if (settings.value("stickers").toList().size()>64) return fail();
  for (const QVariant &entry : settings.value("stickers").toList()) {
    if (entry.metaType().id()!=QMetaType::QVariantMap) return fail();
    const auto map=entry.toMap();
    if (!interval(map) || map.value("imagePath").metaType().id()!=QMetaType::QString ||
        map.value("imagePath").toString().isEmpty() ||
        !bounded(map,"x",0,1,.5) || !bounded(map,"y",0,1,.5) ||
        !bounded(map,"width",.01,2,.2) || !bounded(map,"rotation",-180,180,0) ||
        !bounded(map,"opacity",0,1,1) || !bounded(map,"radius",0,300,0) ||
        !bounded(map,"borderWidth",0,50,0) || !bounded(map,"shadow",0,1,0) ||
        !bounded(map,"enterDuration",0,8,.15) || !bounded(map,"exitDuration",0,8,.15) ||
        !QColor(map.value("borderColor","#ffffff").toString()).isValid()) return fail();
  }
  for (const QString &key : {QStringLiteral("blurRegions"),QStringLiteral("screenMotionSegments")}) {
    if (settings.contains(key) && settings.value(key).metaType().id() != QMetaType::QVariantList) return fail();
    if (settings.value(key).toList().size() > 256) return fail();
    for (const QVariant &entry : settings.value(key).toList()) {
      if (entry.metaType().id() != QMetaType::QVariantMap) return fail();
      const auto map = entry.toMap();
      if (!interval(map)) return fail();
      if (key == "screenMotionSegments") {
        if (map.value("from").metaType().id() != QMetaType::QVariantMap ||
            map.value("to").metaType().id() != QMetaType::QVariantMap ||
            !poseValid(map.value("from").toMap()) || !poseValid(map.value("to").toMap())) return fail();
      } else {
        if (!bounded(map,"x",0,1,0) || !bounded(map,"y",0,1,0) ||
            !bounded(map,"width",.001,1,1) || !bounded(map,"height",.001,1,1) ||
            map.value("x",0).toDouble()+map.value("width",1).toDouble()>1.0000001 ||
            map.value("y",0).toDouble()+map.value("height",1).toDouble()>1.0000001 ||
            !bounded(map,"strength",0,1,.7) || !bounded(map,"enterDuration",0,8,.15) ||
            !bounded(map,"exitDuration",0,8,.15) ||
            !QStringList{"blur","spotlight"}.contains(map.value("style","blur").toString())) return fail();
      }
    }
  }
  if (error) error->clear(); return true;
}
QVariantMap ScreenEffects::evaluate(const QVariantMap &settings, double seconds) {
  auto pose = defaults();
  for (const auto &key : keys) if (settings.contains(key)) pose[key] = settings[key];
  for (const auto &entry : settings.value("screenMotionSegments").toList()) {
    const auto segment=entry.toMap(); const double start=segment.value("sourceStart",0).toDouble();
    const double duration=segment.value("sourceDuration",1).toDouble();
    if (seconds < start || seconds > start+duration) continue;
    const double t=smooth((seconds-start)/duration);
    const auto from=segment.value("from").toMap(), to=segment.value("to").toMap();
    for (const auto &key : keys) {
      const double a=from.value(key,pose[key]).toDouble(), b=to.value(key,pose[key]).toDouble();
      pose[key]=a+(b-a)*t;
    }
  }
  return pose;
}
QTransform ScreenEffects::projection(QRectF shell, QSize canvas, const QVariantMap &settings, double seconds) {
  const auto pose=evaluate(settings,seconds);
  QMatrix4x4 rotation;
  rotation.rotate(float(pose.value("screenRotationZ").toDouble()),0,0,1);
  rotation.rotate(float(pose.value("screenRotationY").toDouble()),0,1,0);
  rotation.rotate(float(pose.value("screenRotationX").toDouble()),1,0,0);
  const double distance=std::max(shell.width(),shell.height())*pose.value("screenPerspective").toDouble();
  const double scale=pose.value("screenScale").toDouble();
  const QPointF offset(pose.value("screenX").toDouble()*canvas.width(),pose.value("screenY").toDouble()*canvas.height());
  const QPolygonF original{shell.topLeft(),shell.topRight(),shell.bottomRight(),shell.bottomLeft()};
  QPolygonF projected;
  for (const auto &point : original) {
    const auto relative=point-shell.center();
    const QVector3D p=rotation*QVector3D(float(relative.x()),float(relative.y()),0);
    const double factor=distance/(distance-p.z());
    projected << shell.center()+offset+QPointF(p.x()*factor*scale,p.y()*factor*scale);
  }
  QTransform result;
  QTransform::quadToQuad(original,projected,result);
  return result;
}
QImage ScreenEffects::applyRegions(const QImage &source, const QVariantMap &settings, double seconds) {
  QImage result=source;
  for (const auto &entry : settings.value("blurRegions").toList()) {
    const auto map=entry.toMap(); const double start=map.value("sourceStart",0).toDouble(), duration=map.value("sourceDuration",1).toDouble();
    if (seconds < start || seconds >= start+duration) continue;
    const double enter=map.value("enterDuration",.15).toDouble(), exit=map.value("exitDuration",.15).toDouble();
    const double opacity=std::min(enter>0?smooth((seconds-start)/enter):1.,exit>0?smooth((start+duration-seconds)/exit):1.);
    const double strength=map.value("strength",.7).toDouble();
    if (opacity<=0 || strength<=0) continue;
    const QRectF rect(map.value("x",0).toDouble()*source.width(),map.value("y",0).toDouble()*source.height(),map.value("width",1).toDouble()*source.width(),map.value("height",1).toDouble()*source.height());
    if (map.value("style","blur").toString()=="spotlight") {
      QPainter painter(&result); QPainterPath outside; outside.addRect(result.rect()); outside.addRoundedRect(rect,12,12);
      outside.setFillRule(Qt::OddEvenFill); painter.fillPath(outside,QColor(0,0,0,qRound(220*strength*opacity)));
    } else {
      const QRect pixels=rect.toAlignedRect().intersected(result.rect());
      const QImage patch=result.copy(pixels);
      // Bounded downsample/reconstruction approximation, not a Mac blur kernel.
      const int factor=std::max(2,qRound(2+strength*30));
      const QImage blurred=patch.scaled(std::max(1,patch.width()/factor),std::max(1,patch.height()/factor),Qt::IgnoreAspectRatio,Qt::SmoothTransformation)
          .scaled(patch.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
      QPainter painter(&result); painter.setOpacity(opacity); painter.drawImage(pixels.topLeft(),blurred);
    }
  }
  return result;
}
void ScreenEffects::paintStickers(QPainter &painter, QSize canvas, const QVariantMap &settings, double seconds) {
  const double unit=double(std::min(canvas.width(),canvas.height()))/1080.;
  for (const auto &entry : settings.value("stickers").toList()) {
    const auto map=entry.toMap();
    const double start=map.value("sourceStart",0).toDouble(), duration=map.value("sourceDuration",1).toDouble();
    if (seconds<start || seconds>=start+duration) continue;
    const double enter=map.value("enterDuration",.15).toDouble(), exit=map.value("exitDuration",.15).toDouble();
    const double fade=std::min(enter>0?smooth((seconds-start)/enter):1.,exit>0?smooth((start+duration-seconds)/exit):1.);
    const double opacity=fade*map.value("opacity",1).toDouble();
    if (opacity<=0) continue;
    const QImage image=stickerImage(map.value("imagePath").toString());
    if (image.isNull()) continue;
    const double width=map.value("width",.2).toDouble()*canvas.width();
    const double height=width*image.height()/image.width();
    const QRectF rect(-width/2,-height/2,width,height);
    const double radius=std::min(map.value("radius",0).toDouble()*unit,std::min(width,height)/2);
    painter.save();
    painter.translate(map.value("x",.5).toDouble()*canvas.width(),map.value("y",.5).toDouble()*canvas.height());
    painter.rotate(map.value("rotation",0).toDouble());
    painter.scale(.85+.15*fade,.85+.15*fade); painter.setOpacity(opacity);
    const double shadow=map.value("shadow",0).toDouble();
    painter.setPen(Qt::NoPen);
    for (int i=8;shadow>0 && i>=1;--i) {
      const double spread=i*2*unit; painter.setBrush(QColor(0,0,0,qRound(255*shadow*.06)));
      painter.drawRoundedRect(rect.translated(0,6*unit).adjusted(-spread,-spread,spread,spread),radius+spread,radius+spread);
    }
    QPainterPath clip; clip.addRoundedRect(rect,radius,radius);
    painter.save(); painter.setClipPath(clip); painter.drawImage(rect,image); painter.restore();
    const double border=map.value("borderWidth",0).toDouble()*unit;
    if (border>0) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(QColor(map.value("borderColor","#ffffff").toString()),border)); painter.drawRoundedRect(rect,radius,radius); }
    painter.restore();
  }
}
