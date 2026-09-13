/************************************************************************
    MeOS - Orienteering Software
    Linux port: system colours and cursors (monitors and metrics follow in
    step 1.2.5).

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_ui.h"

#include <array>

namespace {

// Cursor handles are the resource ids of the system cursors.
constexpr std::uintptr_t cursorArrow = 32512;
constexpr std::uintptr_t cursorIBeam = 32513;
constexpr std::uintptr_t cursorWait = 32514;
constexpr std::uintptr_t cursorCross = 32515;
constexpr std::uintptr_t cursorSizeAll = 32646;
constexpr std::uintptr_t cursorNo = 32648;
constexpr std::uintptr_t cursorHand = 32649;
constexpr std::uintptr_t cursorAppStarting = 32650;

HCURSOR currentCursor = reinterpret_cast<HCURSOR>(cursorArrow);
// The wait cursor is an override cursor, so that it shows during long operations
// without event processing.
bool waitOverride = false;

Qt::CursorShape cursorShape(HCURSOR cursor) {
  switch (reinterpret_cast<std::uintptr_t>(cursor)) {
  case 0: return Qt::BlankCursor;
  case cursorIBeam: return Qt::IBeamCursor;
  case cursorWait: return Qt::WaitCursor;
  case cursorCross: return Qt::CrossCursor;
  case cursorSizeAll: return Qt::SizeAllCursor;
  case cursorNo: return Qt::ForbiddenCursor;
  case cursorHand: return Qt::PointingHandCursor;
  case cursorAppStarting: return Qt::BusyCursor;
  default: return Qt::ArrowCursor;
  }
}

void endWaitOverride() {
  if (waitOverride) {
    QApplication::restoreOverrideCursor();
    waitOverride = false;
  }
}

} // namespace

void meos_qt::setClassCursor(const Window &window, QWidget *widget) {
  if (!window.windowClass || !window.windowClass->cursor || !widget)
    return;
  // Windows replaces any cursor set with SetCursor, the wait cursor included.
  endWaitOverride();
  currentCursor = window.windowClass->cursor;
  const Qt::CursorShape shape = cursorShape(currentCursor);
  if (widget->cursor().shape() != shape)
    widget->setCursor(shape);
}

// The default colours of Windows 10, not those of the desktop theme: MeOS mixes
// system colours with fixed colours of its own, which a dark theme would make
// unreadable.
DWORD GetSysColor(int index) {
  static const std::array<COLORREF, 31> colors = {
      RGB(200, 200, 200), // COLOR_SCROLLBAR
      RGB(0, 0, 0),       // COLOR_BACKGROUND
      RGB(153, 180, 209), // COLOR_ACTIVECAPTION
      RGB(191, 205, 219), // COLOR_INACTIVECAPTION
      RGB(240, 240, 240), // COLOR_MENU
      RGB(255, 255, 255), // COLOR_WINDOW
      RGB(100, 100, 100), // COLOR_WINDOWFRAME
      RGB(0, 0, 0),       // COLOR_MENUTEXT
      RGB(0, 0, 0),       // COLOR_WINDOWTEXT
      RGB(0, 0, 0),       // COLOR_CAPTIONTEXT
      RGB(180, 180, 180), // COLOR_ACTIVEBORDER
      RGB(244, 247, 252), // COLOR_INACTIVEBORDER
      RGB(171, 171, 171), // COLOR_APPWORKSPACE
      RGB(0, 120, 215),   // COLOR_HIGHLIGHT
      RGB(255, 255, 255), // COLOR_HIGHLIGHTTEXT
      RGB(240, 240, 240), // COLOR_3DFACE
      RGB(160, 160, 160), // COLOR_3DSHADOW
      RGB(109, 109, 109), // COLOR_GRAYTEXT
      RGB(0, 0, 0),       // COLOR_BTNTEXT
      RGB(0, 0, 0),       // COLOR_INACTIVECAPTIONTEXT
      RGB(255, 255, 255), // COLOR_3DHIGHLIGHT
      RGB(105, 105, 105), // COLOR_3DDKSHADOW
      RGB(227, 227, 227), // COLOR_3DLIGHT
      RGB(0, 0, 0),       // COLOR_INFOTEXT
      RGB(255, 255, 225), // COLOR_INFOBK
      RGB(0, 0, 0),       // (unused)
      RGB(0, 102, 204),   // COLOR_HOTLIGHT
      RGB(185, 209, 234), // COLOR_GRADIENTACTIVECAPTION
      RGB(215, 228, 242), // COLOR_GRADIENTINACTIVECAPTION
      RGB(51, 153, 255),  // COLOR_MENUHILIGHT
      RGB(240, 240, 240), // COLOR_MENUBAR
  };
  if (index < 0 || index >= int(colors.size()))
    return 0;
  return colors[std::size_t(index)];
}

// Only the system cursors exist; MeOS loads no cursor resources.
HCURSOR LoadCursor(HINSTANCE instance, LPCWSTR cursorName) {
  if (instance || !IS_INTRESOURCE(cursorName))
    return nullptr;
  const std::uintptr_t id = reinterpret_cast<std::uintptr_t>(cursorName);
  switch (id) {
  case cursorArrow:
  case cursorIBeam:
  case cursorWait:
  case cursorCross:
  case cursorSizeAll:
  case cursorNo:
  case cursorHand:
  case cursorAppStarting:
    return reinterpret_cast<HCURSOR>(id);
  }
  return nullptr;
}

// Shows the cursor over the window under the mouse (or the capture window) until
// the next mouse movement sets the class cursor again.
HCURSOR SetCursor(HCURSOR cursor) {
  const HCURSOR previous = currentCursor;
  currentCursor = cursor;
  if (!meos_qt::isGuiThread())
    return previous;

  const Qt::CursorShape shape = cursorShape(cursor);
  if (shape == Qt::WaitCursor) {
    if (!waitOverride) {
      QApplication::setOverrideCursor(Qt::WaitCursor);
      waitOverride = true;
    }
    return previous;
  }
  endWaitOverride();

  QWidget *widget = nullptr;
  if (const std::shared_ptr<meos_qt::Window> capture = meos_qt::findWindow(GetCapture()))
    widget = capture->client;
  else
    widget = QApplication::widgetAt(QCursor::pos());
  if (widget && meos_qt::windowFromWidget(widget) && widget->cursor().shape() != shape)
    widget->setCursor(shape);
  return previous;
}
