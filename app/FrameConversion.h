#pragma once
#include <QImage>
#include <QVideoFrame>

// CPU frame to QImage conversion for the QPainter compositor.
// QVideoFrame::toImage() in Qt 6.8 costs 10-15 ms per 1440p frame. Planar
// 8-bit YUV (the software FFmpeg decoder output) is converted here with
// fixed-point math split across worker threads; any other layout, rotation or
// mirroring falls back to toImage(). The result is opaque Format_RGB32.
namespace FrameConversion {
QImage toImage(const QVideoFrame &frame);
// True when toImage() would take the fast path for this frame's format.
bool isFastPath(const QVideoFrameFormat &format);
} // namespace FrameConversion
