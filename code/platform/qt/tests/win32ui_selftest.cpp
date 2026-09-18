/************************************************************************
    MeOS - Orienteering Software
    Linux port: self test of the Win32 window manager subset on Qt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Checks windows, messages, the message loop, timers and hooks of code/platform/qt
// against the Windows behaviour MeOS relies on. Runs with QT_QPA_PLATFORM=offscreen
// under ctest. Exits with a non-zero status if a check fails.

#include "platform/qt/win32_ui.h"

#include <windowsx.h>

#include <QAbstractSlider>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <cstdio>
#include <set>

namespace {

std::atomic<int> failures{0};

#define CHECK(expr)                                                                   \
  do {                                                                                \
    if (!(expr)) {                                                                    \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr);   \
      failures++;                                                                     \
    }                                                                                 \
  } while (false)

const wchar_t *const recordClass = L"SelfTestRecord";

struct Event {
  HWND window;
  UINT message;
  WPARAM wParam;
  LPARAM lParam;
};
std::vector<Event> events;
WINDOWPOS lastWindowPos;

LRESULT CALLBACK recordingProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  events.push_back(Event{window, message, wParam, lParam});
  if (message == WM_WINDOWPOSCHANGED)
    lastWindowPos = *reinterpret_cast<LPWINDOWPOS>(lParam);
  return DefWindowProc(window, message, wParam, lParam);
}

std::vector<UINT> messagesOf(HWND window) {
  std::vector<UINT> result;
  for (const Event &e : events) {
    if (e.window == window)
      result.push_back(e.message);
  }
  return result;
}

int countOf(HWND window, UINT message) {
  const std::vector<UINT> messages = messagesOf(window);
  return int(std::count(messages.begin(), messages.end(), message));
}

const Event *lastOf(HWND window, UINT message) {
  for (auto it = events.rbegin(); it != events.rend(); ++it) {
    if (it->window == window && it->message == message)
      return &*it;
  }
  return nullptr;
}

ATOM registerClass(const wchar_t *name, WNDPROC proc, UINT style = 0, int wndExtra = 0) {
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = style;
  wc.lpfnWndProc = proc;
  wc.cbWndExtra = wndExtra;
  wc.hInstance = meos_qt::applicationInstance();
  wc.lpszClassName = name;
  return RegisterClassEx(&wc);
}

HWND createTop(const wchar_t *className, DWORD style = WS_POPUP, int width = 200, int height = 100) {
  return CreateWindowEx(0, className, L"", style, 100, 50, width, height, nullptr, nullptr,
                        meos_qt::applicationInstance(), nullptr);
}

HWND createChild(HWND parent, const wchar_t *className, int x, int y, int width, int height, int id = 0,
                 DWORD style = WS_CHILD | WS_VISIBLE, DWORD exStyle = 0) {
  return CreateWindowEx(exStyle, className, L"", style, x, y, width, height, parent,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), meos_qt::applicationInstance(), nullptr);
}

QWidget *clientOf(HWND window) {
  const std::shared_ptr<meos_qt::Window> target = meos_qt::findWindow(window);
  return target ? target->client.data() : nullptr;
}

// Runs Qt's event processing (not GetMessage) until the condition holds.
bool waitFor(const std::function<bool()> &condition, int milliseconds = 3000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  while (!condition()) {
    if (std::chrono::steady_clock::now() > deadline)
      return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return true;
}

void processEventsFor(int milliseconds) {
  waitFor([] { return false; }, milliseconds);
}

void CALLBACK quitAfterTimeout(HWND, UINT, UINT_PTR, DWORD) {
  PostQuitMessage(99);
}

/* ------------------------------------------------------------------ */

void testClassRegistration() {
  CHECK(registerClass(recordClass, recordingProc) != 0);
  SetLastError(0);
  CHECK(registerClass(L"selftestrecord", recordingProc) == 0 && GetLastError() == ERROR_CLASS_ALREADY_EXISTS);

  SetLastError(0);
  CHECK(createTop(L"NoSuchClass") == nullptr && GetLastError() == ERROR_CANNOT_FIND_WND_CLASS);
  SetLastError(0);
  CHECK(createChild(nullptr, recordClass, 0, 0, 10, 10) == nullptr && GetLastError() != 0);

  // Class names are case insensitive; an atom works as a class name as well.
  HWND window = createTop(L"SELFTESTRECORD");
  CHECK(window != nullptr);
  CHECK(GetWindowLongPtr(window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(recordingProc));
  CHECK(GetWindowLongPtr(window, GWLP_HINSTANCE) == reinterpret_cast<LONG_PTR>(meos_qt::applicationInstance()));
  CHECK(DestroyWindow(window));

  const ATOM atom = registerClass(L"SelfTestExtra", recordingProc, 0, 16);
  CHECK(atom != 0);
  window = createTop(MAKEINTRESOURCE(atom));
  CHECK(window != nullptr);
  CHECK(SetWindowLongPtr(window, 8, 1234) == 0 && GetWindowLongPtr(window, 8) == 1234);
  CHECK(SetWindowLong(window, 12, -5) == 0 && GetWindowLong(window, 12) == -5);
  SetLastError(0);
  CHECK(GetWindowLongPtr(window, 12) == 0 && GetLastError() == ERROR_INVALID_INDEX);
  CHECK(DestroyWindow(window));
}

/* ------------------------------------------------------------------ */

LONG_PTR userDataInDestroy = 0;
WNDPROC recordingOriginal = nullptr;

LRESULT CALLBACK userDataProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_DESTROY)
    userDataInDestroy = GetWindowLongPtr(window, GWLP_USERDATA);
  return CallWindowProc(recordingOriginal, window, message, wParam, lParam);
}

void *createParams = nullptr;
int createWidth = 0;

LRESULT CALLBACK failingCreateProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_CREATE) {
    const auto *create = reinterpret_cast<const CREATESTRUCT *>(lParam);
    createParams = create->lpCreateParams;
    createWidth = create->cx;
    return -1;
  }
  return recordingProc(window, message, wParam, lParam);
}

void testCreateAndDestroyOrder() {
  events.clear();
  HWND top = createTop(recordClass, WS_OVERLAPPEDWINDOW, 300, 200);
  HWND owned = CreateWindowEx(WS_EX_TOOLWINDOW, recordClass, L"", WS_POPUP, 0, 0, 50, 50, top, nullptr,
                              meos_qt::applicationInstance(), nullptr);
  HWND child = createChild(top, recordClass, 5, 6, 100, 50, 42, WS_CHILD | WS_VISIBLE | WS_BORDER, WS_EX_CLIENTEDGE);
  HWND grandchild = createChild(child, recordClass, 0, 0, 20, 20, 7);
  CHECK(top && owned && child && grandchild);

  // WM_CREATE, then WM_SIZE with the client area inside WS_BORDER and WS_EX_CLIENTEDGE.
  const std::vector<UINT> childMessages = messagesOf(child);
  CHECK(childMessages.size() >= 2 && childMessages[0] == WM_CREATE && childMessages[1] == WM_SIZE);
  const Event *size = lastOf(child, WM_SIZE);
  CHECK(size && LOWORD(size->lParam) == 94 && HIWORD(size->lParam) == 44 && size->wParam == SIZE_RESTORED);
  RECT rc;
  CHECK(GetClientRect(child, &rc) && rc.left == 0 && rc.top == 0 && rc.right == 94 && rc.bottom == 44);

  CHECK(GetDlgItem(top, 42) == child);
  CHECK(GetDlgItem(top, 7) == nullptr);
  CHECK(GetDlgItem(child, 7) == grandchild);
  CHECK(GetWindowLongPtr(child, GWLP_ID) == 42);

  CHECK(!IsWindowVisible(child));
  CHECK(ShowWindow(top, SW_SHOW) == FALSE);
  CHECK(IsWindowVisible(top) && IsWindowVisible(child) && IsWindowVisible(grandchild));
  CHECK((GetWindowLong(top, GWL_STYLE) & WS_VISIBLE) != 0);
  CHECK(ShowWindow(top, SW_SHOW) == TRUE);

  SetWindowLongPtr(grandchild, GWLP_USERDATA, 99);
  recordingOriginal = reinterpret_cast<WNDPROC>(SetWindowLongPtr(grandchild, GWLP_WNDPROC,
                                                                 reinterpret_cast<LONG_PTR>(userDataProc)));
  CHECK(recordingOriginal == recordingProc);

  // Owned windows first; WM_DESTROY reaches a window before its children.
  events.clear();
  CHECK(DestroyWindow(top));
  std::vector<HWND> destroyed;
  for (const Event &e : events) {
    if (e.message == WM_DESTROY)
      destroyed.push_back(e.window);
  }
  CHECK((destroyed == std::vector<HWND>{owned, top, child, grandchild}));
  CHECK(userDataInDestroy == 99);

  SetLastError(0);
  CHECK(GetWindowLongPtr(child, GWLP_USERDATA) == 0 && GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  CHECK(!DestroyWindow(child));
  CHECK(!meos_qt::findWindow(owned) && !meos_qt::findWindow(grandchild));

  // WM_CREATE returning -1 fails the creation and destroys the window.
  CHECK(registerClass(L"SelfTestFailCreate", failingCreateProc) != 0);
  int marker = 0;
  events.clear();
  HWND failed = CreateWindowEx(0, L"SelfTestFailCreate", L"", WS_POPUP, 0, 0, 120, 80, nullptr, nullptr,
                               meos_qt::applicationInstance(), &marker);
  CHECK(failed == nullptr && createParams == &marker && createWidth == 120);
  CHECK(events.size() == 1 && events[0].message == WM_DESTROY);
}

/* ------------------------------------------------------------------ */

LRESULT CALLBACK recursiveProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_USER)
    return wParam == 0 ? 1 : 1 + SendMessage(window, WM_USER, wParam - 1, 0);
  if (message == WM_USER + 1) {
    // Destroyed inside its own handler, while further handlers are on the stack.
    CHECK(DestroyWindow(window));
    return SendMessage(window, WM_USER, 3, 0);
  }
  return DefWindowProc(window, message, wParam, lParam);
}

WNDPROC textOriginal = nullptr;

LRESULT CALLBACK upperCaseTextProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_SETTEXT && lParam) {
    std::wstring text = reinterpret_cast<LPCWSTR>(lParam);
    for (wchar_t &ch : text)
      ch = static_cast<wchar_t>(std::towupper(static_cast<std::wint_t>(ch)));
    return CallWindowProc(textOriginal, window, message, wParam, reinterpret_cast<LPARAM>(text.c_str()));
  }
  return CallWindowProc(textOriginal, window, message, wParam, lParam);
}

void testSendMessageAndSubclassing() {
  CHECK(registerClass(L"SelfTestRecursive", recursiveProc) != 0);
  HWND window = createTop(L"SelfTestRecursive");
  CHECK(SendMessage(window, WM_USER, 100, 0) == 101);
  CHECK(SendMessage(window, WM_USER + 1, 0, 0) == 0);
  CHECK(!meos_qt::findWindow(window));
  SetLastError(0);
  CHECK(SendMessage(reinterpret_cast<HWND>(std::uintptr_t(0x7654320)), WM_USER, 0, 0) == 0 &&
        GetLastError() == ERROR_INVALID_WINDOW_HANDLE);

  window = CreateWindowEx(0, recordClass, L"abc", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr,
                          meos_qt::applicationInstance(), nullptr);
  wchar_t buffer[16];
  CHECK(GetWindowText(window, buffer, 16) == 3 && std::wstring(buffer) == L"abc");
  CHECK(GetWindowTextLength(window) == 3);

  textOriginal = reinterpret_cast<WNDPROC>(
      SetWindowLongPtr(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(upperCaseTextProc)));
  CHECK(textOriginal == recordingProc);
  CHECK(SetWindowText(window, L"Hello"));
  CHECK(GetWindowText(window, buffer, 4) == 3 && std::wstring(buffer) == L"HEL");
  CHECK(meos_qt::findWindow(window)->frame->windowTitle() == QStringLiteral("HELLO"));
  CHECK(DestroyWindow(window));
}

/* ------------------------------------------------------------------ */

WPARAM clickFlags = 0;
LPARAM clickPoint = 0;

LRESULT CALLBACK destroyOnClickProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_LBUTTONDOWN) {
    clickFlags = wParam;
    clickPoint = lParam;
    // MeOS clears a page (and destroys the button) from the button's own callback.
    CHECK(DestroyWindow(window));
    CHECK(SendMessage(window, WM_USER, 0, 0) == 0);
    return 0;
  }
  return recordingProc(window, message, wParam, lParam);
}

void sendMouseEvent(QWidget *widget, QEvent::Type type, QPoint position, Qt::MouseButton button,
                    Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(type, QPointF(position), QPointF(widget->mapToGlobal(position)), button, buttons, modifiers);
  QCoreApplication::sendEvent(widget, &event);
}

void testMouseAndDestroyInOwnHandler() {
  CHECK(registerClass(L"SelfTestDestroyOnClick", destroyOnClickProc) != 0);
  HWND top = createTop(recordClass, WS_POPUP | WS_VISIBLE, 400, 200);
  HWND button = createChild(top, L"SelfTestDestroyOnClick", 10, 10, 50, 20);
  QPointer<QWidget> frame = meos_qt::findWindow(button)->frame.data();
  QPointer<QWidget> client = clientOf(button);

  sendMouseEvent(client, QEvent::MouseButtonPress, QPoint(5, 7), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  CHECK(clickFlags == (MK_LBUTTON | MK_SHIFT) && GET_X_LPARAM(clickPoint) == 5 && GET_Y_LPARAM(clickPoint) == 7);
  CHECK(!meos_qt::findWindow(button));
  // The widget survives until Qt deletes it outside the event handler.
  CHECK(frame && client);
  events.clear();
  sendMouseEvent(client, QEvent::MouseButtonRelease, QPoint(5, 7), Qt::LeftButton, Qt::NoButton);
  CHECK(events.empty());
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  CHECK(!frame && !client);

  // Capture: all mouse input goes to the capturing window, in its coordinates.
  HWND a = createChild(top, recordClass, 0, 0, 100, 100);
  HWND b = createChild(top, recordClass, 150, 0, 100, 100);
  CHECK(SetCapture(a) == nullptr && GetCapture() == a);
  events.clear();
  sendMouseEvent(clientOf(b), QEvent::MouseMove, QPoint(10, 20), Qt::NoButton, Qt::NoButton);
  const Event *move = lastOf(a, WM_MOUSEMOVE);
  CHECK(move && GET_X_LPARAM(move->lParam) == 160 && GET_Y_LPARAM(move->lParam) == 20);
  CHECK(countOf(b, WM_MOUSEMOVE) == 0);
  sendMouseEvent(clientOf(a), QEvent::MouseMove, QPoint(-3, 4), Qt::NoButton, Qt::NoButton);
  move = lastOf(a, WM_MOUSEMOVE);
  CHECK(move && GET_X_LPARAM(move->lParam) == -3 && GET_Y_LPARAM(move->lParam) == 4);
  CHECK(ReleaseCapture() && GetCapture() == nullptr);
  sendMouseEvent(clientOf(b), QEvent::MouseMove, QPoint(10, 20), Qt::NoButton, Qt::NoButton);
  CHECK(countOf(b, WM_MOUSEMOVE) == 1);

  // A destroyed window loses the capture.
  SetCapture(b);
  CHECK(DestroyWindow(b) && GetCapture() == nullptr);

  // Double clicks need CS_DBLCLKS.
  events.clear();
  sendMouseEvent(clientOf(a), QEvent::MouseButtonDblClick, QPoint(1, 1), Qt::LeftButton, Qt::LeftButton);
  CHECK(countOf(a, WM_LBUTTONDOWN) == 1 && countOf(a, WM_LBUTTONDBLCLK) == 0);
  CHECK(registerClass(L"SelfTestDoubleClicks", recordingProc, CS_DBLCLKS) != 0);
  HWND c = createChild(top, L"SelfTestDoubleClicks", 0, 120, 50, 50);
  sendMouseEvent(clientOf(c), QEvent::MouseButtonDblClick, QPoint(1, 1), Qt::LeftButton, Qt::LeftButton);
  CHECK(countOf(c, WM_LBUTTONDBLCLK) == 1);

  events.clear();
  QWheelEvent wheel(QPointF(2, 3), QPointF(clientOf(a)->mapToGlobal(QPoint(2, 3))), QPoint(), QPoint(0, -120),
                    Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(clientOf(a), &wheel);
  const Event *wheelMessage = lastOf(a, WM_MOUSEWHEEL);
  CHECK(wheelMessage && GET_WHEEL_DELTA_WPARAM(wheelMessage->wParam) == -120 &&
        LOWORD(wheelMessage->wParam) == MK_CONTROL);
  CHECK(wheelMessage && GET_X_LPARAM(wheelMessage->lParam) == 100 + 2 && GET_Y_LPARAM(wheelMessage->lParam) == 50 + 3);

  CHECK(DestroyWindow(top));
}

/* ------------------------------------------------------------------ */

std::vector<std::pair<UINT, WPARAM>> received;
std::thread::id receivedThread;

LRESULT CALLBACK postedProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message >= WM_USER && message <= WM_USER + 20) {
    received.emplace_back(message, wParam);
    if (message == WM_USER + 1) {
      receivedThread = std::this_thread::get_id();
      return LRESULT(1000 + wParam);
    }
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

void testPostMessageFromThread() {
  CHECK(registerClass(L"SelfTestPosted", postedProc) != 0);
  HWND window = createTop(L"SelfTestPosted");
  received.clear();

  std::atomic<LRESULT> sendResult{0};
  std::thread poster([window, &sendResult] {
    for (int i = 0; i < 10; i++)
      CHECK(PostMessage(window, WM_USER, WPARAM(i), 0));
    sendResult = SendMessage(window, WM_USER + 1, 5, 0);
    CHECK(PostMessage(window, WM_USER + 2, 0, 0));
    CHECK(!PostMessage(reinterpret_cast<HWND>(std::uintptr_t(0x7654320)), WM_USER, 0, 0));
  });

  const UINT_PTR safety = SetTimer(nullptr, 0, 10000, quitAfterTimeout);
  MSG msg;
  BOOL result;
  while ((result = GetMessage(&msg, nullptr, 0, 0)) != 0) {
    CHECK(result != -1);
    DispatchMessage(&msg);
    if (msg.message == WM_USER + 2)
      PostQuitMessage(3);
  }
  KillTimer(nullptr, safety);
  poster.join();

  CHECK(msg.message == WM_QUIT && msg.wParam == 3);
  CHECK(sendResult == 1005 && receivedThread == std::this_thread::get_id());
  std::vector<WPARAM> postedOrder;
  for (const auto &entry : received) {
    if (entry.first == WM_USER)
      postedOrder.push_back(entry.second);
  }
  CHECK((postedOrder == std::vector<WPARAM>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
  CHECK(received.size() == 12 && received.back().first == WM_USER + 2);
  CHECK(DestroyWindow(window));
}

QEventLoop *modalLoop = nullptr;
bool receivedInModalLoop = false;

LRESULT CALLBACK modalProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_TIMER: {
    // Stands for a message box opened from a handler that Qt calls while GetMessage
    // waits (input or a timer): a nested loop in which posted messages must still
    // be dispatched.
    KillTimer(window, wParam);
    QEventLoop loop;
    modalLoop = &loop;
    PostMessage(window, WM_USER + 1, 0, 0);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    modalLoop = nullptr;
    return 0;
  }
  case WM_USER + 1:
    receivedInModalLoop = modalLoop != nullptr;
    if (modalLoop)
      modalLoop->quit();
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

void testPostedMessageInModalLoop() {
  CHECK(registerClass(L"SelfTestModal", modalProc) != 0);
  HWND window = createTop(L"SelfTestModal");
  CHECK(SetTimer(window, 1, 10, nullptr) != 0);
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0))
    DispatchMessage(&msg);
  CHECK(receivedInModalLoop);
  CHECK(DestroyWindow(window));
}

/* ------------------------------------------------------------------ */

int procCalls = 0;
UINT_PTR procId = 0;
UINT procMessage = 0;
HWND procWindow = nullptr;

void CALLBACK oneShotProc(HWND window, UINT message, UINT_PTR id, DWORD /*time*/) {
  procCalls++;
  procId = id;
  procMessage = message;
  procWindow = window;
  KillTimer(window, id);
}

int threadTimerCalls = 0;
void CALLBACK countingProc(HWND, UINT, UINT_PTR, DWORD) {
  threadTimerCalls++;
}

void CALLBACK quitProc(HWND window, UINT, UINT_PTR id, DWORD) {
  KillTimer(window, id);
  PostQuitMessage(7);
}

void testTimers() {
  HWND window = createTop(recordClass);
  events.clear();
  CHECK(SetTimer(window, 7, 10, nullptr) != 0);
  CHECK(waitFor([window] { return countOf(window, WM_TIMER) >= 2; }));
  const Event *timer = lastOf(window, WM_TIMER);
  CHECK(timer && timer->wParam == 7 && timer->lParam == 0);
  CHECK(KillTimer(window, 7));
  CHECK(!KillTimer(window, 7));

  // A TIMERPROC with a pointer as id, as gdioutput uses it, killed in its own call.
  static int marker;
  const auto pointerId = reinterpret_cast<UINT_PTR>(&marker);
  CHECK(SetTimer(window, pointerId, 10, oneShotProc) != 0);
  CHECK(waitFor([] { return procCalls == 1; }));
  processEventsFor(60);
  CHECK(procCalls == 1 && procId == pointerId && procMessage == WM_TIMER && procWindow == window);

  // Setting a timer again replaces it.
  CHECK(SetTimer(window, 8, 10, nullptr) && SetTimer(window, 8, 10, nullptr));
  CHECK(KillTimer(window, 8) && !KillTimer(window, 8));

  // Thread timers get their own ids.
  const UINT_PTR first = SetTimer(nullptr, 0, 10, countingProc);
  const UINT_PTR second = SetTimer(nullptr, 0, 10, countingProc);
  CHECK(first != 0 && second != 0 && first != second);
  CHECK(waitFor([] { return threadTimerCalls >= 2; }));
  CHECK(KillTimer(nullptr, first) && KillTimer(nullptr, second));

  // Destroying a window kills its timers.
  HWND shortLived = createTop(recordClass);
  procCalls = 0;
  CHECK(SetTimer(shortLived, 1, 10, oneShotProc) != 0);
  CHECK(DestroyWindow(shortLived));
  processEventsFor(60);
  CHECK(procCalls == 0);

  // Timers run while GetMessage waits.
  CHECK(SetTimer(window, 9, 20, quitProc) != 0);
  const UINT_PTR safety = SetTimer(nullptr, 0, 10000, quitAfterTimeout);
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0))
    DispatchMessage(&msg);
  KillTimer(nullptr, safety);
  CHECK(msg.message == WM_QUIT && msg.wParam == 7);
  CHECK(DestroyWindow(window));
}

/* ------------------------------------------------------------------ */

std::vector<std::pair<WPARAM, LPARAM>> hookCalls;
int olderHookCalls = 0;
bool shiftDownInHook = false;

LRESULT CALLBACK tabHook(int code, WPARAM wParam, LPARAM lParam) {
  if (code < 0)
    return CallNextHookEx(nullptr, code, wParam, lParam);
  hookCalls.emplace_back(wParam, lParam);
  if (wParam == VK_TAB) {
    shiftDownInHook = GetKeyState(VK_SHIFT) < 0;
    return 1;
  }
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK olderHook(int code, WPARAM wParam, LPARAM lParam) {
  olderHookCalls++;
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

void sendKeyEvent(QWidget *widget, QEvent::Type type, int key, Qt::KeyboardModifiers modifiers,
                  const QString &text = QString()) {
  QKeyEvent event(type, key, modifiers, text);
  QCoreApplication::sendEvent(widget, &event);
}

void testKeyboardHook() {
  HWND window = createTop(recordClass);
  QWidget *client = clientOf(window);

  const HHOOK older = SetWindowsHookEx(WH_KEYBOARD, olderHook, nullptr, GetCurrentThreadId());
  const HHOOK newer = SetWindowsHookEx(WH_KEYBOARD, tabHook, nullptr, GetCurrentThreadId());
  CHECK(older && newer && older != newer);

  // The newest hook runs first and discards Shift+Tab (Qt reports it as Backtab).
  events.clear();
  sendKeyEvent(client, QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
  CHECK(hookCalls.size() == 1 && hookCalls[0].first == VK_TAB && (DWORD(hookCalls[0].second) & 0x80000000u) == 0);
  CHECK(shiftDownInHook && olderHookCalls == 0 && events.empty());
  sendKeyEvent(client, QEvent::KeyRelease, Qt::Key_Tab, Qt::NoModifier);
  CHECK(hookCalls.size() == 2 && (DWORD(hookCalls[1].second) & 0xC0000000u) == 0xC0000000u);
  CHECK(!shiftDownInHook && events.empty());

  // Other keys pass both hooks and reach the window as WM_KEYDOWN and WM_CHAR.
  sendKeyEvent(client, QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
  CHECK(hookCalls.size() == 3 && hookCalls[2].first == 'A' && olderHookCalls == 1);
  CHECK(events.size() == 2 && events[0].message == WM_KEYDOWN && events[0].wParam == 'A' &&
        events[1].message == WM_CHAR && events[1].wParam == 'a');
  CHECK(GetKeyState('A') < 0);
  sendKeyEvent(client, QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
  CHECK(GetKeyState('A') == 0);
  CHECK(lastOf(window, WM_KEYUP) && lastOf(window, WM_KEYUP)->wParam == 'A');

  events.clear();
  sendKeyEvent(client, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
  sendKeyEvent(client, QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier, QString(QChar(0x7F)));
  sendKeyEvent(client, QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
  sendKeyEvent(client, QEvent::KeyPress, Qt::Key_Plus, Qt::KeypadModifier, QStringLiteral("+"));
  std::vector<std::pair<UINT, WPARAM>> keys;
  for (const Event &e : events)
    keys.emplace_back(e.message, e.wParam);
  CHECK((keys == std::vector<std::pair<UINT, WPARAM>>{{WM_KEYDOWN, VK_RETURN}, {WM_CHAR, L'\r'},
                                                      {WM_KEYDOWN, VK_DELETE}, {WM_KEYDOWN, VK_NEXT},
                                                      {WM_KEYDOWN, VK_ADD}, {WM_CHAR, L'+'}}));

  CHECK(UnhookWindowsHookEx(newer));
  SetLastError(0);
  CHECK(!UnhookWindowsHookEx(newer) && GetLastError() == ERROR_INVALID_HOOK_HANDLE);
  events.clear();
  sendKeyEvent(client, QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
  CHECK(olderHookCalls == 7 && countOf(window, WM_KEYDOWN) == 1);
  CHECK(UnhookWindowsHookEx(older));
  CHECK(CallNextHookEx(nullptr, 0, 0, 0) == 0);
  CHECK(DestroyWindow(window));
}

/* ------------------------------------------------------------------ */

std::vector<MSG> getMessageHookCalls;
bool changeMessageInHook = false;

LRESULT CALLBACK getMessageHook(int code, WPARAM wParam, LPARAM lParam) {
  if (code >= 0) {
    auto *msg = reinterpret_cast<MSG *>(lParam);
    getMessageHookCalls.push_back(*msg);
    CHECK(wParam == PM_REMOVE);
    // A WH_GETMESSAGE hook may change the message it is given.
    if (changeMessageInHook)
      msg->wParam = 4711;
  }
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

// MeOS installs a WH_GETMESSAGE hook to pass mouse messages to its tooltips.
// This layer delivers key and mouse input directly to the window procedure, so
// the hook sees only what the application posts.
void testGetMessageHook() {
  HWND window = createTop(recordClass);
  const HHOOK hook = SetWindowsHookEx(WH_GETMESSAGE, getMessageHook, nullptr, GetCurrentThreadId());
  CHECK(hook != nullptr);

  events.clear();
  getMessageHookCalls.clear();
  CHECK(PostMessage(window, WM_USER + 5, 7, 8));
  MSG msg;
  CHECK(GetMessage(&msg, nullptr, 0, 0));
  CHECK(getMessageHookCalls.size() == 1);
  CHECK(getMessageHookCalls.size() == 1 && getMessageHookCalls[0].message == WM_USER + 5 &&
        getMessageHookCalls[0].wParam == 7 && getMessageHookCalls[0].hwnd == window);
  DispatchMessage(&msg);
  CHECK(lastOf(window, WM_USER + 5) && lastOf(window, WM_USER + 5)->wParam == 7);

  // A message the hook changed is the one that is dispatched.
  changeMessageInHook = true;
  CHECK(PostMessage(window, WM_USER + 5, 7, 8));
  CHECK(GetMessage(&msg, nullptr, 0, 0) && msg.wParam == 4711);
  DispatchMessage(&msg);
  CHECK(lastOf(window, WM_USER + 5) && lastOf(window, WM_USER + 5)->wParam == 4711);
  changeMessageInHook = false;

  // MeOS finds the gdioutput of a mouse message with IsChild: a child window of
  // any depth belongs to its top-level window.
  HWND child = CreateWindowEx(0, recordClass, L"child", WS_CHILD | WS_VISIBLE, 0, 0, 50, 50, window, nullptr,
                              meos_qt::applicationInstance(), nullptr);
  HWND grandChild = CreateWindowEx(0, recordClass, L"grand", WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, child, nullptr,
                                   meos_qt::applicationInstance(), nullptr);
  HWND other = createTop(recordClass);
  CHECK(IsChild(window, child) && IsChild(window, grandChild) && IsChild(child, grandChild));
  CHECK(!IsChild(grandChild, child) && !IsChild(window, other) && !IsChild(window, window));
  CHECK(!IsChild(nullptr, child) && !IsChild(window, nullptr));
  CHECK(DestroyWindow(other));

  // Input does not pass the queue, so the hook does not see it.
  getMessageHookCalls.clear();
  sendKeyEvent(clientOf(window), QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
  CHECK(getMessageHookCalls.empty() && countOf(window, WM_KEYDOWN) == 1);

  CHECK(UnhookWindowsHookEx(hook));
  getMessageHookCalls.clear();
  CHECK(PostMessage(window, WM_USER + 5, 1, 0));
  CHECK(GetMessage(&msg, nullptr, 0, 0) && getMessageHookCalls.empty());
  CHECK(DestroyWindow(window));
}

/* ------------------------------------------------------------------ */

void testHandleReuse() {
  std::set<HWND> handles;
  HWND first = nullptr;
  for (int i = 0; i < 50; i++) {
    HWND window = createTop(recordClass);
    CHECK(window && handles.insert(window).second);
    if (i == 0)
      first = window;
    CHECK(DestroyWindow(window));
  }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  HWND window = createTop(recordClass);
  CHECK(window && !handles.count(window));
  CHECK(!meos_qt::findWindow(first) && !IsWindowEnabled(first) && !IsWindowVisible(first));
  SetLastError(0);
  CHECK(SendMessage(first, WM_USER, 0, 0) == 0 && GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
  CHECK(!PostMessage(first, WM_USER, 0, 0));
  CHECK(!SetTimer(first, 1, 10, nullptr));
  CHECK(DestroyWindow(window));
}

/* ------------------------------------------------------------------ */

void testScrollBars() {
  HWND window = createTop(recordClass, WS_POPUP, 200, 100);
  RECT rc;
  CHECK(GetClientRect(window, &rc) && rc.right == 200 && rc.bottom == 100);

  // A range that does not fit on a page shows the scroll bar, which narrows the client area.
  events.clear();
  SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE, 0, 299, 100, 0, 0};
  CHECK(SetScrollInfo(window, SB_VERT, &si, TRUE) == 0);
  CHECK((GetWindowLong(window, GWL_STYLE) & WS_VSCROLL) != 0);
  CHECK(GetClientRect(window, &rc) && rc.right == 200 - meos_qt::scrollBarExtent && rc.bottom == 100);
  const std::vector<UINT> messages = messagesOf(window);
  CHECK((messages == std::vector<UINT>{WM_WINDOWPOSCHANGED, WM_SIZE}));
  const Event *size = lastOf(window, WM_SIZE);
  CHECK(size && LOWORD(size->lParam) == 183 && HIWORD(size->lParam) == 100);

  // The position is clamped to the last full page.
  si.fMask = SIF_POS;
  si.nPos = 500;
  CHECK(SetScrollInfo(window, SB_VERT, &si, TRUE) == 200);
  SCROLLINFO out = {sizeof(out), SIF_ALL, 0, 0, 0, 0, 0};
  CHECK(GetScrollInfo(window, SB_VERT, &out) && out.nMin == 0 && out.nMax == 299 && out.nPage == 100 &&
        out.nPos == 200);

  // User actions send WM_VSCROLL; only SetScrollInfo moves the thumb.
  QScrollBar *bar = meos_qt::findWindow(window)->scrollBars[SB_VERT];
  CHECK(bar && !bar->isHidden() && bar->value() == 200 && bar->maximum() == 200);
  events.clear();
  bar->triggerAction(QAbstractSlider::SliderSingleStepSub);
  bar->triggerAction(QAbstractSlider::SliderPageStepAdd);
  CHECK(events.size() == 2 && events[0].message == WM_VSCROLL && LOWORD(events[0].wParam) == SB_LINEUP &&
        LOWORD(events[1].wParam) == SB_PAGEDOWN);
  CHECK(bar->value() == 200);

  // A range that fits hides the scroll bar again.
  events.clear();
  si.fMask = SIF_RANGE | SIF_PAGE;
  si.nMax = 99;
  SetScrollInfo(window, SB_VERT, &si, TRUE);
  CHECK((GetWindowLong(window, GWL_STYLE) & WS_VSCROLL) == 0 && bar->isHidden());
  size = lastOf(window, WM_SIZE);
  CHECK(size && LOWORD(size->lParam) == 200);

  si = {sizeof(si), SIF_RANGE | SIF_PAGE, 0, 0, 0, 0, 0};
  SetScrollInfo(window, SB_HORZ, &si, TRUE);
  CHECK((GetWindowLong(window, GWL_STYLE) & WS_HSCROLL) == 0);
  CHECK(DestroyWindow(window));
}

/* ------------------------------------------------------------------ */

QRect paintedRect;

LRESULT CALLBACK paintProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_PAINT)
    paintedRect = meos_qt::findWindow(window)->paintRegion.boundingRect();
  return recordingProc(window, message, wParam, lParam);
}

void testPaint() {
  CHECK(registerClass(L"SelfTestPaint", paintProc) != 0);
  HWND window = createTop(L"SelfTestPaint", WS_POPUP, 50, 50);

  // Hidden windows are not painted.
  events.clear();
  CHECK(InvalidateRect(window, nullptr, TRUE) && UpdateWindow(window));
  CHECK(countOf(window, WM_PAINT) == 0);

  CHECK(ShowWindow(window, SW_SHOW) == FALSE);
  CHECK(UpdateWindow(window) && countOf(window, WM_PAINT) == 1 && paintedRect == QRect(0, 0, 50, 50));
  CHECK(UpdateWindow(window) && countOf(window, WM_PAINT) == 1);

  RECT rect = {5, 6, 15, 26};
  CHECK(InvalidateRect(window, &rect, FALSE));
  CHECK(UpdateWindow(window) && countOf(window, WM_PAINT) == 2 && paintedRect == QRect(5, 6, 10, 20));

  // Qt's paint event sends WM_PAINT for the update region.
  CHECK(InvalidateRect(window, &rect, FALSE));
  CHECK(waitFor([window] { return countOf(window, WM_PAINT) == 3; }));
  CHECK(DestroyWindow(window));
}

/* ------------------------------------------------------------------ */

void testGeometryAndState() {
  HWND top = createTop(recordClass, WS_POPUP, 400, 300);
  HWND child = createChild(top, recordClass, 10, 20, 100, 50);

  events.clear();
  CHECK(SetWindowPos(child, nullptr, 30, 40, 0, 0, SWP_NOSIZE | SWP_NOZORDER));
  CHECK(countOf(child, WM_WINDOWPOSCHANGED) == 1 && countOf(child, WM_SIZE) == 0);
  CHECK(lastWindowPos.hwnd == child && lastWindowPos.x == 30 && lastWindowPos.y == 40 &&
        lastWindowPos.cx == 100 && (lastWindowPos.flags & SWP_NOSIZE) && !(lastWindowPos.flags & SWP_NOMOVE));

  CHECK(MoveWindow(child, 30, 40, 120, 60, TRUE));
  const Event *size = lastOf(child, WM_SIZE);
  CHECK(size && LOWORD(size->lParam) == 120 && HIWORD(size->lParam) == 60);

  RECT rc;
  CHECK(GetWindowRect(child, &rc) && rc.left == 130 && rc.top == 90 && rc.right == 250 && rc.bottom == 150);
  POINT point = {5, 5};
  CHECK(ClientToScreen(child, &point) && point.x == 135 && point.y == 95);
  CHECK(ScreenToClient(top, &point) && point.x == 35 && point.y == 45);

  CHECK(EnableWindow(child, FALSE) == FALSE);
  CHECK(!IsWindowEnabled(child) && (GetWindowLong(child, GWL_STYLE) & WS_DISABLED));
  CHECK(EnableWindow(child, TRUE) == TRUE && IsWindowEnabled(child));

  CHECK(ShowWindow(child, SW_HIDE) == TRUE);
  CHECK(!(GetWindowLong(child, GWL_STYLE) & WS_VISIBLE));

  // Style changes keep the visibility, which only ShowWindow changes.
  ShowWindow(top, SW_SHOW);
  SetWindowLong(top, GWL_STYLE, WS_OVERLAPPEDWINDOW);
  CHECK(GetWindowLong(top, GWL_STYLE) == LONG(WS_OVERLAPPEDWINDOW | WS_VISIBLE));
  CHECK(!(meos_qt::findWindow(top)->frame->windowFlags() & Qt::FramelessWindowHint));

  CHECK(GetClientRect(GetDesktopWindow(), &rc) && rc.right > 0 && rc.bottom > 0);
  CHECK(DestroyWindow(top));
}

LRESULT CALLBACK keepOpenProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_CLOSE)
    return 0;
  return recordingProc(window, message, wParam, lParam);
}

void testClose() {
  // Closing a window asks its procedure; DefWindowProc destroys it.
  HWND window = createTop(recordClass, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
  meos_qt::findWindow(window)->frame->close();
  CHECK(!meos_qt::findWindow(window));

  CHECK(registerClass(L"SelfTestKeepOpen", keepOpenProc) != 0);
  window = createTop(L"SelfTestKeepOpen", WS_OVERLAPPEDWINDOW | WS_VISIBLE);
  meos_qt::findWindow(window)->frame->close();
  CHECK(meos_qt::findWindow(window) != nullptr);
  CHECK(PostMessage(window, WM_CLOSE, 0, 0));
  processEventsFor(20);
  CHECK(meos_qt::findWindow(window) != nullptr);
  CHECK(DestroyWindow(window));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

} // namespace

int main(int argc, char **argv) {
  const std::unique_ptr<QApplication> app = meos_qt::createApplication(argc, argv);

  testClassRegistration();
  testCreateAndDestroyOrder();
  testSendMessageAndSubclassing();
  testMouseAndDestroyInOwnHandler();
  testPostMessageFromThread();
  testPostedMessageInModalLoop();
  testTimers();
  testKeyboardHook();
  testGetMessageHook();
  testHandleReuse();
  testScrollBars();
  testPaint();
  testGeometryAndState();
  testClose();

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures.load());
    return 1;
  }
  std::printf("win32ui self test passed\n");
  return 0;
}
