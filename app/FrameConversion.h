#pragma once
#include <QImage>
#include <QVideoFrame>

// CPU frame to QImage conversion for the QPainter compositor.
// QVideoFrame::toImage() in Qt 6.8 costs 10-15 ms per 1440p frame. Planar
// 8-bit YUV (the software FFmpeg decoder output) is converted here with
// fixed-point math split across worker threads; any other layout, rotation or
// mirroring falls back to toImage(). The result is opaque Format_RGB32.
namespace FrameConversion {
// parallel=false keeps the work on the calling thread, for callers that already
// run one conversion per worker (export) and must not oversubscribe the CPU.
QImage toImage(const QVideoFrame &frame, bool parallel = true);
// True when toImage() would take the fast path for this frame's format.
bool isFastPath(const QVideoFrameFormat &format);
// Opaque 32-bit RGB to a limited-range NV12 frame for encoding. Qt's encoder
// otherwise converts BGRA on its single encoding thread with the BT.601 matrix,
// which halves 1440p throughput. Qt 6.8 does not write colour tags, so the
// matrix follows what players assume for untagged video: BT.709 above 576
// lines, BT.601 otherwise (the frame is tagged accordingly). Odd sizes and
// other formats are wrapped unchanged.
QVideoFrame toNV12(const QImage &image, bool parallel = true);
} // namespace FrameConversion
