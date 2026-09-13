/************************************************************************
    MeOS - Orienteering Software
    Linux port: internal interface of the Qt backend for the Win32 window API.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Shared by the files in code/platform/qt and by their self test. MeOS code uses
// only the Win32 declarations in code/platform/compat/win32.
//
// All window functions run in the GUI thread (the thread that created the Qt
// application). The exceptions are those Windows allows across threads:
// PostMessage and SendMessage.

#pragma once

// Qt first: the Win32 headers define many short macros.
#include <QApplication>
#include <QCursor>
#include <QPixmap>
#include <QPointer>
#include <QRegion>
#include <QScrollBar>
#include <QWidget>

#include "windows.h"

#include <functional>

class QHelpEvent;
class QKeyEvent;
class QMouseEvent;

namespace meos_qt {

class Surface;
class Window;

/* ---------------------------------------------------------------------
   Application and message loop: win32_app.cpp
   --------------------------------------------------------------------- */

// Creates the Qt application. Key events pass the WH_KEYBOARD hooks there, so
// every program that uses this layer must create its application with it.
std::unique_ptr<QApplication> createApplication(int &argc, char **argv);

// The HINSTANCE passed to WinMain.
HINSTANCE applicationInstance();

bool isGuiThread();

/* ---------------------------------------------------------------------
   Windows: win32_window.cpp
   --------------------------------------------------------------------- */

struct WindowClass {
  std::wstring name;
  ATOM atom = 0;
  UINT style = 0;
  WNDPROC proc = nullptr;
  int wndExtra = 0;
  HINSTANCE instance = nullptr;
  HCURSOR cursor = nullptr;
  HBRUSH background = nullptr;
  // Creates the widgets of a window of a system class (a control). Windows of
  // registered classes get a canvas.
  void (*createWidgets)(Window &window, QWidget *parentWidget) = nullptr;
};

// The state of a control (win32_controls.cpp, win32_commctrl.cpp).
class Control {
public:
  virtual ~Control() = default;
  // The height a control takes for a requested window height. A combo box keeps
  // the height of its selection field.
  virtual int windowHeight(int requested) const { return requested; }
};

struct ScrollBarState {
  int min = 0;
  int max = 100;
  UINT page = 0;
  int pos = 0;
  int trackPos = 0;
};

class Window {
public:
  HWND handle = nullptr;
  std::shared_ptr<const WindowClass> windowClass;
  WNDPROC proc = nullptr;
  DWORD style = 0;
  DWORD exStyle = 0;
  // Parent of a child window, owner of a top-level window.
  HWND parent = nullptr;
  HINSTANCE instance = nullptr;
  LONG_PTR id = 0;
  LONG_PTR userData = 0;
  std::vector<char> extraBytes;
  std::wstring text;
  // Child windows in creation order.
  std::vector<HWND> children;

  // frame covers the window rectangle, client the client area. Child windows are
  // placed in client.
  QPointer<QWidget> frame;
  QPointer<QWidget> client;
  // Backing store of the client area (win32_gdi.h). GetDC and BeginPaint draw
  // on it, the client widget shows it. Set once when the window is created.
  std::shared_ptr<Surface> surface;
  // Set for controls; their client widget is the Qt control.
  std::shared_ptr<Control> control;

  bool destroying = false;

  // Scroll bars of the window (SB_HORZ, SB_VERT). Visible while the style has
  // WS_HSCROLL or WS_VSCROLL.
  ScrollBarState scroll[2];
  QPointer<QScrollBar> scrollBars[2];

  // Area to be painted by the next WM_PAINT, in client coordinates, and the area
  // that the current WM_PAINT paints.
  QRegion updateRegion;
  bool eraseRequested = false;
  QRegion paintRegion;
  bool paintErase = false;

  // Last geometry reported with WM_WINDOWPOSCHANGED and WM_SIZE, and the client
  // size the update region was last adjusted to.
  QRect reportedRect;
  QSize reportedClientSize{-1, -1};
  QSize knownClientSize{-1, -1};

  bool isChild() const { return (style & WS_CHILD) != 0; }
};

// The window behind a handle, or nullptr. Handles are never reused.
std::shared_ptr<Window> findWindow(HWND window);
std::vector<std::shared_ptr<Window>> allWindows();
// As findWindow, but sets ERROR_INVALID_WINDOW_HANDLE if there is no window.
std::shared_ptr<Window> windowOrError(HWND window);
// The window a widget belongs to (the widget itself or its nearest ancestor).
HWND windowFromWidget(const QWidget *widget);
// Marks a widget as part of a window.
void attachWidget(QWidget *widget, HWND window);

// Calls the window procedure (in the GUI thread).
LRESULT callWindowProc(const std::shared_ptr<Window> &window, UINT message, WPARAM wParam, LPARAM lParam);

// The window rectangle in parent client coordinates (screen coordinates for
// top-level windows), and the client area size.
QRect windowRect(const Window &window);
QSize clientSize(const Window &window);

// Reports a changed window rectangle or client area with WM_WINDOWPOSCHANGED
// (whose default processing sends WM_SIZE).
void reportGeometry(const std::shared_ptr<Window> &window, UINT extraFlags = 0);

// Destroys all remaining windows without messages, as at process exit.
void destroyAllWindows();

// Sends WM_COMMAND with a notification code to the parent of a control.
void notifyParent(const Window &control, int code);

/* Keyboard focus. The layer keeps the focus window itself and sends
   WM_KILLFOCUS and WM_SETFOCUS as Windows does; the Qt focus widget follows it. */

// Qt moves its focus widget (user click, window activation); called from
// QApplication::notify for focus events.
void qtFocusEvent(QWidget *receiver, bool focusIn, Qt::FocusReason reason);

// Suppresses qtFocusEvent while the layer hides, disables or focuses widgets, and
// afterwards gives the Qt focus to the focus window.
class QtFocusGuard {
public:
  QtFocusGuard();
  ~QtFocusGuard();
  QtFocusGuard(const QtFocusGuard &) = delete;
  QtFocusGuard &operator=(const QtFocusGuard &) = delete;
};

// While a window has the mouse capture, mouse input over other windows goes to
// it. Returns true if the event was redirected.
bool redirectMouseToCapture(QWidget *receiver, QMouseEvent *event);

/* ---------------------------------------------------------------------
   Controls: win32_controls.cpp (BUTTON, EDIT, COMBOBOX, LISTBOX, STATIC)
   and win32_commctrl.cpp (tooltips, toolbar)
   --------------------------------------------------------------------- */

// The system window classes, registered with the first window class lookup.
void registerControlClasses(const std::function<void(const WindowClass &)> &add);
void registerCommonControlClasses(const std::function<void(const WindowClass &)> &add);

// Shows the text of a tool of a tooltip window for a QEvent::ToolTip. Returns true
// if the event concerns a window of this layer.
bool showToolTip(QWidget *receiver, QHelpEvent *event);

// The pixmap of a bitmap handle (BM_SETIMAGE, image lists), or a null pixmap.
QPixmap bitmapPixmap(HBITMAP bitmap);

/* ---------------------------------------------------------------------
   Messages, timers, hooks and keyboard state: win32_message.cpp
   --------------------------------------------------------------------- */

// Counts the window procedure, hook and timer calls on the stack of the GUI thread.
// A posted message is left for GetMessage only if GetMessage waits at the current
// depth; inside a handler (a modal loop such as a message box) it is dispatched
// directly, as the modal loop of Windows does.
class HandlerScope {
public:
  HandlerScope();
  ~HandlerScope();
  HandlerScope(const HandlerScope &) = delete;
  HandlerScope &operator=(const HandlerScope &) = delete;
};

// Posted messages. takePostedMessage removes the first message that matches.
void postToQueue(const MSG &msg);
bool takePostedMessage(MSG &msg, HWND filter, UINT filterMin, UINT filterMax);
// Called in the GUI thread for every posted message: dispatches queued messages
// unless GetMessage waits for them at the current handler depth.
void deliverPostedMessages();
// Marks the part of GetMessage that waits for Qt events.
class GetMessageWait {
public:
  GetMessageWait();
  ~GetMessageWait();
  GetMessageWait(const GetMessageWait &) = delete;
  GetMessageWait &operator=(const GetMessageWait &) = delete;
};
int handlerDepth();

void killTimersOf(HWND window);

// Calls the hooks of a type, newest first. Returns 0 if none is installed.
LRESULT callHooks(int hookType, int code, WPARAM wParam, LPARAM lParam);
bool hasHooks(int hookType);

// Active while a key event is delivered: records the key state for GetKeyState.
class KeyEventScope {
public:
  explicit KeyEventScope(const QKeyEvent &event);
  ~KeyEventScope();
  KeyEventScope(const KeyEventScope &) = delete;
  KeyEventScope &operator=(const KeyEventScope &) = delete;

private:
  bool outer;
};

// Runs the WH_KEYBOARD hooks for a key event. Returns true if a hook discards it.
bool filterKeyEvent(const QKeyEvent &event);

// The virtual key code of a key event, or 0 if Windows has none.
int virtualKey(const QKeyEvent &event);
// lParam of WM_KEYDOWN, WM_KEYUP and WM_CHAR and of the keyboard hook.
LPARAM keyMessageParam(const QKeyEvent &event);
// MK_* flags for mouse messages.
WPARAM mouseKeyFlags(Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);

/* ---------------------------------------------------------------------
   Canvas: win32_canvas.cpp. The widgets of windows of registered classes.
   --------------------------------------------------------------------- */

// Creates the frame widget of a window, which draws the border of child windows.
QWidget *createFrame(Window &window, QWidget *parentWidget);

// Creates frame, client area and scroll bars. parentWidget is the client area of
// the parent (child windows), the frame of the owner or nullptr.
void createCanvas(Window &window, QWidget *parentWidget);

// Places client area and scroll bars in the frame according to border and
// scroll bar visibility.
void layoutCanvas(Window &window);

// Applies the style bits of a top-level window to the Qt window flags.
void applyWindowFlags(Window &window);

// Sends WM_PAINT if the update region is not empty.
void dispatchPaint(const std::shared_ptr<Window> &window);

void invalidate(Window &window, const QRegion &region, bool erase);

// Width of the border that WS_BORDER and WS_EX_CLIENTEDGE add to a child window.
int borderWidth(DWORD style, DWORD exStyle);

constexpr int scrollBarExtent = 17; // SM_CXVSCROLL at 96 DPI

/* ---------------------------------------------------------------------
   Cursors: win32_screen.cpp
   --------------------------------------------------------------------- */

// Sets the cursor of a class (WM_SETCURSOR default processing) on the widget
// the mouse is over.
void setClassCursor(const Window &window, QWidget *widget);

} // namespace meos_qt
