/************************************************************************
    MeOS - Orienteering Software
    Linux port: the Qt application and the Win32 message loop.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_ui.h"

#include <QFontDatabase>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QStyleFactory>
#include <QThread>

#include <map>
#include <sstream>

// Registers the fonts compiled in from fonts/meos_fonts.qrc (outside any namespace,
// as Q_INIT_RESOURCE requires).
static void initFontResources() {
  Q_INIT_RESOURCE(meos_fonts);
}

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
    if (!receiver->isWidgetType())
      return QApplication::notify(receiver, event);
    const auto widget = static_cast<QWidget *>(receiver);

    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
      const QKeyEvent &key = static_cast<const QKeyEvent &>(*event);
      meos_qt::KeyEventScope scope(key);
      if (meos_qt::filterKeyEvent(key) || meos_qt::dialogKeyEvent(widget, key))
        return true;
      return QApplication::notify(receiver, event);
    }
    // The layer keeps the focus window and sends WM_KILLFOCUS/WM_SETFOCUS.
    case QEvent::FocusIn:
    case QEvent::FocusOut: {
      const bool result = QApplication::notify(receiver, event);
      const auto &focus = static_cast<const QFocusEvent &>(*event);
      meos_qt::qtFocusEvent(widget, event->type() == QEvent::FocusIn, focus.reason());
      return result;
    }
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
      if (meos_qt::redirectMouseToCapture(widget, static_cast<QMouseEvent *>(event)))
        return true;
      return QApplication::notify(receiver, event);
    case QEvent::ToolTip:
      if (meos_qt::showToolTip(widget, static_cast<QHelpEvent *>(event)))
        return true;
      return QApplication::notify(receiver, event);
    default:
      return QApplication::notify(receiver, event);
    }
  }
};

// Qt colours from the Windows 10 system colours (see GetSysColor), so that the
// controls do not follow a dark desktop theme either.
QPalette windowsPalette() {
  const auto color = [](int index) { return QColor(GetRValue(GetSysColor(index)), GetGValue(GetSysColor(index)),
                                                   GetBValue(GetSysColor(index))); };
  QPalette palette;
  for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    const bool disabled = group == QPalette::Disabled;
    palette.setColor(group, QPalette::Window, color(COLOR_3DFACE));
    palette.setColor(group, QPalette::WindowText, color(disabled ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
    palette.setColor(group, QPalette::Base, color(disabled ? COLOR_3DFACE : COLOR_WINDOW));
    palette.setColor(group, QPalette::AlternateBase, color(COLOR_WINDOW));
    palette.setColor(group, QPalette::Text, color(disabled ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
    palette.setColor(group, QPalette::Button, color(COLOR_3DFACE));
    palette.setColor(group, QPalette::ButtonText, color(disabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT));
    palette.setColor(group, QPalette::Highlight, color(COLOR_HIGHLIGHT));
    palette.setColor(group, QPalette::HighlightedText, color(COLOR_HIGHLIGHTTEXT));
    palette.setColor(group, QPalette::ToolTipBase, color(COLOR_INFOBK));
    palette.setColor(group, QPalette::ToolTipText, color(COLOR_INFOTEXT));
    palette.setColor(group, QPalette::Light, color(COLOR_3DHIGHLIGHT));
    palette.setColor(group, QPalette::Midlight, color(COLOR_3DLIGHT));
    palette.setColor(group, QPalette::Mid, color(COLOR_3DSHADOW));
    palette.setColor(group, QPalette::Dark, color(COLOR_3DSHADOW));
    palette.setColor(group, QPalette::Shadow, color(COLOR_3DDKSHADOW));
    palette.setColor(group, QPalette::Link, color(COLOR_HOTLIGHT));
  }
  return palette;
}

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
  auto app = std::make_unique<Application>(argc, argv);

  // MeOS places controls by its own measures; a desktop style with larger
  // margins would make them overlap.
  QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  QApplication::setPalette(windowsPalette());

  // Selawik stands in for Segoe UI, the default font of MeOS.
  initFontResources();
  for (const char *file : {":/meos/fonts/selawk.ttf", ":/meos/fonts/selawkb.ttf", ":/meos/fonts/selawkl.ttf"})
    QFontDatabase::addApplicationFont(QString::fromLatin1(file));
  return app;
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
    if (meos_qt::takePostedMessage(*msg, window, filterMin, filterMax)) {
      // WH_GETMESSAGE sees every message this call returns, and may change it.
      if (meos_qt::hasHooks(WH_GETMESSAGE))
        meos_qt::callHooks(WH_GETMESSAGE, HC_ACTION, PM_REMOVE, reinterpret_cast<LPARAM>(msg));
      return TRUE;
    }
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

/* ---------------------------------------------------------------------
   Accelerators. The table comes from the ACCELERATORS block of meos.rc, which
   cmake/Win32Resources.cmake embeds as a text resource of type 9 (RT_ACCELERATOR),
   one entry "<flags> <key> <command>" per line.

   Keyboard input does not pass the message queue in this layer: the canvas calls
   the window procedure directly (win32_canvas.cpp), and TranslateMessage does
   nothing. A key message therefore reaches TranslateAccelerator only if the
   application puts one there itself. MeOS calls it in its main message loop, where
   no key message arrives.
   --------------------------------------------------------------------- */

namespace {

struct AcceleratorEntry {
  WORD flags;
  WORD key;
  WORD command;
};

std::map<HACCEL, std::vector<AcceleratorEntry>> acceleratorTables;
std::uintptr_t lastAcceleratorTable = 0;

bool keyDown(int virtualKey) {
  return (GetKeyState(virtualKey) & 0x8000) != 0;
}

} // namespace

HACCEL LoadAccelerators(HINSTANCE instance, LPCWSTR tableName) {
  const HRSRC resource = FindResource(instance, tableName, MAKEINTRESOURCE(9));
  const HGLOBAL data = resource ? LoadResource(instance, resource) : nullptr;
  const void *table = data ? LockResource(data) : nullptr;
  if (!table)
    return nullptr;

  std::vector<AcceleratorEntry> entries;
  std::istringstream text(std::string(static_cast<const char *>(table), SizeofResource(instance, resource)));
  unsigned flags = 0;
  unsigned key = 0;
  unsigned command = 0;
  while (text >> flags >> key >> command)
    entries.push_back(AcceleratorEntry{WORD(flags), WORD(key), WORD(command)});
  if (entries.empty()) {
    SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return nullptr;
  }
  const auto handle = reinterpret_cast<HACCEL>(++lastAcceleratorTable);
  acceleratorTables[handle] = std::move(entries);
  return handle;
}

int TranslateAccelerator(HWND window, HACCEL table, LPMSG msg) {
  const auto entries = acceleratorTables.find(table);
  if (!window || !msg || entries == acceleratorTables.end())
    return 0;

  const bool keyMessage = msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN;
  const bool charMessage = msg->message == WM_CHAR || msg->message == WM_SYSCHAR;
  const bool systemMessage = msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSCHAR;
  if (!keyMessage && !charMessage)
    return 0;

  for (const AcceleratorEntry &entry : entries->second) {
    const bool virtualKey = (entry.flags & FVIRTKEY) != 0;
    if (virtualKey != keyMessage || WORD(msg->wParam) != entry.key)
      continue;
    if (virtualKey) {
      if (keyDown(VK_SHIFT) != ((entry.flags & FSHIFT) != 0) ||
          keyDown(VK_CONTROL) != ((entry.flags & FCONTROL) != 0) ||
          keyDown(VK_MENU) != ((entry.flags & FALT) != 0))
        continue;
    }
    // A character with Alt arrives as a system message, as on Windows.
    else if (systemMessage != ((entry.flags & FALT) != 0)) {
      continue;
    }
    SendMessage(window, WM_COMMAND, MAKEWPARAM(entry.command, 1), 0);
    return 1;
  }
  return 0;
}

void PostQuitMessage(int exitCode) {
  quitPosted = true;
  quitExitCode = exitCode;
  // Wakes a GetMessage that waits for Qt events.
  if (QCoreApplication *app = QCoreApplication::instance())
    QMetaObject::invokeMethod(app, [] {}, Qt::QueuedConnection);
}
