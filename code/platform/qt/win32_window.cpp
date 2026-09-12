/************************************************************************
    MeOS - Orienteering Software
    Linux port: Win32 windows and window classes on Qt widgets.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_ui.h"

#include <QGuiApplication>
#include <QScreen>

#include <map>
#include <unordered_map>

namespace {

using meos_qt::Window;
using meos_qt::WindowClass;

const char *const handleProperty = "meosHwnd";

constexpr DWORD errorTopLevelWithChildStyle = 1406; // ERROR_TLW_WITH_WSCHILD
constexpr DWORD errorControlIdNotFound = 1421;      // ERROR_CONTROL_ID_NOT_FOUND

// Handles are numbers, never addresses: MeOS keeps maps keyed by HWND, and a
// reused handle would find stale entries there.
std::mutex tableMutex;
std::unordered_map<HWND, std::shared_ptr<Window>> windows;
std::uintptr_t lastHandle = 0x10000;
const HWND desktopHandle = reinterpret_cast<HWND>(std::uintptr_t(0x10000));

// Window classes by lower-case name (class names are case insensitive).
std::map<std::wstring, std::shared_ptr<WindowClass>> classes;
ATOM lastAtom = 0xC000;

HWND captureWindow = nullptr;

std::wstring lowerCase(const std::wstring &text) {
  std::wstring lower(text);
  for (wchar_t &ch : lower)
    ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));
  return lower;
}

std::shared_ptr<const WindowClass> findClass(LPCWSTR name) {
  std::lock_guard<std::mutex> lock(tableMutex);
  if (IS_INTRESOURCE(name)) {
    const ATOM atom = static_cast<ATOM>(reinterpret_cast<std::uintptr_t>(name));
    for (const auto &entry : classes) {
      if (entry.second->atom == atom)
        return entry.second;
    }
    return nullptr;
  }
  const auto entry = classes.find(lowerCase(name));
  return entry == classes.end() ? nullptr : entry->second;
}

HWND registerWindow(const std::shared_ptr<Window> &window) {
  std::lock_guard<std::mutex> lock(tableMutex);
  lastHandle += 4;
  window->handle = reinterpret_cast<HWND>(lastHandle);
  windows[window->handle] = window;
  return window->handle;
}

void unregisterWindow(HWND handle) {
  std::lock_guard<std::mutex> lock(tableMutex);
  windows.erase(handle);
}

QRect screenRect() {
  const QScreen *screen = QGuiApplication::primaryScreen();
  return screen ? screen->geometry() : QRect();
}

RECT toRect(const QRect &rect) {
  return RECT{rect.left(), rect.top(), rect.left() + rect.width(), rect.top() + rect.height()};
}

bool isGuiThreadOrError() {
  if (meos_qt::isGuiThread())
    return true;
  SetLastError(ERROR_ACCESS_DENIED);
  return false;
}

// The update region follows the client area as on Windows: everything is invalid
// after a size change with CS_HREDRAW/CS_VREDRAW, otherwise the uncovered strips.
void adjustUpdateRegion(Window &window, QSize newSize) {
  const QSize oldSize = window.knownClientSize;
  window.knownClientSize = newSize;
  if (!window.client || oldSize == newSize)
    return;

  const UINT classStyle = window.windowClass ? window.windowClass->style : 0;
  const bool redrawAll = !oldSize.isValid() ||
                         (oldSize.width() != newSize.width() && (classStyle & CS_HREDRAW)) ||
                         (oldSize.height() != newSize.height() && (classStyle & CS_VREDRAW));
  if (redrawAll) {
    meos_qt::invalidate(window, QRect(QPoint(0, 0), newSize), true);
    return;
  }
  QRegion uncovered;
  if (newSize.width() > oldSize.width())
    uncovered += QRect(oldSize.width(), 0, newSize.width() - oldSize.width(), newSize.height());
  if (newSize.height() > oldSize.height())
    uncovered += QRect(0, oldSize.height(), newSize.width(), newSize.height() - oldSize.height());
  meos_qt::invalidate(window, uncovered, true);
}

void sendSizeIfChanged(const std::shared_ptr<Window> &window) {
  const QSize size = meos_qt::clientSize(*window);
  if (size == window->reportedClientSize)
    return;
  window->reportedClientSize = size;
  const bool maximized = !window->isChild() && window->frame && window->frame->isMaximized();
  meos_qt::callWindowProc(window, WM_SIZE, maximized ? SIZE_MAXIMIZED : SIZE_RESTORED,
                          MAKELPARAM(size.width(), size.height()));
}

// Sets the size of the window rectangle. For top-level windows Qt sizes the client
// area and adds the decoration outside; its size is only known once the window
// manager has decorated the window.
void resizeWindow(Window &window, int width, int height) {
  QWidget *frame = window.frame;
  if (!frame)
    return;
  width = std::max(width, 0);
  height = std::max(height, 0);
  if (!window.isChild() && frame->isWindow()) {
    const QSize decoration = frame->frameGeometry().size() - frame->size();
    width = std::max(width - decoration.width(), 0);
    height = std::max(height - decoration.height(), 0);
  }
  frame->resize(width, height);
}

std::shared_ptr<Window> topLevelOf(std::shared_ptr<Window> window) {
  while (window && window->isChild()) {
    std::shared_ptr<Window> parent = meos_qt::findWindow(window->parent);
    if (!parent)
      break;
    window = parent;
  }
  return window;
}

void releaseWindow(const std::shared_ptr<Window> &window, bool root) {
  meos_qt::killTimersOf(window->handle);
  if (captureWindow == window->handle)
    captureWindow = nullptr;
  unregisterWindow(window->handle);

  if (std::shared_ptr<Window> parent = meos_qt::findWindow(window->parent)) {
    auto &siblings = parent->children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), window->handle), siblings.end());
  }

  // Events that are still underway for the widgets find no window any more. The
  // widgets of child windows go with the root widget; Qt must not delete a widget
  // inside its own event handler, so the root is deleted later.
  if (window->frame) {
    window->frame->setProperty(handleProperty, QVariant::fromValue<quintptr>(0));
    if (root)
      window->frame->deleteLater();
  }
}

// As on Windows: owned windows first, then WM_DESTROY to the window before its
// children (which still exist while the parent handles it), and the handles are
// released children first.
void destroyTree(const std::shared_ptr<Window> &window, bool root) {
  window->destroying = true;

  for (const std::shared_ptr<Window> &owned : meos_qt::allWindows()) {
    if (!owned->isChild() && owned->parent == window->handle && !owned->destroying)
      destroyTree(owned, true);
  }

  if (root && window->frame)
    window->frame->hide();

  meos_qt::callWindowProc(window, WM_DESTROY, 0, 0);

  for (;;) {
    std::shared_ptr<Window> child;
    for (HWND handle : window->children) {
      std::shared_ptr<Window> candidate = meos_qt::findWindow(handle);
      if (candidate && !candidate->destroying) {
        child = candidate;
        break;
      }
    }
    if (!child)
      break;
    destroyTree(child, false);
  }

  releaseWindow(window, root);
}

bool setStyle(const std::shared_ptr<Window> &window, DWORD style) {
  // Visibility and enabled state stay as they are; ShowWindow and EnableWindow
  // change them.
  const DWORD state = WS_VISIBLE | WS_DISABLED;
  const DWORD oldStyle = window->style;
  window->style = (style & ~state) | (oldStyle & state);
  const DWORD changed = oldStyle ^ window->style;

  const DWORD frameStyles = WS_CAPTION | WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
  if (!window->isChild() && (changed & frameStyles))
    meos_qt::applyWindowFlags(*window);
  if (changed & (WS_BORDER | WS_HSCROLL | WS_VSCROLL)) {
    meos_qt::layoutCanvas(*window);
    meos_qt::reportGeometry(window);
  }
  return true;
}

} // namespace

/* ---------------------------------------------------------------------
   Internal interface
   --------------------------------------------------------------------- */

std::shared_ptr<Window> meos_qt::findWindow(HWND window) {
  if (!window)
    return nullptr;
  std::lock_guard<std::mutex> lock(tableMutex);
  const auto entry = windows.find(window);
  return entry == windows.end() ? nullptr : entry->second;
}

std::vector<std::shared_ptr<Window>> meos_qt::allWindows() {
  std::lock_guard<std::mutex> lock(tableMutex);
  std::vector<std::shared_ptr<Window>> result;
  result.reserve(windows.size());
  for (const auto &entry : windows)
    result.push_back(entry.second);
  return result;
}

std::shared_ptr<Window> meos_qt::windowOrError(HWND window) {
  std::shared_ptr<Window> result = findWindow(window);
  if (!result)
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
  return result;
}

HWND meos_qt::windowFromWidget(const QWidget *widget) {
  for (; widget; widget = widget->parentWidget()) {
    const QVariant handle = widget->property(handleProperty);
    if (handle.isValid())
      return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(handle.value<quintptr>()));
  }
  return nullptr;
}

void meos_qt::attachWidget(QWidget *widget, HWND window) {
  widget->setProperty(handleProperty, QVariant::fromValue<quintptr>(reinterpret_cast<std::uintptr_t>(window)));
}

LRESULT meos_qt::callWindowProc(const std::shared_ptr<Window> &window, UINT message, WPARAM wParam, LPARAM lParam) {
  HandlerScope scope;
  const WNDPROC proc = window->proc;
  return proc ? proc(window->handle, message, wParam, lParam)
              : DefWindowProc(window->handle, message, wParam, lParam);
}

QRect meos_qt::windowRect(const Window &window) {
  const QWidget *frame = window.frame;
  if (!frame)
    return QRect();
  return window.isChild() ? frame->geometry() : frame->frameGeometry();
}

QSize meos_qt::clientSize(const Window &window) {
  return window.client ? window.client->size() : QSize(0, 0);
}

void meos_qt::reportGeometry(const std::shared_ptr<Window> &window, UINT extraFlags) {
  if (window->destroying)
    return;
  const QRect rect = windowRect(*window);
  const QSize size = clientSize(*window);
  adjustUpdateRegion(*window, size);

  const bool moved = rect.topLeft() != window->reportedRect.topLeft();
  const bool sized = rect.size() != window->reportedRect.size();
  if (!moved && !sized && size == window->reportedClientSize)
    return;
  window->reportedRect = rect;

  WINDOWPOS pos = {window->handle, nullptr, rect.x(), rect.y(), rect.width(), rect.height(),
                   extraFlags | SWP_NOZORDER | (moved ? 0 : SWP_NOMOVE) | (sized ? 0 : SWP_NOSIZE)};
  callWindowProc(window, WM_WINDOWPOSCHANGED, 0, reinterpret_cast<LPARAM>(&pos));
}

void meos_qt::destroyAllWindows() {
  const std::vector<std::shared_ptr<Window>> remaining = allWindows();
  for (const std::shared_ptr<Window> &window : remaining) {
    killTimersOf(window->handle);
    if (window->frame && !window->frame->parentWidget())
      delete window->frame.data();
  }
  // Owned windows and children were deleted with their parent widgets.
  for (const std::shared_ptr<Window> &window : remaining) {
    if (window->frame)
      delete window->frame.data();
  }
  std::lock_guard<std::mutex> lock(tableMutex);
  windows.clear();
  captureWindow = nullptr;
}

/* ---------------------------------------------------------------------
   Window classes and window lifetime
   --------------------------------------------------------------------- */

ATOM RegisterClassEx(const WNDCLASSEX *windowClass) {
  if (!windowClass || windowClass->cbSize != sizeof(WNDCLASSEX) || !windowClass->lpszClassName ||
      IS_INTRESOURCE(windowClass->lpszClassName) || windowClass->cbWndExtra < 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }

  auto entry = std::make_shared<WindowClass>();
  entry->name = windowClass->lpszClassName;
  entry->style = windowClass->style;
  entry->proc = windowClass->lpfnWndProc;
  entry->wndExtra = windowClass->cbWndExtra;
  entry->instance = windowClass->hInstance;
  entry->cursor = windowClass->hCursor;
  entry->background = windowClass->hbrBackground;

  std::lock_guard<std::mutex> lock(tableMutex);
  const std::wstring key = lowerCase(entry->name);
  if (classes.count(key)) {
    SetLastError(ERROR_CLASS_ALREADY_EXISTS);
    return 0;
  }
  entry->atom = ++lastAtom;
  classes[key] = entry;
  return entry->atom;
}

HWND CreateWindowEx(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, int x, int y,
                    int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
  if (!isGuiThreadOrError())
    return nullptr;
  const std::shared_ptr<const WindowClass> windowClass = findClass(className);
  if (!windowClass) {
    SetLastError(ERROR_CANNOT_FIND_WND_CLASS);
    return nullptr;
  }

  if (parent == desktopHandle)
    parent = nullptr;
  std::shared_ptr<Window> parentWindow;
  if (parent) {
    parentWindow = meos_qt::windowOrError(parent);
    if (!parentWindow)
      return nullptr;
  }
  else if (style & WS_CHILD) {
    SetLastError(errorTopLevelWithChildStyle);
    return nullptr;
  }

  auto window = std::make_shared<Window>();
  window->windowClass = windowClass;
  window->proc = windowClass->proc;
  window->style = style & ~WS_VISIBLE;
  window->exStyle = exStyle;
  window->parent = parent;
  window->instance = instance;
  window->id = (style & WS_CHILD) ? reinterpret_cast<LONG_PTR>(menu) : 0;
  window->extraBytes.assign(static_cast<std::size_t>(windowClass->wndExtra), 0);
  window->text = windowName ? windowName : L"";

  const HWND handle = registerWindow(window);
  if (parentWindow)
    parentWindow->children.push_back(handle);

  QWidget *parentWidget = nullptr;
  if (parentWindow)
    parentWidget = window->isChild() ? parentWindow->client.data() : parentWindow->frame.data();
  meos_qt::createCanvas(*window, parentWidget);
  if (!window->isChild())
    window->frame->setWindowTitle(QString::fromWCharArray(window->text.c_str(), int(window->text.size())));

  if (window->isChild()) {
    if (x == CW_USEDEFAULT)
      x = y = 0;
    if (width == CW_USEDEFAULT)
      width = height = 0;
    window->frame->move(x, y);
  }
  else {
    if (width == CW_USEDEFAULT) {
      const QRect screen = screenRect();
      width = std::max(screen.width() * 3 / 4, 640);
      height = std::max(screen.height() * 3 / 4, 480);
    }
    if (x != CW_USEDEFAULT)
      window->frame->move(x, y);
  }
  resizeWindow(*window, width, height);
  meos_qt::layoutCanvas(*window);
  if (style & WS_DISABLED)
    window->frame->setEnabled(false);

  window->reportedRect = meos_qt::windowRect(*window);
  adjustUpdateRegion(*window, meos_qt::clientSize(*window));

  CREATESTRUCT create = {param, instance, menu, parent, height, width, y, x, static_cast<LONG>(style),
                         windowName, className, exStyle};
  if (meos_qt::callWindowProc(window, WM_CREATE, 0, reinterpret_cast<LPARAM>(&create)) == -1) {
    if (meos_qt::findWindow(handle))
      DestroyWindow(handle);
    return nullptr;
  }
  if (!meos_qt::findWindow(handle))
    return nullptr;

  // Windows sends WM_SIZE (and WM_MOVE) directly after WM_CREATE.
  sendSizeIfChanged(window);

  if (style & WS_VISIBLE)
    ShowWindow(handle, SW_SHOW);
  return meos_qt::findWindow(handle) ? handle : nullptr;
}

BOOL DestroyWindow(HWND window) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !isGuiThreadOrError())
    return FALSE;
  if (target->destroying)
    return FALSE;
  destroyTree(target, true);
  return TRUE;
}

/* ---------------------------------------------------------------------
   Window procedures and window data
   --------------------------------------------------------------------- */

LRESULT DefWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  const std::shared_ptr<Window> target = meos_qt::findWindow(window);
  if (!target)
    return 0;

  switch (message) {
  case WM_CLOSE:
    DestroyWindow(window);
    return 0;

  case WM_SETTEXT:
    target->text = lParam ? reinterpret_cast<LPCWSTR>(lParam) : L"";
    if (!target->isChild() && target->frame)
      target->frame->setWindowTitle(QString::fromWCharArray(target->text.c_str(), int(target->text.size())));
    return TRUE;

  case WM_GETTEXT: {
    const auto buffer = reinterpret_cast<LPWSTR>(lParam);
    if (!buffer || wParam == 0)
      return 0;
    const std::size_t count = std::min<std::size_t>(target->text.size(), wParam - 1);
    std::wmemcpy(buffer, target->text.c_str(), count);
    buffer[count] = 0;
    return static_cast<LRESULT>(count);
  }

  case WM_GETTEXTLENGTH:
    return static_cast<LRESULT>(target->text.size());

  case WM_WINDOWPOSCHANGED:
    sendSizeIfChanged(target);
    return 0;
  }
  return 0;
}

LRESULT CallWindowProc(WNDPROC windowProc, HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (!windowProc)
    return 0;
  meos_qt::HandlerScope scope;
  return windowProc(window, message, wParam, lParam);
}

LONG_PTR GetWindowLongPtr(HWND window, int index) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target)
    return 0;
  switch (index) {
  case GWLP_WNDPROC:
    return reinterpret_cast<LONG_PTR>(target->proc);
  case GWLP_HINSTANCE:
    return reinterpret_cast<LONG_PTR>(target->instance);
  case GWLP_ID:
    return target->id;
  case GWL_STYLE:
    return static_cast<LONG>(target->style);
  case GWL_EXSTYLE:
    return static_cast<LONG>(target->exStyle);
  case GWLP_USERDATA:
    return target->userData;
  }
  if (index >= 0 && std::size_t(index) + sizeof(LONG_PTR) <= target->extraBytes.size()) {
    LONG_PTR value;
    std::memcpy(&value, target->extraBytes.data() + index, sizeof(value));
    return value;
  }
  SetLastError(ERROR_INVALID_INDEX);
  return 0;
}

LONG_PTR SetWindowLongPtr(HWND window, int index, LONG_PTR value) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target)
    return 0;
  // A previous value of 0 is told apart from failure by the last error.
  SetLastError(ERROR_SUCCESS);
  const LONG_PTR previous = GetWindowLongPtr(window, index);
  switch (index) {
  case GWLP_WNDPROC:
    target->proc = reinterpret_cast<WNDPROC>(value);
    return previous;
  case GWLP_HINSTANCE:
    target->instance = reinterpret_cast<HINSTANCE>(value);
    return previous;
  case GWLP_ID:
    target->id = value;
    return previous;
  case GWL_STYLE:
    setStyle(target, static_cast<DWORD>(value));
    return previous;
  case GWL_EXSTYLE:
    target->exStyle = static_cast<DWORD>(value);
    meos_qt::applyWindowFlags(*target);
    meos_qt::layoutCanvas(*target);
    meos_qt::reportGeometry(target);
    return previous;
  case GWLP_USERDATA:
    target->userData = value;
    return previous;
  }
  if (index >= 0 && std::size_t(index) + sizeof(LONG_PTR) <= target->extraBytes.size()) {
    std::memcpy(target->extraBytes.data() + index, &value, sizeof(value));
    return previous;
  }
  SetLastError(ERROR_INVALID_INDEX);
  return 0;
}

LONG GetWindowLong(HWND window, int index) {
  if (index >= 0) {
    const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
    if (!target)
      return 0;
    if (std::size_t(index) + sizeof(LONG) > target->extraBytes.size()) {
      SetLastError(ERROR_INVALID_INDEX);
      return 0;
    }
    LONG value;
    std::memcpy(&value, target->extraBytes.data() + index, sizeof(value));
    return value;
  }
  return static_cast<LONG>(GetWindowLongPtr(window, index));
}

LONG SetWindowLong(HWND window, int index, LONG value) {
  if (index >= 0) {
    const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
    if (!target)
      return 0;
    if (std::size_t(index) + sizeof(LONG) > target->extraBytes.size()) {
      SetLastError(ERROR_INVALID_INDEX);
      return 0;
    }
    LONG previous;
    std::memcpy(&previous, target->extraBytes.data() + index, sizeof(previous));
    std::memcpy(target->extraBytes.data() + index, &value, sizeof(value));
    return previous;
  }
  // Styles are 32-bit values; extend them without sign.
  const LONG_PTR wide = (index == GWL_STYLE || index == GWL_EXSTYLE) ? LONG_PTR(DWORD(value)) : LONG_PTR(value);
  return static_cast<LONG>(SetWindowLongPtr(window, index, wide));
}

BOOL SetWindowText(HWND window, LPCWSTR text) {
  if (!meos_qt::windowOrError(window))
    return FALSE;
  return SendMessage(window, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text)) ? TRUE : FALSE;
}

int GetWindowText(HWND window, LPWSTR buffer, int maxCount) {
  if (!buffer || maxCount <= 0 || !meos_qt::windowOrError(window))
    return 0;
  buffer[0] = 0;
  return static_cast<int>(SendMessage(window, WM_GETTEXT, WPARAM(maxCount), reinterpret_cast<LPARAM>(buffer)));
}

int GetWindowTextLength(HWND window) {
  if (!meos_qt::windowOrError(window))
    return 0;
  return static_cast<int>(SendMessage(window, WM_GETTEXTLENGTH, 0, 0));
}

HWND GetDlgItem(HWND dialog, int id) {
  const std::shared_ptr<Window> parent = meos_qt::windowOrError(dialog);
  if (!parent)
    return nullptr;
  for (HWND handle : parent->children) {
    const std::shared_ptr<Window> child = meos_qt::findWindow(handle);
    if (child && child->id == id)
      return handle;
  }
  SetLastError(errorControlIdNotFound);
  return nullptr;
}

/* ---------------------------------------------------------------------
   Visibility, enabled state, position and size
   --------------------------------------------------------------------- */

BOOL ShowWindow(HWND window, int command) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !target->frame || !isGuiThreadOrError())
    return FALSE;
  QWidget *frame = target->frame;
  const bool wasVisible = (target->style & WS_VISIBLE) != 0;
  const bool topLevel = !target->isChild();

  switch (command) {
  case SW_HIDE:
    target->style &= ~WS_VISIBLE;
    frame->hide();
    break;
  case SW_MAXIMIZE:
    target->style |= WS_VISIBLE;
    if (topLevel)
      frame->showMaximized();
    else
      frame->show();
    break;
  case SW_SHOWNORMAL:
  case SW_SHOWDEFAULT:
    target->style |= WS_VISIBLE;
    if (topLevel && (frame->isMaximized() || frame->isMinimized()))
      frame->showNormal();
    else
      frame->show();
    break;
  default:
    target->style |= WS_VISIBLE;
    frame->show();
    break;
  }

  if (!wasVisible && (target->style & WS_VISIBLE) && target->client)
    meos_qt::invalidate(*target, target->client->rect(), true);
  meos_qt::layoutCanvas(*target);
  meos_qt::reportGeometry(target);
  return wasVisible ? TRUE : FALSE;
}

BOOL IsWindowVisible(HWND window) {
  std::shared_ptr<Window> target = meos_qt::findWindow(window);
  while (target) {
    if (!(target->style & WS_VISIBLE))
      return FALSE;
    if (!target->isChild())
      return TRUE;
    target = meos_qt::findWindow(target->parent);
  }
  return FALSE;
}

BOOL EnableWindow(HWND window, BOOL enable) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target)
    return FALSE;
  const bool wasDisabled = (target->style & WS_DISABLED) != 0;
  if (enable)
    target->style &= ~WS_DISABLED;
  else
    target->style |= WS_DISABLED;
  if (target->frame)
    target->frame->setEnabled(enable != FALSE);
  return wasDisabled ? TRUE : FALSE;
}

BOOL IsWindowEnabled(HWND window) {
  const std::shared_ptr<Window> target = meos_qt::findWindow(window);
  return target && !(target->style & WS_DISABLED) ? TRUE : FALSE;
}

BOOL SetWindowPos(HWND window, HWND insertAfter, int x, int y, int width, int height, UINT flags) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !target->frame || !isGuiThreadOrError())
    return FALSE;
  QWidget *frame = target->frame;

  if (!(flags & SWP_NOZORDER)) {
    // HWND_TOPMOST means HWND_TOP for child windows.
    if (insertAfter == HWND_TOPMOST && !target->isChild() && !(target->exStyle & WS_EX_TOPMOST)) {
      target->exStyle |= WS_EX_TOPMOST;
      meos_qt::applyWindowFlags(*target);
    }
    if (insertAfter == HWND_TOP || insertAfter == HWND_TOPMOST)
      frame->raise();
  }
  if (!(flags & SWP_NOMOVE))
    frame->move(x, y);
  if (!(flags & SWP_NOSIZE))
    resizeWindow(*target, width, height);

  meos_qt::layoutCanvas(*target);
  meos_qt::reportGeometry(target);
  return TRUE;
}

BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint) {
  if (!SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER))
    return FALSE;
  const std::shared_ptr<Window> target = meos_qt::findWindow(window);
  if (repaint && target && target->client)
    meos_qt::invalidate(*target, target->client->rect(), true);
  return TRUE;
}

BOOL GetClientRect(HWND window, LPRECT rect) {
  if (!rect)
    return FALSE;
  if (window == desktopHandle) {
    *rect = toRect(QRect(QPoint(0, 0), screenRect().size()));
    return TRUE;
  }
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target)
    return FALSE;
  *rect = toRect(QRect(QPoint(0, 0), meos_qt::clientSize(*target)));
  return TRUE;
}

BOOL GetWindowRect(HWND window, LPRECT rect) {
  if (!rect)
    return FALSE;
  if (window == desktopHandle) {
    *rect = toRect(screenRect());
    return TRUE;
  }
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !target->frame)
    return FALSE;
  const QWidget *frame = target->frame;
  if (target->isChild())
    *rect = toRect(QRect(frame->mapToGlobal(QPoint(0, 0)), frame->size()));
  else
    *rect = toRect(frame->frameGeometry());
  return TRUE;
}

BOOL ClientToScreen(HWND window, LPPOINT point) {
  if (!point)
    return FALSE;
  if (window == desktopHandle)
    return TRUE;
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !target->client)
    return FALSE;
  const QPoint global = target->client->mapToGlobal(QPoint(point->x, point->y));
  point->x = global.x();
  point->y = global.y();
  return TRUE;
}

BOOL ScreenToClient(HWND window, LPPOINT point) {
  if (!point)
    return FALSE;
  if (window == desktopHandle)
    return TRUE;
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !target->client)
    return FALSE;
  const QPoint local = target->client->mapFromGlobal(QPoint(point->x, point->y));
  point->x = local.x();
  point->y = local.y();
  return TRUE;
}

HWND WindowFromPoint(POINT point) {
  HWND window = meos_qt::windowFromWidget(QApplication::widgetAt(point.x, point.y));
  // Disabled child windows are skipped in favour of their parent.
  std::shared_ptr<Window> target = meos_qt::findWindow(window);
  while (target && target->isChild() && (target->style & WS_DISABLED))
    target = meos_qt::findWindow(target->parent);
  return target ? target->handle : nullptr;
}

HWND GetDesktopWindow() {
  return desktopHandle;
}

/* ---------------------------------------------------------------------
   Activation, focus and mouse capture
   --------------------------------------------------------------------- */

BOOL SetForegroundWindow(HWND window) {
  const std::shared_ptr<Window> target = topLevelOf(meos_qt::windowOrError(window));
  if (!target || !target->frame)
    return FALSE;
  target->frame->raise();
  target->frame->activateWindow();
  return TRUE;
}

HWND SetActiveWindow(HWND window) {
  const HWND previous = meos_qt::windowFromWidget(QApplication::activeWindow());
  const std::shared_ptr<Window> target = topLevelOf(meos_qt::windowOrError(window));
  if (!target || !target->frame)
    return nullptr;
  target->frame->activateWindow();
  return previous;
}

HWND SetFocus(HWND window) {
  const HWND previous = GetFocus();
  if (!window) {
    if (QWidget *focus = QApplication::focusWidget())
      focus->clearFocus();
    return previous;
  }
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !target->client || (target->style & WS_DISABLED))
    return nullptr;
  target->client->setFocus(Qt::OtherFocusReason);
  return previous;
}

HWND GetFocus() {
  return meos_qt::windowFromWidget(QApplication::focusWidget());
}

// Qt only reports mouse movement outside a widget while a button is held. The
// canvas turns leaving the capture window into a WM_MOUSEMOVE outside the client
// area, which covers how MeOS uses capture (hover highlighting).
HWND SetCapture(HWND window) {
  const HWND previous = GetCapture();
  if (meos_qt::windowOrError(window))
    captureWindow = window;
  return previous;
}

BOOL ReleaseCapture() {
  captureWindow = nullptr;
  return TRUE;
}

HWND GetCapture() {
  if (captureWindow && !meos_qt::findWindow(captureWindow))
    captureWindow = nullptr;
  return captureWindow;
}
