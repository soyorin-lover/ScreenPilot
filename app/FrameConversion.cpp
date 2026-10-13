#include "FrameConversion.h"
#include <QThreadPool>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <semaphore>
#include <vector>
#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#define FRAMECONVERSION_X86 1
#endif

namespace {
// 16.16 fixed-point YCbCr -> R'G'B' coefficients.
struct Matrix {
  int yOffset, y, rv, gu, gv, bu;
};
Matrix matrixFor(const QVideoFrameFormat &format) {
  double kr = .2126, kb = .0722; // BT.709
  auto space = format.colorSpace();
  if (space == QVideoFrameFormat::ColorSpace_Undefined)
    space = format.frameHeight() > 576 ? QVideoFrameFormat::ColorSpace_BT709
                                       : QVideoFrameFormat::ColorSpace_BT601;
  if (space == QVideoFrameFormat::ColorSpace_BT601) kr = .299, kb = .114;
  else if (space == QVideoFrameFormat::ColorSpace_BT2020) kr = .2627, kb = .0593;
  const bool full = format.colorRange() == QVideoFrameFormat::ColorRange_Full;
  const double kg = 1 - kr - kb;
  const double ys = full ? 1. : 255. / 219., cs = full ? 1. : 255. / 224.;
  auto fixed = [](double v) { return int(std::lround(v * 65536)); };
  return {full ? 0 : 16, fixed(ys), fixed(cs * 2 * (1 - kr)),
          fixed(cs * 2 * kb * (1 - kb) / kg), fixed(cs * 2 * kr * (1 - kr) / kg),
          fixed(cs * 2 * (1 - kb))};
}

struct Planes {
  const uchar *y, *u, *v; // u/v share one plane for semi-planar layouts
  int yStride, cStride, cStep; // cStep: 1 planar, 2 interleaved
  int width, height;
  Matrix m;
  QImage *out;
};

#ifdef FRAMECONVERSION_X86
bool hasAvx2() {
  static const bool available = [] {
    int info[4]{};
    __cpuid(info, 0);
    if (info[0] < 7) return false;
    __cpuid(info, 1);
    const bool osxsave = info[2] & (1 << 27), avx = info[2] & (1 << 28);
    if (!osxsave || !avx || (_xgetbv(0) & 6) != 6) return false;
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
  }();
  return available;
}
#endif

// Packs eight pixels. cu/cv carry chroma minus 128, scaled by 16.
#ifdef FRAMECONVERSION_X86
#if defined(__clang__) || defined(__GNUC__)
__attribute__((target("avx2")))
#endif
int pixelsAvx2(const Matrix &m, const uchar *y, const int32_t *cu, const int32_t *cv,
               QRgb *dst, int width) {
  const __m256i yOffset = _mm256_set1_epi32(m.yOffset), ys = _mm256_set1_epi32(m.y),
                round = _mm256_set1_epi32(32768), rv = _mm256_set1_epi32(m.rv),
                gu = _mm256_set1_epi32(m.gu), gv = _mm256_set1_epi32(m.gv),
                bu = _mm256_set1_epi32(m.bu), zero = _mm256_setzero_si256(),
                max = _mm256_set1_epi32(255), alpha = _mm256_set1_epi32(int(0xff000000u));
  int x = 0;
  for (; x + 8 <= width; x += 8) {
    const __m256i luma = _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(y + x)));
    const __m256i yy = _mm256_add_epi32(_mm256_mullo_epi32(_mm256_sub_epi32(luma, yOffset), ys), round);
    const __m256i u = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(cu + x));
    const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(cv + x));
    auto channel = [&](__m256i value) {
      return _mm256_min_epi32(_mm256_max_epi32(_mm256_srai_epi32(value, 16), zero), max);
    };
    const __m256i r = channel(_mm256_add_epi32(yy, _mm256_srai_epi32(_mm256_mullo_epi32(rv, v), 4)));
    const __m256i g = channel(_mm256_sub_epi32(yy, _mm256_srai_epi32(
        _mm256_add_epi32(_mm256_mullo_epi32(gu, u), _mm256_mullo_epi32(gv, v)), 4)));
    const __m256i b = channel(_mm256_add_epi32(yy, _mm256_srai_epi32(_mm256_mullo_epi32(bu, u), 4)));
    const __m256i argb = _mm256_or_si256(_mm256_or_si256(alpha, _mm256_slli_epi32(r, 16)),
                                         _mm256_or_si256(_mm256_slli_epi32(g, 8), b));
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + x), argb);
  }
  return x;
}
#endif

// Rows [first, last). Chroma is bilinearly upsampled with centered siting and
// edge clamping, matching Qt's texture-based converter.
void convertRows(const Planes &p, int first, int last) {
  const Matrix m = p.m;
  const int cw = (p.width + 1) / 2, ch = (p.height + 1) / 2;
  thread_local std::vector<int32_t> vu, vv, cu, cv;
  vu.resize(cw); vv.resize(cw); cu.resize(p.width + 8); cv.resize(p.width + 8);
#ifdef FRAMECONVERSION_X86
  const bool avx2 = hasAvx2();
#endif
  for (int row = first; row < last; ++row) {
    const int ci = row >> 1;
    const int cn = row & 1 ? std::min(ci + 1, ch - 1) : std::max(ci - 1, 0);
    const uchar *u0 = p.u + qsizetype(ci) * p.cStride, *u1 = p.u + qsizetype(cn) * p.cStride;
    const uchar *v0 = p.v + qsizetype(ci) * p.cStride, *v1 = p.v + qsizetype(cn) * p.cStride;
    const int step = p.cStep;
    for (int k = 0; k < cw; ++k) {
      vu[k] = 3 * u0[k * step] + u1[k * step];
      vv[k] = 3 * v0[k * step] + v1[k * step];
    }
    for (int k = 0; k < cw; ++k) {
      const int l = std::max(k - 1, 0), r = std::min(k + 1, cw - 1);
      const int x = 2 * k;
      cu[x] = 3 * vu[k] + vu[l] - 2048;
      cv[x] = 3 * vv[k] + vv[l] - 2048;
      cu[x + 1] = 3 * vu[k] + vu[r] - 2048;
      cv[x + 1] = 3 * vv[k] + vv[r] - 2048;
    }
    const uchar *y = p.y + qsizetype(row) * p.yStride;
    auto *dst = reinterpret_cast<QRgb *>(p.out->scanLine(row));
    int x = 0;
#ifdef FRAMECONVERSION_X86
    if (avx2) x = pixelsAvx2(m, y, cu.data(), cv.data(), dst, p.width);
#endif
    for (; x < p.width; ++x) {
      const int yy = (int(y[x]) - m.yOffset) * m.y + 32768;
      auto clamp8 = [](int v) { return uint(std::clamp(v >> 16, 0, 255)); };
      dst[x] = 0xff000000u | clamp8(yy + (m.rv * cv[x] >> 4)) << 16 |
               clamp8(yy - ((m.gu * cu[x] + m.gv * cv[x]) >> 4)) << 8 |
               clamp8(yy + (m.bu * cu[x] >> 4));
    }
  }
}

QThreadPool &pool() {
  static QThreadPool instance;
  static const bool configured = [] {
    instance.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 1, 1, 6));
    instance.setExpiryTimeout(10000);
    return true;
  }();
  Q_UNUSED(configured);
  return instance;
}

// Bands are claimed from a shared counter by the caller and pool helpers alike,
// so the caller never waits on a band nobody started (no pool starvation).
struct Job {
  Planes planes;
  int bands = 1, rowsPerBand = 0;
  std::atomic<int> next{0}, done{0};
  std::binary_semaphore finished{0};
  void work() {
    for (int band; (band = next.fetch_add(1)) < bands;) {
      const int first = band * rowsPerBand;
      convertRows(planes, first, std::min(planes.height, first + rowsPerBand));
      if (done.fetch_add(1) + 1 == bands) finished.release();
    }
  }
};
} // namespace

bool FrameConversion::isFastPath(const QVideoFrameFormat &format) {
  switch (format.pixelFormat()) {
  case QVideoFrameFormat::Format_YUV420P:
  case QVideoFrameFormat::Format_YV12:
  case QVideoFrameFormat::Format_NV12:
  case QVideoFrameFormat::Format_NV21:
    break;
  default:
    return false;
  }
  return format.rotation() == QtVideo::Rotation::None && !format.isMirrored() &&
         format.colorSpace() != QVideoFrameFormat::ColorSpace_AdobeRgb &&
         format.colorTransfer() != QVideoFrameFormat::ColorTransfer_ST2084 &&
         format.colorTransfer() != QVideoFrameFormat::ColorTransfer_STD_B67;
}

QImage FrameConversion::toImage(const QVideoFrame &source) {
  if (!source.isValid()) return {};
  const QVideoFrameFormat format = source.surfaceFormat();
  if (!isFastPath(format)) return source.toImage();
  QVideoFrame frame = source;
  if (!frame.map(QVideoFrame::ReadOnly)) return source.toImage();
  const int width = frame.width(), height = frame.height();
  QImage out(width, height, QImage::Format_RGB32);
  if (out.isNull() || width < 2 || height < 2) {
    frame.unmap();
    return source.toImage();
  }
  const auto pixel = format.pixelFormat();
  const bool semi = pixel == QVideoFrameFormat::Format_NV12 || pixel == QVideoFrameFormat::Format_NV21;
  Planes planes{frame.bits(0), nullptr, nullptr, frame.bytesPerLine(0),
                frame.bytesPerLine(1), semi ? 2 : 1, width, height, matrixFor(format), &out};
  if (semi) {
    const uchar *uv = frame.bits(1);
    planes.u = pixel == QVideoFrameFormat::Format_NV12 ? uv : uv + 1;
    planes.v = pixel == QVideoFrameFormat::Format_NV12 ? uv + 1 : uv;
  } else {
    const bool yv12 = pixel == QVideoFrameFormat::Format_YV12;
    planes.u = frame.bits(yv12 ? 2 : 1);
    planes.v = frame.bits(yv12 ? 1 : 2);
  }
  if (!planes.y || !planes.u || !planes.v) {
    frame.unmap();
    return source.toImage();
  }
  // Small frames (thumbnails, camera bubbles) are not worth a thread hop.
  const int helpers = qint64(width) * height >= 640 * 360 ? pool().maxThreadCount() : 0;
  if (!helpers) {
    convertRows(planes, 0, height);
  } else {
    auto job = std::make_shared<Job>();
    job->planes = planes;
    job->bands = std::min(height / 2, (helpers + 1) * 2);
    job->rowsPerBand = ((height + job->bands - 1) / job->bands + 1) & ~1;
    job->bands = (height + job->rowsPerBand - 1) / job->rowsPerBand;
    for (int i = 0; i < std::min(helpers, job->bands - 1); ++i)
      pool().start([job] { job->work(); });
    job->work();
    job->finished.acquire();
  }
  frame.unmap();
  return out;
}
