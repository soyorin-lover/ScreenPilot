#include "CaptureExclusion.h"
#include <QWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

bool excludeWindowFromCapture(QWindow *window) {
  if (!window) return false;
#ifdef Q_OS_WIN
  // WDA_EXCLUDEFROMCAPTURE requires Windows 10 2004+. Older Windows can
  // accept it as WDA_MONITOR, so verify the affinity rather than just BOOL.
  const HWND handle = reinterpret_cast<HWND>(window->winId());
  constexpr DWORD excludeFromCapture = 0x00000011;
  DWORD affinity = 0;
  return SetWindowDisplayAffinity(handle, excludeFromCapture) &&
         GetWindowDisplayAffinity(handle, &affinity) && affinity == excludeFromCapture;
#else
  return false;
#endif
}
