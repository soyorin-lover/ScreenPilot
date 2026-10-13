#pragma once
#include <QQuickPaintedItem>
#include "CanvasPreview.h"
#include <QPointer>
class CropPreview : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(CanvasPreview *sourcePreview READ sourcePreview WRITE setSourcePreview NOTIFY sourcePreviewChanged)
  Q_PROPERTY(QRectF imageRect READ imageRect NOTIFY imageRectChanged)
public:
  explicit CropPreview(QQuickItem *parent = nullptr);
  CanvasPreview *sourcePreview() const { return m_source; }
  void setSourcePreview(CanvasPreview *source);
  QRectF imageRect() const;
  void paint(QPainter *painter) override;
signals:
  void sourcePreviewChanged();
  void imageRectChanged();
private:
  QPointer<CanvasPreview> m_source;
};
