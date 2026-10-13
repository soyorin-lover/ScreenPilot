#pragma once
#include <QtGlobal>

// QPC aligns the stream once; device frame positions preserve PCM continuity.
class AudioPacketTimeline {
  bool anchored = false;
  quint64 deviceAnchor = 0;
  qint64 timelineAnchor = 0;
  qint64 previousEnd = 0;
  quint64 previousPosition = 0;

public:
  qint64 place(qint64 timestampFrame, quint64 devicePosition, quint32 frames,
               bool timestampError, quint32 deviceRate = 48000) {
    if (!anchored || devicePosition < previousPosition) {
      timelineAnchor =
          anchored && timestampError ? previousEnd : timestampFrame;
      deviceAnchor = devicePosition;
      anchored = true;
    }
    // Shared-mode conversion can return 48kHz PCM while the endpoint position
    // still advances in its native frame units (e.g. 441 per 480 PCM frames).
    const qint64 first = timelineAnchor +
        qRound64(double(devicePosition - deviceAnchor) * 48000 /
                 qMax(quint32(1), deviceRate));
    previousPosition = devicePosition;
    previousEnd = first + frames;
    return first;
  }
};
