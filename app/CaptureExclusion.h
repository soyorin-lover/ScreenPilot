#pragma once
class QWindow;

// Asks Windows to leave this window out of screen capture (our own recorder
// and others that honour display affinity). Requires Windows 10 2004+; returns
// whether the exclusion was confirmed.
bool excludeWindowFromCapture(QWindow *window);
