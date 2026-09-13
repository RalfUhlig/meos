/************************************************************************
    MeOS - Orienteering Software
    Linux port: system colours, cursors, system metrics, monitors and the
    placement of windows.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_ui.h"

#include <QGuiApplication>
#include <QScreen>

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

/* ---------------------------------------------------------------------
   System metrics and monitors
   --------------------------------------------------------------------- */

namespace {

constexpr int edgeExtent = 2; // SM_CXEDGE, SM_CYEDGE

QRect virtualScreen() {
  QRect area;
  for (const QScreen *screen : QGuiApplication::screens())
    area |= screen->geometry();
  return area;
}

} // namespace

int GetSystemMetrics(int index) {
  const QScreen *primary = QGuiApplication::primaryScreen();
  const QRect screen = primary ? primary->geometry() : QRect();
  switch (index) {
  case SM_CXSCREEN: return screen.width();
  case SM_CYSCREEN: return screen.height();
  case SM_CXVIRTUALSCREEN: return virtualScreen().width();
  case SM_CYVIRTUALSCREEN: return virtualScreen().height();
  case SM_CXEDGE:
  case SM_CYEDGE: return edgeExtent;
  default: return 0;
  }
}

// One call per screen with its rectangle in virtual screen coordinates, the primary
// screen first. With a clip rectangle, only the screens it intersects.
BOOL EnumDisplayMonitors(HDC dc, LPCRECT clip, MONITORENUMPROC enumProc, LPARAM data) {
  if (dc || !enumProc) {
    // Monitors of a DC are not needed by MeOS.
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  QList<QScreen *> screens = QGuiApplication::screens();
  const QScreen *primary = QGuiApplication::primaryScreen();
  std::stable_partition(screens.begin(), screens.end(), [primary](const QScreen *s) { return s == primary; });
  for (int i = 0; i < screens.size(); i++) {
    QRect area = screens[i]->geometry();
    if (clip) {
      area &= QRect(QPoint(clip->left, clip->top), QPoint(clip->right - 1, clip->bottom - 1));
      if (area.isEmpty())
        continue;
    }
    RECT rect = {area.left(), area.top(), area.left() + area.width(), area.top() + area.height()};
    const auto monitor = reinterpret_cast<HMONITOR>(std::uintptr_t(i + 1));
    if (!enumProc(monitor, nullptr, &rect, data))
      break;
  }
  return TRUE;
}

/* ---------------------------------------------------------------------
   Window placement
   --------------------------------------------------------------------- */

namespace {

// Top-level windows without WS_EX_TOOLWINDOW report their normal position in
// workspace coordinates, which exclude panels at the top or left of the primary
// screen.
QPoint workspaceOffset(const meos_qt::Window &window) {
  if (window.isChild() || (window.exStyle & WS_EX_TOOLWINDOW))
    return QPoint();
  const QScreen *primary = QGuiApplication::primaryScreen();
  return primary ? primary->availableGeometry().topLeft() - primary->geometry().topLeft() : QPoint();
}

// The window rectangle in the normal (restored) state: parent client coordinates
// for child windows, screen coordinates for top-level windows.
QRect normalRect(const meos_qt::Window &window) {
  const QWidget *frame = window.frame;
  if (!frame)
    return QRect();
  if (window.isChild())
    return frame->geometry();
  if (!frame->isMaximized() && !frame->isMinimized() && !frame->isFullScreen())
    return frame->frameGeometry();
  // normalGeometry excludes the decoration, which the current frame shows.
  const QRect normal = frame->normalGeometry();
  const QMargins decoration(frame->geometry().left() - frame->frameGeometry().left(),
                            frame->geometry().top() - frame->frameGeometry().top(),
                            frame->frameGeometry().right() - frame->geometry().right(),
                            frame->frameGeometry().bottom() - frame->geometry().bottom());
  return normal.isValid() ? normal.marginsAdded(decoration) : frame->frameGeometry();
}

} // namespace

BOOL GetWindowPlacement(HWND window, WINDOWPLACEMENT *placement) {
  const std::shared_ptr<meos_qt::Window> target = meos_qt::windowOrError(window);
  if (!target || !placement)
    return FALSE;
  const QWidget *frame = target->frame;
  placement->flags = 0;
  placement->showCmd = SW_SHOWNORMAL;
  if (frame && !target->isChild()) {
    if (frame->isMinimized())
      placement->showCmd = SW_SHOWMINIMIZED;
    else if (frame->isMaximized())
      placement->showCmd = SW_SHOWMAXIMIZED;
  }
  placement->ptMinPosition = POINT{-1, -1};
  placement->ptMaxPosition = POINT{-1, -1};
  const QRect rect = normalRect(*target).translated(-workspaceOffset(*target));
  placement->rcNormalPosition = RECT{rect.left(), rect.top(), rect.left() + rect.width(), rect.top() + rect.height()};
  return TRUE;
}

// Sets the normal position, then shows the window as showCmd says.
BOOL SetWindowPlacement(HWND window, const WINDOWPLACEMENT *placement) {
  const std::shared_ptr<meos_qt::Window> target = meos_qt::windowOrError(window);
  if (!target || !placement)
    return FALSE;
  if (placement->length != sizeof(WINDOWPLACEMENT)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  QWidget *frame = target->frame;
  if (!frame)
    return FALSE;

  if (!target->isChild() && (frame->isMaximized() || frame->isMinimized()))
    frame->showNormal();
  const RECT &normal = placement->rcNormalPosition;
  const QPoint offset = workspaceOffset(*target);
  SetWindowPos(window, nullptr, normal.left + offset.x(), normal.top + offset.y(), normal.right - normal.left,
               normal.bottom - normal.top, SWP_NOZORDER);

  switch (placement->showCmd) {
  case SW_HIDE:
    ShowWindow(window, SW_HIDE);
    break;
  case SW_SHOWMINIMIZED:
    ShowWindow(window, SW_SHOW);
    if (!target->isChild())
      frame->showMinimized();
    break;
  case SW_SHOWMAXIMIZED:
    ShowWindow(window, SW_MAXIMIZE);
    break;
  default:
    ShowWindow(window, SW_SHOWNORMAL);
    break;
  }
  return TRUE;
}
