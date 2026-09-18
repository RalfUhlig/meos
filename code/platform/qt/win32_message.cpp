/************************************************************************
    MeOS - Orienteering Software
    Linux port: Win32 messages, timers, hooks and keyboard state on Qt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_ui.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QTimerEvent>

#include <array>
#include <deque>

namespace {

/* Posted messages, filled from any thread. */
std::mutex queueMutex;
std::deque<MSG> postedMessages;

/* The remaining state belongs to the GUI thread. */
int currentHandlerDepth = 0;
// Handler depths at which GetMessage calls wait, innermost last.
std::vector<int> waitingDepths;

struct Hook {
  HHOOK handle;
  int type;
  HOOKPROC proc;
};
std::vector<Hook> hooks;
std::uintptr_t lastHook = 0;

// One call of a hook chain. CallNextHookEx continues the innermost active call:
// MeOS passes no hook handle there, as Windows ignores it.
struct HookCall {
  std::vector<HHOOK> chain;
  std::size_t next = 0;
};
std::vector<HookCall *> activeHookCalls;

struct Timer {
  HWND window;
  UINT_PTR id;
  TIMERPROC proc;
  int qtTimer;
};
std::vector<Timer> timers;

class TimerHost : public QObject {
public:
  using QObject::QObject;

protected:
  void timerEvent(QTimerEvent *event) override;
};
QPointer<TimerHost> timerHost;

// Key state for GetKeyState: keys seen going down and up, and the modifiers of
// the key event that is being delivered (synthetic events do not update Qt's
// global modifier state).
std::array<bool, 256> keysDown{};
Qt::KeyboardModifiers keyEventModifiers;
bool inKeyEvent = false;

// USER_TIMER_MINIMUM and USER_TIMER_MAXIMUM
constexpr UINT timerMinimum = 10;
constexpr UINT timerMaximum = 0x7FFFFFFF;

constexpr DWORD errorInvalidHookFilter = 1426; // ERROR_INVALID_HOOK_FILTER

// VK_OEM_* codes of the US layout that Windows reports for these characters.
constexpr int vkOemPlus = 0xBB;
constexpr int vkOemComma = 0xBC;
constexpr int vkOemMinus = 0xBD;
constexpr int vkOemPeriod = 0xBE;

LRESULT callNextHook(HookCall &call, int code, WPARAM wParam, LPARAM lParam) {
  while (call.next < call.chain.size()) {
    const HHOOK handle = call.chain[call.next++];
    const auto hook = std::find_if(hooks.begin(), hooks.end(), [handle](const Hook &h) { return h.handle == handle; });
    if (hook == hooks.end())
      continue; // Removed while the chain runs.
    const HOOKPROC proc = hook->proc;

    struct ActiveCall {
      explicit ActiveCall(HookCall &call) { activeHookCalls.push_back(&call); }
      ~ActiveCall() { activeHookCalls.pop_back(); }
    } active(call);
    meos_qt::HandlerScope scope;
    return proc(code, wParam, lParam);
  }
  return 0;
}

std::vector<Timer>::iterator findTimer(HWND window, UINT_PTR id) {
  return std::find_if(timers.begin(), timers.end(),
                      [window, id](const Timer &t) { return t.window == window && t.id == id; });
}

void TimerHost::timerEvent(QTimerEvent *event) {
  const auto timer = std::find_if(timers.begin(), timers.end(),
                                  [event](const Timer &t) { return t.qtTimer == event->timerId(); });
  if (timer == timers.end()) {
    killTimer(event->timerId());
    return;
  }
  const Timer fired = *timer;

  std::shared_ptr<meos_qt::Window> window;
  if (fired.window) {
    window = meos_qt::findWindow(fired.window);
    if (!window) {
      KillTimer(fired.window, fired.id);
      return;
    }
  }

  // Windows delivers WM_TIMER through the message loop; here the procedure is
  // called directly, which also works inside modal loops.
  if (fired.proc) {
    meos_qt::HandlerScope scope;
    fired.proc(fired.window, WM_TIMER, fired.id, GetTickCount());
  }
  else if (window) {
    meos_qt::callWindowProc(window, WM_TIMER, fired.id, 0);
  }
}

} // namespace

/* ---------------------------------------------------------------------
   Internal interface
   --------------------------------------------------------------------- */

meos_qt::HandlerScope::HandlerScope() {
  currentHandlerDepth++;
}

meos_qt::HandlerScope::~HandlerScope() {
  currentHandlerDepth--;
}

meos_qt::GetMessageWait::GetMessageWait() {
  waitingDepths.push_back(currentHandlerDepth);
}

meos_qt::GetMessageWait::~GetMessageWait() {
  waitingDepths.pop_back();
}

int meos_qt::handlerDepth() {
  return currentHandlerDepth;
}

void meos_qt::postToQueue(const MSG &msg) {
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    postedMessages.push_back(msg);
  }
  if (QCoreApplication *app = QCoreApplication::instance())
    QMetaObject::invokeMethod(app, [] { deliverPostedMessages(); }, Qt::QueuedConnection);
}

bool meos_qt::takePostedMessage(MSG &msg, HWND filter, UINT filterMin, UINT filterMax) {
  std::lock_guard<std::mutex> lock(queueMutex);
  for (auto it = postedMessages.begin(); it != postedMessages.end(); ++it) {
    if (filter && it->hwnd != filter)
      continue;
    if ((filterMin || filterMax) && (it->message < filterMin || it->message > filterMax))
      continue;
    msg = *it;
    postedMessages.erase(it);
    return true;
  }
  return false;
}

void meos_qt::deliverPostedMessages() {
  MSG msg;
  while (waitingDepths.empty() || waitingDepths.back() != currentHandlerDepth) {
    if (!takePostedMessage(msg, nullptr, 0, 0))
      return;
    DispatchMessage(&msg);
  }
  // GetMessage waits at this depth and takes the messages itself.
}

void meos_qt::killTimersOf(HWND window) {
  for (auto it = timers.begin(); it != timers.end();) {
    if (it->window == window) {
      if (timerHost)
        timerHost->killTimer(it->qtTimer);
      it = timers.erase(it);
    }
    else {
      ++it;
    }
  }
}

LRESULT meos_qt::callHooks(int hookType, int code, WPARAM wParam, LPARAM lParam) {
  HookCall call;
  for (auto it = hooks.rbegin(); it != hooks.rend(); ++it) {
    if (it->type == hookType)
      call.chain.push_back(it->handle);
  }
  return callNextHook(call, code, wParam, lParam);
}

bool meos_qt::hasHooks(int hookType) {
  return std::any_of(hooks.begin(), hooks.end(), [hookType](const Hook &h) { return h.type == hookType; });
}

meos_qt::KeyEventScope::KeyEventScope(const QKeyEvent &event) : outer(!inKeyEvent) {
  const int key = virtualKey(event);
  if (key > 0 && key < int(keysDown.size()))
    keysDown[std::size_t(key)] = event.type() == QEvent::KeyPress;
  keyEventModifiers = event.modifiers();
  inKeyEvent = true;
}

meos_qt::KeyEventScope::~KeyEventScope() {
  if (outer)
    inKeyEvent = false;
}

bool meos_qt::filterKeyEvent(const QKeyEvent &event) {
  // X11 reports auto repeat as release and press; Windows only repeats WM_KEYDOWN.
  if (event.type() == QEvent::KeyRelease && event.isAutoRepeat())
    return false;
  const int key = virtualKey(event);
  if (!key || !hasHooks(WH_KEYBOARD))
    return false;
  return callHooks(WH_KEYBOARD, HC_ACTION, WPARAM(key), keyMessageParam(event)) != 0;
}

int meos_qt::virtualKey(const QKeyEvent &event) {
  const int key = event.key();
  const bool keypad = (event.modifiers() & Qt::KeypadModifier) != 0;

  if (key >= Qt::Key_A && key <= Qt::Key_Z)
    return 'A' + (key - Qt::Key_A);
  if (key >= Qt::Key_0 && key <= Qt::Key_9)
    return keypad ? VK_NUMPAD0 + (key - Qt::Key_0) : '0' + (key - Qt::Key_0);
  if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
    return VK_F1 + (key - Qt::Key_F1);

  switch (key) {
  case Qt::Key_Backspace: return VK_BACK;
  case Qt::Key_Tab:
  case Qt::Key_Backtab: return VK_TAB;
  case Qt::Key_Return:
  case Qt::Key_Enter: return VK_RETURN;
  case Qt::Key_Shift: return VK_SHIFT;
  case Qt::Key_Control: return VK_CONTROL;
  case Qt::Key_Alt: return VK_MENU;
  case Qt::Key_Escape: return VK_ESCAPE;
  case Qt::Key_Space: return VK_SPACE;
  case Qt::Key_PageUp: return VK_PRIOR;
  case Qt::Key_PageDown: return VK_NEXT;
  case Qt::Key_End: return VK_END;
  case Qt::Key_Home: return VK_HOME;
  case Qt::Key_Left: return VK_LEFT;
  case Qt::Key_Up: return VK_UP;
  case Qt::Key_Right: return VK_RIGHT;
  case Qt::Key_Down: return VK_DOWN;
  case Qt::Key_Insert: return VK_INSERT;
  case Qt::Key_Delete: return VK_DELETE;
  case Qt::Key_Asterisk: return keypad ? VK_MULTIPLY : 0;
  case Qt::Key_Plus: return keypad ? VK_ADD : vkOemPlus;
  case Qt::Key_Minus: return keypad ? VK_SUBTRACT : vkOemMinus;
  case Qt::Key_Comma: return keypad ? VK_DECIMAL : vkOemComma;
  case Qt::Key_Period: return keypad ? VK_DECIMAL : vkOemPeriod;
  case Qt::Key_Slash: return keypad ? VK_DIVIDE : 0;
  }
  return 0;
}

LPARAM meos_qt::keyMessageParam(const QKeyEvent &event) {
  // Repeat count 1; bit 30: key was down before; bit 31: key is being released.
  DWORD param = 1;
  if (event.type() == QEvent::KeyRelease)
    param |= 0xC0000000u;
  else if (event.isAutoRepeat())
    param |= 0x40000000u;
  return LPARAM(param);
}

WPARAM meos_qt::mouseKeyFlags(Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
  WPARAM flags = 0;
  if (buttons & Qt::LeftButton)
    flags |= MK_LBUTTON;
  if (buttons & Qt::RightButton)
    flags |= MK_RBUTTON;
  if (buttons & Qt::MiddleButton)
    flags |= MK_MBUTTON;
  if (modifiers & Qt::ShiftModifier)
    flags |= MK_SHIFT;
  if (modifiers & Qt::ControlModifier)
    flags |= MK_CONTROL;
  return flags;
}

/* ---------------------------------------------------------------------
   Sending and posting
   --------------------------------------------------------------------- */

LRESULT SendMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (!meos_qt::isGuiThread()) {
    // As on Windows, a message sent from another thread is processed by the
    // thread that owns the window, and the sender waits.
    QCoreApplication *app = QCoreApplication::instance();
    if (!app || !meos_qt::windowOrError(window))
      return 0;
    LRESULT result = 0;
    QMetaObject::invokeMethod(
        app, [&] { result = SendMessage(window, message, wParam, lParam); }, Qt::BlockingQueuedConnection);
    return result;
  }

  const std::shared_ptr<meos_qt::Window> target = meos_qt::windowOrError(window);
  if (!target)
    return 0;
  return meos_qt::callWindowProc(target, message, wParam, lParam);
}

BOOL PostMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (window && !meos_qt::windowOrError(window))
    return FALSE;
  MSG msg = {window, message, wParam, lParam, GetTickCount(), {0, 0}, 0};
  meos_qt::postToQueue(msg);
  return TRUE;
}

/* ---------------------------------------------------------------------
   Timers
   --------------------------------------------------------------------- */

UINT_PTR SetTimer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC timerProc) {
  if (!meos_qt::isGuiThread()) {
    SetLastError(ERROR_ACCESS_DENIED);
    return 0;
  }
  if (window && !meos_qt::windowOrError(window))
    return 0;
  if (!timerHost)
    timerHost = new TimerHost(QCoreApplication::instance());

  elapse = std::clamp(elapse, timerMinimum, timerMaximum);

  // Without a window, an unknown id creates a timer with a new id.
  auto existing = findTimer(window, id);
  if (!window && (id == 0 || existing == timers.end())) {
    id = 1;
    while (findTimer(nullptr, id) != timers.end())
      id++;
    existing = timers.end();
  }

  if (existing != timers.end()) {
    // Setting an existing timer again replaces it.
    timerHost->killTimer(existing->qtTimer);
    existing->proc = timerProc;
    existing->qtTimer = timerHost->startTimer(int(elapse));
  }
  else {
    timers.push_back(Timer{window, id, timerProc, timerHost->startTimer(int(elapse))});
  }
  return window ? 1 : id;
}

BOOL KillTimer(HWND window, UINT_PTR id) {
  const auto timer = findTimer(window, id);
  if (timer == timers.end()) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (timerHost)
    timerHost->killTimer(timer->qtTimer);
  timers.erase(timer);
  return TRUE;
}

/* ---------------------------------------------------------------------
   Hooks. WH_KEYBOARD is called for the key events of the application
   (see createApplication), WH_CBT by the message box (step 1.2.5), and
   WH_GETMESSAGE by GetMessage for every message it returns (win32_app.cpp).
   Key input does not pass the message queue in this layer, so a WH_GETMESSAGE
   hook sees no key message; MeOS installs one to pass mouse messages to its
   tooltips, which Qt shows on its own hover event anyway.
   --------------------------------------------------------------------- */

HHOOK SetWindowsHookEx(int hookType, HOOKPROC hookProc, HINSTANCE /*module*/, DWORD /*threadId*/) {
  if (hookType != WH_KEYBOARD && hookType != WH_CBT && hookType != WH_GETMESSAGE) {
    SetLastError(errorInvalidHookFilter);
    return nullptr;
  }
  if (!hookProc || !meos_qt::isGuiThread()) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  const HHOOK handle = reinterpret_cast<HHOOK>(++lastHook);
  hooks.push_back(Hook{handle, hookType, hookProc});
  return handle;
}

BOOL UnhookWindowsHookEx(HHOOK hook) {
  const auto it = std::find_if(hooks.begin(), hooks.end(), [hook](const Hook &h) { return h.handle == hook; });
  if (it == hooks.end()) {
    SetLastError(ERROR_INVALID_HOOK_HANDLE);
    return FALSE;
  }
  hooks.erase(it);
  return TRUE;
}

LRESULT CallNextHookEx(HHOOK /*hook*/, int code, WPARAM wParam, LPARAM lParam) {
  if (activeHookCalls.empty())
    return 0;
  return callNextHook(*activeHookCalls.back(), code, wParam, lParam);
}

/* ---------------------------------------------------------------------
   Keyboard state
   --------------------------------------------------------------------- */

SHORT GetKeyState(int virtualKey) {
  Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
  if (inKeyEvent)
    modifiers |= keyEventModifiers;

  bool down = false;
  switch (virtualKey) {
  case VK_SHIFT:
    down = (modifiers & Qt::ShiftModifier) != 0;
    break;
  case VK_CONTROL:
    down = (modifiers & Qt::ControlModifier) != 0;
    break;
  case VK_MENU:
    down = (modifiers & Qt::AltModifier) != 0;
    break;
  case VK_LBUTTON:
    down = (QGuiApplication::mouseButtons() & Qt::LeftButton) != 0;
    break;
  case VK_RBUTTON:
    down = (QGuiApplication::mouseButtons() & Qt::RightButton) != 0;
    break;
  case VK_MBUTTON:
    down = (QGuiApplication::mouseButtons() & Qt::MiddleButton) != 0;
    break;
  default:
    down = virtualKey > 0 && virtualKey < int(keysDown.size()) && keysDown[std::size_t(virtualKey)];
    break;
  }
  // The high-order bit is set while the key is down.
  return static_cast<SHORT>(down ? 0x8000 : 0);
}
