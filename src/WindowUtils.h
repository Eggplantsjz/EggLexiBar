#pragma once

#include <QRect>
#include <QSize>

/// Rectangle of the window that currently has the keyboard focus, in Qt
/// logical (device independent) coordinates. Empty when unavailable.
QRect activeWindowRect();

/// Rectangle centred on the mouse cursor.
QRect cursorRegionRect(const QSize& size);

/// Bounding rectangle of the screen that contains the cursor.
QRect screenRectAtCursor();
