/************************************************************************
    MeOS - Orienteering Software
    Linux port: the Qt application and the Win32 message loop.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_ui.h"

#include <QKeyEvent>
#include <QThread>

namespace {

class Application : public QApplication {
public:
  Application(int &argc, char **argv) : QApplication(argc, argv) {
    // Windows programs end with PostQuitMessage, not with their last window.
    setQuitOnLastWindowClosed(false);
  }

  ~Application() override {
    meos_qt::destroyAllWindows();
  }

  // The WH_KEYBOARD hooks see every key before it reaches a widget. This happens
  // here and not in an application event filter, which Qt calls again for each
  // parent a key event propagates to.
  bool notify(QObject *receiver, QEvent *event) override {
    const QEvent::Type type = event->type();
    if ((type == QEvent::KeyPress || type == QEvent::KeyRelease) && receiver->isWidgetType()) {
      const QKeyEvent &key = static_cast<const QKeyEvent &>(*event);
      meos_qt::KeyEventScope scope(key);
      if (meos_qt::filterKeyEvent(key))
        return true;
      return QApplication::notify(receiver, event);
    }
    return QApplication::notify(receiver, event);
  }
};

// WM_QUIT is not queued; GetMessage returns it once the queue is empty.
bool quitPosted = false;
int quitExitCode = 0;

} // namespace

std::unique_ptr<QApplication> meos_qt::createApplication(int &argc, char **argv) {
  // Window positions in screen coordinates exist only with X11; under Wayland
  // XWayland provides them.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
    qputenv("QT_QPA_PLATFORM", "xcb");
  // GDI measures and draws text with the classic TrueType hinting, which also
  // produced the hdmx tables of the fonts. FreeType's newer interpreter hints
  // advance widths differently, so text would be up to 4 % wider or narrower
  // than on Windows (measured with tests/gdi_metrics.cpp). Read by FreeType when
  // Qt loads the first font.
  if (qEnvironmentVariableIsEmpty("FREETYPE_PROPERTIES"))
    qputenv("FREETYPE_PROPERTIES", "truetype:interpreter-version=35");
  return std::make_unique<Application>(argc, argv);
}

HINSTANCE meos_qt::applicationInstance() {
  return reinterpret_cast<HINSTANCE>(std::uintptr_t(0x400000));
}

bool meos_qt::isGuiThread() {
  const QCoreApplication *app = QCoreApplication::instance();
  return app && QThread::currentThread() == app->thread();
}

// Posted messages come from the queue in win32_message.cpp. Input, painting and
// timers reach the window procedures directly through Qt, so the loop mainly
// keeps Qt's event processing going while it waits.
BOOL GetMessage(LPMSG msg, HWND window, UINT filterMin, UINT filterMax) {
  if (!msg || !meos_qt::isGuiThread()) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return -1;
  }

  meos_qt::GetMessageWait wait;
  for (;;) {
    if (meos_qt::takePostedMessage(*msg, window, filterMin, filterMax))
      return TRUE;
    if (quitPosted) {
      quitPosted = false;
      *msg = MSG{nullptr, WM_QUIT, WPARAM(quitExitCode), 0, GetTickCount(), {0, 0}, 0};
      return FALSE;
    }
    // Qt deletes objects whose deleteLater was called outside any event handler
    // (for example in a posted message dispatched from here) only on request.
    // Outside all handlers nothing on the stack can still use them.
    if (meos_qt::handlerDepth() == 0)
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
  }
}

// Keyboard input is not queued, and the canvas sends WM_CHAR itself.
BOOL TranslateMessage(const MSG * /*msg*/) {
  return FALSE;
}

LRESULT DispatchMessage(const MSG *msg) {
  if (!msg)
    return 0;
  if (msg->message == WM_TIMER && msg->lParam) {
    meos_qt::HandlerScope scope;
    reinterpret_cast<TIMERPROC>(msg->lParam)(msg->hwnd, WM_TIMER, msg->wParam, GetTickCount());
    return 0;
  }
  const std::shared_ptr<meos_qt::Window> target = meos_qt::findWindow(msg->hwnd);
  if (!target)
    return 0;
  return meos_qt::callWindowProc(target, msg->message, msg->wParam, msg->lParam);
}

void PostQuitMessage(int exitCode) {
  quitPosted = true;
  quitExitCode = exitCode;
  // Wakes a GetMessage that waits for Qt events.
  if (QCoreApplication *app = QCoreApplication::instance())
    QMetaObject::invokeMethod(app, [] {}, Qt::QueuedConnection);
}
