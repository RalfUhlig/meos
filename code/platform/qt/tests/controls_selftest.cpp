/************************************************************************
    MeOS - Orienteering Software
    Linux port: self test of the standard and common controls on Qt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Checks BUTTON, EDIT, COMBOBOX, LISTBOX, STATIC, tooltips, the toolbar and the
// tab control of
// code/platform/qt against the Windows behaviour MeOS relies on: which changes
// notify the parent and in which order, focus, item data, multiple selection,
// tab stops, subclassing, capture and cursors. User input is simulated with Qt
// events. Runs with QT_QPA_PLATFORM=offscreen under ctest.

#include "platform/qt/win32_gdi.h"

#include <windowsx.h>

#include <QAbstractButton>
#include <QComboBox>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTabBar>
#include <QThread>
#include <QToolBar>
#include <QToolTip>
#include <QWheelEvent>

#include <chrono>
#include <cstdio>

#include "commctrl.h"

namespace {

int failures = 0;

#define CHECK(expr)                                                                   \
  do {                                                                                \
    if (!(expr)) {                                                                    \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr);   \
      failures++;                                                                     \
    }                                                                                 \
  } while (false)

const wchar_t *const parentClass = L"ControlsTestParent";

struct Event {
  HWND window;
  UINT message;
  WPARAM wParam;
  LPARAM lParam;
  HWND focus;  // GetFocus() when the message arrived
  LRESULT check; // BM_GETCHECK of the sender of WM_COMMAND
};
std::vector<Event> events;
// Runs for every message the parent receives, after it has been recorded.
std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)> parentHandler;

LRESULT CALLBACK parentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  Event e{window, message, wParam, lParam, GetFocus(), 0};
  if (message == WM_COMMAND)
    e.check = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
  events.push_back(e);
  if (parentHandler) {
    const LRESULT result = parentHandler(window, message, wParam, lParam);
    if (result)
      return result;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

struct Notification {
  HWND control;
  int id;
  int code;
  bool operator==(const Notification &other) const {
    return control == other.control && id == other.id && code == other.code;
  }
};

std::vector<Notification> notifications() {
  std::vector<Notification> result;
  for (const Event &e : events) {
    if (e.message == WM_COMMAND)
      result.push_back(Notification{reinterpret_cast<HWND>(e.lParam), LOWORD(e.wParam), HIWORD(e.wParam)});
  }
  return result;
}

int countOf(HWND window, UINT message) {
  return int(std::count_if(events.begin(), events.end(),
                           [&](const Event &e) { return e.window == window && e.message == message; }));
}

const Event *lastOf(HWND window, UINT message) {
  for (auto it = events.rbegin(); it != events.rend(); ++it) {
    if (it->window == window && it->message == message)
      return &*it;
  }
  return nullptr;
}

HWND createParent(int width = 600, int height = 400) {
  return CreateWindowEx(0, parentClass, L"Controls", WS_POPUP | WS_VISIBLE, 50, 50, width, height, nullptr,
                        nullptr, meos_qt::applicationInstance(), nullptr);
}

HWND createControl(HWND parent, const wchar_t *className, DWORD style, int x, int y, int width, int height, int id,
                   const wchar_t *text = L"", DWORD exStyle = 0) {
  const HWND control = CreateWindowEx(exStyle, className, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
                                      parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      meos_qt::applicationInstance(), nullptr);
  SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), 0);
  return control;
}

template <typename T>
T *widgetOf(HWND window) {
  const std::shared_ptr<meos_qt::Window> target = meos_qt::findWindow(window);
  return target ? qobject_cast<T *>(target->client.data()) : nullptr;
}

QWidget *clientOf(HWND window) {
  const std::shared_ptr<meos_qt::Window> target = meos_qt::findWindow(window);
  return target ? target->client.data() : nullptr;
}

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

bool activate(HWND top) {
  const std::shared_ptr<meos_qt::Window> window = meos_qt::findWindow(top);
  window->frame->activateWindow();
  return waitFor([&] { return window->frame->isActiveWindow(); });
}

void sendKey(QWidget *widget, int key, const QString &text = QString(), Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
  QCoreApplication::sendEvent(widget, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
  QCoreApplication::sendEvent(widget, &release);
}

void sendMouse(QWidget *widget, QEvent::Type type, QPoint position, Qt::MouseButton button = Qt::LeftButton) {
  const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease || type == QEvent::MouseMove ? Qt::NoButton
                                                                                                 : Qt::MouseButtons(button);
  QMouseEvent event(type, QPointF(position), QPointF(widget->mapToGlobal(position)),
                    type == QEvent::MouseMove ? Qt::NoButton : button, buttons, Qt::NoModifier);
  QCoreApplication::sendEvent(widget, &event);
}

void click(QWidget *widget, QPoint position) {
  sendMouse(widget, QEvent::MouseButtonPress, position);
  sendMouse(widget, QEvent::MouseButtonRelease, position);
}

void sendWheel(QWidget *widget, int delta) {
  const QPoint position = widget->rect().center();
  QWheelEvent wheel(QPointF(position), QPointF(widget->mapToGlobal(position)), QPoint(), QPoint(0, delta),
                    Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(widget, &wheel);
}

std::wstring windowText(HWND window) {
  wchar_t buffer[256];
  GetWindowText(window, buffer, 256);
  return buffer;
}

/* ------------------------------------------------------------------ */

void testButtons() {
  HWND parent = createParent();
  HWND push = createControl(parent, L"BUTTON", BS_PUSHBUTTON | BS_NOTIFY, 10, 10, 80, 24, 101, L"Push");
  HWND autoCheck = createControl(parent, L"button", BS_AUTOCHECKBOX, 10, 40, 16, 16, 102);
  HWND pushLike = createControl(parent, L"Button", BS_CHECKBOX | BS_PUSHLIKE | BS_NOTIFY, 10, 70, 80, 24, 103, L"State");
  CHECK(push && autoCheck && pushLike);
  CHECK(QApplication::style()->name().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) == 0);

  CHECK(windowText(push) == L"Push" && widgetOf<QAbstractButton>(push)->text() == QStringLiteral("Push"));
  SetWindowText(push, L"Other");
  CHECK(windowText(push) == L"Other" && widgetOf<QAbstractButton>(push)->text() == QStringLiteral("Other"));
  CHECK(SendMessage(push, WM_GETFONT, 0, 0) == reinterpret_cast<LRESULT>(GetStockObject(DEFAULT_GUI_FONT)));

  // Messages change the state without notifications; push buttons have none.
  events.clear();
  SendMessage(push, BM_SETCHECK, BST_CHECKED, 0);
  CHECK(SendMessage(push, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
  SendMessage(autoCheck, BM_SETCHECK, BST_CHECKED, 0);
  CHECK(SendMessage(autoCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
  SendMessage(pushLike, BM_SETCHECK, BST_CHECKED, 0);
  CHECK(SendMessage(pushLike, BM_GETCHECK, 0, 0) == BST_CHECKED);
  CHECK(notifications().empty());

  // A click on BS_AUTOCHECKBOX toggles before BN_CLICKED; BS_CHECKBOX does not toggle.
  widgetOf<QAbstractButton>(autoCheck)->click();
  CHECK((notifications() == std::vector<Notification>{{autoCheck, 102, BN_CLICKED}}));
  CHECK(events.back().check == BST_UNCHECKED && SendMessage(autoCheck, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
  events.clear();
  widgetOf<QAbstractButton>(pushLike)->click();
  CHECK((notifications() == std::vector<Notification>{{pushLike, 103, BN_CLICKED}}));
  CHECK(SendMessage(pushLike, BM_GETCHECK, 0, 0) == BST_CHECKED);

  // Mouse click on a push button; the space bar clicks as well.
  events.clear();
  QWidget *pushWidget = widgetOf<QAbstractButton>(push);
  click(pushWidget, QPoint(5, 5));
  CHECK((notifications() == std::vector<Notification>{{push, 101, BN_CLICKED}}));
  events.clear();
  sendKey(pushWidget, Qt::Key_Space, QStringLiteral(" "));
  CHECK((notifications() == std::vector<Notification>{{push, 101, BN_CLICKED}}));
  // Keys are not passed on to the parent window.
  events.clear();
  sendKey(pushWidget, Qt::Key_Down);
  CHECK(countOf(parent, WM_KEYDOWN) == 0);

  // BS_BITMAP shows the image only.
  HWND image = createControl(parent, L"BUTTON", BS_PUSHBUTTON | BS_BITMAP, 100, 10, 40, 40, 104, L"...");
  CHECK(widgetOf<QAbstractButton>(image)->text().isEmpty() && windowText(image) == L"...");
  HDC screen = GetDC(nullptr);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, 20, 20);
  ReleaseDC(nullptr, screen);
  CHECK(SendMessage(image, BM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(bitmap)) == 0);
  CHECK(SendMessage(image, BM_GETIMAGE, IMAGE_BITMAP, 0) == reinterpret_cast<LRESULT>(bitmap));
  CHECK(!widgetOf<QAbstractButton>(image)->icon().isNull());
  SIZE ideal = {0, 0};
  CHECK(Button_GetIdealSize(push, &ideal) && ideal.cx > 0 && ideal.cy > 0);

  // MeOS destroys buttons from their own click callback.
  parentHandler = [&](HWND, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == 104)
      CHECK(DestroyWindow(reinterpret_cast<HWND>(lParam)));
    return 0;
  };
  QPointer<QAbstractButton> imageWidget = widgetOf<QAbstractButton>(image);
  imageWidget->click();
  CHECK(!meos_qt::findWindow(image) && imageWidget);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  CHECK(!imageWidget);
  parentHandler = nullptr;

  DeleteObject(bitmap);
  CHECK(DestroyWindow(parent));
}

/* ------------------------------------------------------------------ */

void testEdit() {
  HWND parent = createParent();
  HWND edit = createControl(parent, L"EDIT", ES_AUTOHSCROLL | WS_BORDER, 10, 10, 150, 22, 201, L"start",
                            WS_EX_CLIENTEDGE);
  HWND box = createControl(parent, L"EDIT", ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 10, 40, 150, 80, 202);
  QLineEdit *line = widgetOf<QLineEdit>(edit);
  QPlainTextEdit *multi = widgetOf<QPlainTextEdit>(box);
  CHECK(line && multi && line->text() == QStringLiteral("start"));

  // The window rectangle includes the border; the client area is inside.
  RECT rect;
  GetClientRect(edit, &rect);
  CHECK(rect.right == 150 - 6 && rect.bottom == 22 - 6);

  // WM_SETTEXT sends EN_CHANGE synchronously for single-line edits only, even
  // for the same text; the caret goes to the start.
  events.clear();
  CHECK(SetWindowText(edit, L"Hello"));
  CHECK((notifications() == std::vector<Notification>{{edit, 201, EN_CHANGE}}));
  CHECK(line->cursorPosition() == 0 && windowText(edit) == L"Hello");
  SetWindowText(edit, L"Hello");
  CHECK(notifications().size() == 2);
  events.clear();
  SetWindowText(box, L"one\r\ntwo");
  CHECK(notifications().empty());
  CHECK(multi->toPlainText() == QStringLiteral("one\ntwo") && windowText(box) == L"one\r\ntwo");
  CHECK(GetWindowTextLength(box) == 8);

  // Typing notifies; keys do not reach the parent.
  events.clear();
  line->setCursorPosition(5);
  sendKey(line, Qt::Key_Exclam, QStringLiteral("!"));
  CHECK(windowText(edit) == L"Hello!");
  CHECK((notifications() == std::vector<Notification>{{edit, 201, EN_CHANGE}}));
  sendKey(line, Qt::Key_Up);
  sendKey(line, Qt::Key_Return, QStringLiteral("\r"));
  CHECK(countOf(parent, WM_KEYDOWN) == 0);

  // Selection: end -1 means the end, the caret goes to the end position, start
  // -1 removes the selection.
  SendMessage(edit, EM_SETSEL, 0, -1);
  CHECK(line->selectedText() == QStringLiteral("Hello!"));
  DWORD start = 99, end = 99;
  SendMessage(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
  CHECK(start == 0 && end == 6);
  SendMessage(edit, EM_SETSEL, 6, 0);
  CHECK(line->selectedText() == QStringLiteral("Hello!") && line->cursorPosition() == 0);
  SendMessage(edit, EM_SETSEL, WPARAM(-1), 0);
  CHECK(!line->hasSelectedText());
  SendMessage(edit, EM_SETSEL, 1, 3);
  events.clear();
  SendMessage(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"ipp"));
  CHECK(windowText(edit) == L"Hipplo!");
  CHECK((notifications() == std::vector<Notification>{{edit, 201, EN_CHANGE}}));

  SendMessage(edit, EM_LIMITTEXT, 7, 0);
  line->setCursorPosition(7);
  sendKey(line, Qt::Key_X, QStringLiteral("x"));
  CHECK(windowText(edit) == L"Hipplo!");

  SendMessage(edit, EM_SETPASSWORDCHAR, 183, 0);
  CHECK(line->echoMode() == QLineEdit::Password && (GetWindowLong(edit, GWL_STYLE) & ES_PASSWORD));
  CHECK(line->style()->styleHint(QStyle::SH_LineEdit_PasswordCharacter, nullptr, line) == 183);
  SendMessage(edit, EM_SETPASSWORDCHAR, 0, 0);
  CHECK(line->echoMode() == QLineEdit::Normal);

  // WM_CTLCOLOREDIT: the parent's colours are asked for when the edit paints.
  parentHandler = [&](HWND, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
    if (message == WM_CTLCOLOREDIT && reinterpret_cast<HWND>(lParam) == edit) {
      SetDCBrushColor(reinterpret_cast<HDC>(wParam), RGB(255, 200, 200));
      SetTextColor(reinterpret_cast<HDC>(wParam), RGB(0, 0, 128));
      return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    return 0;
  };
  events.clear();
  InvalidateRect(edit, nullptr, TRUE);
  waitFor([&] { return countOf(parent, WM_CTLCOLOREDIT) > 0; });
  line->repaint();
  CHECK(countOf(parent, WM_CTLCOLOREDIT) > 0);
  CHECK(line->palette().color(QPalette::Active, QPalette::Base) == QColor(255, 200, 200));
  CHECK(line->palette().color(QPalette::Active, QPalette::Text) == QColor(0, 0, 128));
  parentHandler = nullptr;
  line->repaint();
  CHECK(line->palette().color(QPalette::Active, QPalette::Base) == QColor(255, 255, 255));

  events.clear();
  SendMessage(box, EM_SETSEL, 0, -1);
  SendMessage(box, WM_PASTE, 0, 0);
  SendMessage(box, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(L"a\r\nb"));
  CHECK(windowText(box) == L"a\r\nb");
  CHECK(!notifications().empty() && notifications().back() == (Notification{box, 202, EN_CHANGE}));

  CHECK(DestroyWindow(parent));
}

/* ------------------------------------------------------------------ */

void testFocus() {
  HWND parent = createParent();
  CHECK(activate(parent));
  HWND edit1 = createControl(parent, L"EDIT", ES_AUTOHSCROLL, 10, 10, 100, 20, 301);
  HWND edit2 = createControl(parent, L"EDIT", ES_AUTOHSCROLL, 10, 40, 100, 20, 302);
  HWND quiet = createControl(parent, L"BUTTON", BS_PUSHBUTTON, 10, 70, 80, 24, 303, L"Quiet");
  HWND notify = createControl(parent, L"BUTTON", BS_PUSHBUTTON | BS_NOTIFY, 10, 100, 80, 24, 304, L"Notify");
  HWND list = createControl(parent, L"LISTBOX", 0, 200, 10, 100, 100, 305);
  HWND combo = createControl(parent, L"COMBOBOX", CBS_DROPDOWNLIST, 200, 120, 100, 100, 306);

  events.clear();
  CHECK(SetFocus(edit1) == nullptr && GetFocus() == edit1);
  CHECK((notifications() == std::vector<Notification>{{edit1, 301, EN_SETFOCUS}}));
  CHECK(QApplication::focusWidget() == widgetOf<QLineEdit>(edit1));

  // The previous window loses the focus first; the focus has moved already.
  events.clear();
  CHECK(SetFocus(edit2) == edit1);
  CHECK((notifications() == std::vector<Notification>{{edit1, 301, EN_KILLFOCUS}, {edit2, 302, EN_SETFOCUS}}));
  CHECK(!events.empty() && events.front().focus == edit2);
  CHECK(SetFocus(edit2) == edit2);

  // Buttons notify only with BS_NOTIFY; list and combo boxes always.
  events.clear();
  SetFocus(quiet);
  SetFocus(notify);
  SetFocus(list);
  SetFocus(combo);
  SetFocus(parent);
  CHECK((notifications() == std::vector<Notification>{{edit2, 302, EN_KILLFOCUS},
                                                      {notify, 304, BN_SETFOCUS},
                                                      {notify, 304, BN_KILLFOCUS},
                                                      {list, 305, LBN_SETFOCUS},
                                                      {list, 305, LBN_KILLFOCUS},
                                                      {combo, 306, CBN_SETFOCUS},
                                                      {combo, 306, CBN_KILLFOCUS}}));
  CHECK(countOf(parent, WM_SETFOCUS) == 1 && lastOf(parent, WM_SETFOCUS)->wParam == reinterpret_cast<WPARAM>(combo));

  // A handler of the kill notification that moves the focus elsewhere prevents
  // the WM_SETFOCUS of the original target. The focus had already moved to it, so
  // it gets WM_KILLFOCUS without WM_SETFOCUS (as in Wine's NtUserSetFocus).
  SetFocus(edit1);
  parentHandler = [&](HWND, UINT message, WPARAM wParam, LPARAM) -> LRESULT {
    if (message == WM_COMMAND && HIWORD(wParam) == EN_KILLFOCUS && LOWORD(wParam) == 301)
      SetFocus(list);
    return 0;
  };
  events.clear();
  SetFocus(edit2);
  parentHandler = nullptr;
  CHECK(GetFocus() == list);
  CHECK((notifications() == std::vector<Notification>{
             {edit1, 301, EN_KILLFOCUS}, {edit2, 302, EN_KILLFOCUS}, {list, 305, LBN_SETFOCUS}}));

  // Hiding and destroying a focused child moves the focus to the parent,
  // disabling removes it. A disabled window cannot get the focus.
  SetFocus(edit1);
  events.clear();
  ShowWindow(edit1, SW_HIDE);
  CHECK(GetFocus() == parent && (notifications() == std::vector<Notification>{{edit1, 301, EN_KILLFOCUS}}));
  ShowWindow(edit1, SW_SHOW);
  SetFocus(edit2);
  events.clear();
  EnableWindow(edit2, FALSE);
  CHECK(GetFocus() == nullptr && (notifications() == std::vector<Notification>{{edit2, 302, EN_KILLFOCUS}}));
  CHECK(SetFocus(edit2) == nullptr && GetFocus() == nullptr);
  EnableWindow(edit2, TRUE);
  SetFocus(edit1);
  events.clear();
  CHECK(DestroyWindow(edit1));
  CHECK(GetFocus() == parent && (notifications() == std::vector<Notification>{{edit1, 301, EN_KILLFOCUS}}));

  // Focus changes by Qt (a click) become the same messages.
  SetFocus(edit2);
  CHECK(QApplication::focusWidget() == widgetOf<QLineEdit>(edit2));
  events.clear();
  widgetOf<QListWidget>(list)->setFocus(Qt::MouseFocusReason);
  CHECK(GetFocus() == list);
  CHECK((notifications() == std::vector<Notification>{{edit2, 302, EN_KILLFOCUS}, {list, 305, LBN_SETFOCUS}}));

  // Tab does not move the focus without the MeOS keyboard hook.
  sendKey(widgetOf<QListWidget>(list), Qt::Key_Tab, QStringLiteral("\t"));
  CHECK(GetFocus() == list && QApplication::focusWidget() == widgetOf<QListWidget>(list));

  // Deactivating the application removes the focus, reactivating restores it.
  events.clear();
  QWidget *other = new QWidget(nullptr);
  other->show();
  other->activateWindow();
  CHECK(waitFor([&] { return other->isActiveWindow(); }));
  CHECK(GetFocus() == nullptr);
  CHECK((notifications() == std::vector<Notification>{{list, 305, LBN_KILLFOCUS}}));
  CHECK(activate(parent));
  CHECK(GetFocus() == list);
  delete other;

  CHECK(DestroyWindow(parent));
  CHECK(GetFocus() == nullptr);
}

/* ------------------------------------------------------------------ */

void testComboBox() {
  HWND parent = createParent();
  HWND list = createControl(parent, L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL | WS_BORDER, 10, 10, 150, 200, 401,
                            L"", WS_EX_CLIENTEDGE);
  QComboBox *combo = widgetOf<QComboBox>(list);
  CHECK(combo && !combo->isEditable());

  // The window keeps the height of the selection field: text height + 8.
  HDC dc = GetDC(nullptr);
  SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
  SIZE cell;
  GetTextExtentPoint32(dc, L"M", 1, &cell); // cy is tmHeight
  ReleaseDC(nullptr, dc);
  RECT rect;
  GetWindowRect(list, &rect);
  CHECK(rect.bottom - rect.top == cell.cy + 8 + 6);
  SetWindowPos(list, nullptr, 0, 0, 170, 300, SWP_NOMOVE | SWP_NOZORDER);
  GetWindowRect(list, &rect);
  CHECK(rect.right - rect.left == 170 && rect.bottom - rect.top == cell.cy + 8 + 6);

  events.clear();
  CHECK(SendMessage(list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Alpha")) == 0);
  CHECK(SendMessage(list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Beta")) == 1);
  CHECK(SendMessage(list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Gamma")) == 2);
  // Unlike QComboBox, the first item is not selected.
  CHECK(SendMessage(list, CB_GETCURSEL, 0, 0) == CB_ERR && SendMessage(list, CB_GETCOUNT, 0, 0) == 3);
  for (int i = 0; i < 3; i++)
    CHECK(SendMessage(list, CB_SETITEMDATA, i, 1000 + i) == TRUE);
  CHECK(SendMessage(list, CB_SETITEMDATA, 3, 0) == CB_ERR && SendMessage(list, CB_GETITEMDATA, 7, 0) == CB_ERR);
  CHECK(SendMessage(list, CB_GETITEMDATA, 2, 0) == 1002);

  CHECK(SendMessage(list, CB_SETCURSEL, 1, 0) == 1 && SendMessage(list, CB_GETCURSEL, 0, 0) == 1);
  CHECK(windowText(list) == L"Beta" && GetWindowTextLength(list) == 4);
  CHECK(SendMessage(list, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"x")) == CB_ERR);
  wchar_t buffer[32];
  CHECK(SendMessage(list, CB_GETLBTEXT, 2, reinterpret_cast<LPARAM>(buffer)) == 5 && std::wstring(buffer) == L"Gamma");
  CHECK(SendMessage(list, CB_GETLBTEXTLEN, 0, 0) == 5 && SendMessage(list, CB_GETLBTEXT, 3, 0) == CB_ERR);
  // Case-insensitive prefix search after the start index, wrapping around.
  CHECK(SendMessage(list, CB_FINDSTRING, WPARAM(-1), reinterpret_cast<LPARAM>(L"gam")) == 2);
  CHECK(SendMessage(list, CB_FINDSTRING, 2, reinterpret_cast<LPARAM>(L"A")) == 0);
  CHECK(SendMessage(list, CB_FINDSTRING, WPARAM(-1), reinterpret_cast<LPARAM>(L"Delta")) == CB_ERR);

  // Inserting and deleting keep the selected item.
  CHECK(SendMessage(list, CB_INSERTSTRING, 0, reinterpret_cast<LPARAM>(L"First")) == 0);
  CHECK(SendMessage(list, CB_GETCURSEL, 0, 0) == 2 && SendMessage(list, CB_GETITEMDATA, 2, 0) == 1001);
  CHECK(SendMessage(list, CB_INSERTSTRING, 9, reinterpret_cast<LPARAM>(L"x")) == CB_ERR);
  CHECK(SendMessage(list, CB_DELETESTRING, 0, 0) == 3 && SendMessage(list, CB_GETCURSEL, 0, 0) == 1);
  CHECK(SendMessage(list, CB_DELETESTRING, 1, 0) == 2 && SendMessage(list, CB_GETCURSEL, 0, 0) == CB_ERR);
  CHECK(windowText(list).empty());
  CHECK(SendMessage(list, CB_SETCURSEL, 5, 0) == CB_ERR);
  CHECK(notifications().empty());

  // The user selects with the arrow keys and the wheel: CBN_SELCHANGE.
  SendMessage(list, CB_SETCURSEL, 0, 0);
  sendKey(combo, Qt::Key_Down);
  CHECK(SendMessage(list, CB_GETCURSEL, 0, 0) == 1);
  CHECK((notifications() == std::vector<Notification>{{list, 401, CBN_SELCHANGE}}));
  sendWheel(combo, 120);
  CHECK(SendMessage(list, CB_GETCURSEL, 0, 0) == 0 && notifications().size() == 2);
  // A selection by the user of the current item notifies as well.
  events.clear();
  combo->activated(0);
  CHECK((notifications() == std::vector<Notification>{{list, 401, CBN_SELCHANGE}}));

  CHECK(SendMessage(list, CB_RESETCONTENT, 0, 0) == 0 && SendMessage(list, CB_GETCOUNT, 0, 0) == 0);

  // Editable combo box.
  HWND dropDown = createControl(parent, L"COMBOBOX", CBS_DROPDOWN | CBS_AUTOHSCROLL, 10, 60, 150, 200, 402);
  QComboBox *editable = widgetOf<QComboBox>(dropDown);
  CHECK(editable && editable->isEditable());
  events.clear();
  SetWindowText(dropDown, L"typed");
  CHECK(SendMessage(dropDown, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Oslo")) == 0);
  SendMessage(dropDown, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Umeå"));
  // Adding items keeps the text and selects nothing.
  CHECK(windowText(dropDown) == L"typed" && SendMessage(dropDown, CB_GETCURSEL, 0, 0) == CB_ERR);
  CHECK(notifications().empty());

  SendMessage(dropDown, CB_SETCURSEL, 1, 0);
  CHECK(windowText(dropDown) == L"Umeå" && notifications().empty());
  // WM_SETTEXT changes the text without notification and keeps the selection.
  SetWindowText(dropDown, L"Ume");
  CHECK(SendMessage(dropDown, CB_GETCURSEL, 0, 0) == 1 && notifications().empty());

  // Typing notifies CBN_EDITCHANGE and removes the selection of the list.
  editable->lineEdit()->setCursorPosition(3);
  sendKey(editable, Qt::Key_A, QStringLiteral("a"));
  CHECK(windowText(dropDown) == L"Umea");
  CHECK((notifications() == std::vector<Notification>{{dropDown, 402, CBN_EDITCHANGE}}));
  CHECK(SendMessage(dropDown, CB_GETCURSEL, 0, 0) == CB_ERR);

  // Enter in a closed combo box selects nothing, even for a matching text.
  SetWindowText(dropDown, L"Oslo");
  events.clear();
  sendKey(editable, Qt::Key_Return, QStringLiteral("\r"));
  CHECK(notifications().empty() && SendMessage(dropDown, CB_GETCURSEL, 0, 0) == CB_ERR);
  CHECK(countOf(parent, WM_KEYDOWN) == 0 && editable->count() == 2);

  // Selecting an item shows it in the edit field.
  events.clear();
  editable->setCurrentIndex(1);
  editable->activated(1);
  CHECK(windowText(dropDown) == L"Umeå" && SendMessage(dropDown, CB_GETCURSEL, 0, 0) == 1);
  CHECK((notifications() == std::vector<Notification>{{dropDown, 402, CBN_SELCHANGE}}));

  SendMessage(dropDown, CB_RESETCONTENT, 0, 0);
  CHECK(windowText(dropDown).empty() && SendMessage(dropDown, CB_GETCURSEL, 0, 0) == CB_ERR);

  CHECK(DestroyWindow(parent));
}

/* ------------------------------------------------------------------ */

WNDPROC originalListProc = nullptr;
std::vector<HWND> syncedLists;
bool swallowKeys = false;

// As gdioutput's GetMsgProc: scrolling one list scrolls the other.
LRESULT CALLBACK syncListProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (swallowKeys && message == WM_KEYDOWN)
    return 0;
  const LRESULT result = CallWindowProc(originalListProc, window, message, wParam, lParam);
  if (message == WM_VSCROLL || message == WM_MOUSEWHEEL || message == WM_KEYDOWN) {
    const LRESULT top = CallWindowProc(originalListProc, window, LB_GETTOPINDEX, 0, 0);
    for (HWND other : syncedLists) {
      if (other != window)
        CallWindowProc(originalListProc, other, LB_SETTOPINDEX, WPARAM(top), 0);
    }
  }
  return result;
}

QPoint itemCenter(QListWidget *list, int row) {
  return list->visualItemRect(list->item(row)).center();
}

void testListBox() {
  HWND parent = createParent();
  DWORD style = WS_BORDER | LBS_USETABSTOPS | LBS_NOTIFY | WS_VSCROLL;
  HWND single = createControl(parent, L"LISTBOX", style, 10, 10, 200, 150, 501, L"", WS_EX_CLIENTEDGE);
  QListWidget *list = widgetOf<QListWidget>(single);
  CHECK(list);

  events.clear();
  CHECK(SendMessage(single, LB_INSERTSTRING, WPARAM(-1), reinterpret_cast<LPARAM>(L"Anna")) == 0);
  CHECK(SendMessage(single, LB_INSERTSTRING, WPARAM(-1), reinterpret_cast<LPARAM>(L"Bertil\tB")) == 1);
  CHECK(SendMessage(single, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cecilia")) == 2);
  CHECK(SendMessage(single, LB_INSERTSTRING, 1, reinterpret_cast<LPARAM>(L"Anders")) == 1);
  CHECK(SendMessage(single, LB_INSERTSTRING, 10, reinterpret_cast<LPARAM>(L"x")) == LB_ERR);
  CHECK(SendMessage(single, LB_GETCOUNT, 0, 0) == 4 && SendMessage(single, LB_GETCURSEL, 0, 0) == LB_ERR);
  for (int i = 0; i < 4; i++)
    SendMessage(single, LB_SETITEMDATA, i, 10 * i);
  CHECK(SendMessage(single, LB_GETITEMDATA, 3, 0) == 30 && SendMessage(single, LB_GETITEMDATA, 4, 0) == LB_ERR);
  wchar_t buffer[32];
  CHECK(SendMessage(single, LB_GETTEXT, 2, reinterpret_cast<LPARAM>(buffer)) == 8 && std::wstring(buffer) == L"Bertil\tB");
  CHECK(SendMessage(single, LB_GETTEXTLEN, 1, 0) == 6);
  CHECK(SendMessage(single, LB_FINDSTRING, 0, reinterpret_cast<LPARAM>(L"an")) == 1);
  CHECK(SendMessage(single, LB_FINDSTRING, 1, reinterpret_cast<LPARAM>(L"an")) == 0);

  CHECK(SendMessage(single, LB_SETCURSEL, 2, 0) == 2 && SendMessage(single, LB_GETCURSEL, 0, 0) == 2);
  CHECK(SendMessage(single, LB_SETSEL, TRUE, 0) == LB_ERR);
  CHECK(SendMessage(single, LB_DELETESTRING, 0, 0) == 3 && SendMessage(single, LB_GETCURSEL, 0, 0) == 1);
  CHECK(SendMessage(single, LB_DELETESTRING, 1, 0) == 2 && SendMessage(single, LB_GETCURSEL, 0, 0) == LB_ERR);
  CHECK(SendMessage(single, LB_SETCURSEL, WPARAM(-1), 0) == LB_ERR);
  CHECK(notifications().empty());

  // A click notifies on release, also for the item already selected; a double
  // click adds LBN_DBLCLK.
  SendMessage(single, LB_RESETCONTENT, 0, 0);
  for (const wchar_t *name : {L"Anna", L"Bertil\tB", L"Cecilia"})
    SendMessage(single, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
  list->doItemsLayout();
  QWidget *viewport = list->viewport();
  events.clear();
  sendMouse(viewport, QEvent::MouseButtonPress, itemCenter(list, 1));
  CHECK(notifications().empty());
  sendMouse(viewport, QEvent::MouseButtonRelease, itemCenter(list, 1));
  CHECK((notifications() == std::vector<Notification>{{single, 501, LBN_SELCHANGE}}));
  CHECK(SendMessage(single, LB_GETCURSEL, 0, 0) == 1);
  click(viewport, itemCenter(list, 1));
  CHECK(notifications().size() == 2);
  events.clear();
  sendMouse(viewport, QEvent::MouseButtonPress, itemCenter(list, 2));
  sendMouse(viewport, QEvent::MouseButtonRelease, itemCenter(list, 2));
  sendMouse(viewport, QEvent::MouseButtonDblClick, itemCenter(list, 2));
  sendMouse(viewport, QEvent::MouseButtonRelease, itemCenter(list, 2));
  CHECK((notifications() == std::vector<Notification>{{single, 501, LBN_SELCHANGE}, {single, 501, LBN_DBLCLK}}));

  // Arrow keys move the selection and notify.
  events.clear();
  sendKey(list, Qt::Key_Up);
  CHECK(SendMessage(single, LB_GETCURSEL, 0, 0) == 1);
  CHECK((notifications() == std::vector<Notification>{{single, 501, LBN_SELCHANGE}}));
  CHECK(countOf(parent, WM_KEYDOWN) == 0);

  // Without LBS_NOTIFY there are no selection notifications.
  HWND silent = createControl(parent, L"LISTBOX", WS_VSCROLL, 220, 10, 100, 100, 502);
  SendMessage(silent, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"one"));
  QListWidget *silentList = widgetOf<QListWidget>(silent);
  silentList->doItemsLayout();
  events.clear();
  click(silentList->viewport(), itemCenter(silentList, 0));
  CHECK(notifications().empty() && SendMessage(silent, LB_GETCURSEL, 0, 0) == 0);

  // Tab stops in dialog units: a quarter of the average character width.
  HDC dc = GetDC(nullptr);
  SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
  SIZE alphabet;
  GetTextExtentPoint32(dc, L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", 52, &alphabet);
  ReleaseDC(nullptr, dc);
  const int dialogWidth = (alphabet.cx / 26 + 1) / 2;
  int stops[] = {100};
  CHECK(SendMessage(single, LB_SETTABSTOPS, 1, reinterpret_cast<LPARAM>(stops)) == TRUE);
  const int stopX = (100 * dialogWidth + 2) / 4;
  CHECK(list->visualItemRect(list->item(0)).height() == alphabet.cy);
  SendMessage(single, LB_SETCURSEL, WPARAM(-1), 0);
  const QImage image = list->viewport()->grab().toImage();
  const QRect row = list->visualItemRect(list->item(1));
  int firstInkAfterGap = -1;
  for (int x = 45; x < row.right() && firstInkAfterGap < 0; x++) {
    for (int y = row.top(); y <= row.bottom(); y++) {
      if (qGray(image.pixel(x, y)) < 128) {
        firstInkAfterGap = x;
        break;
      }
    }
  }
  CHECK(firstInkAfterGap >= stopX && firstInkAfterGap <= stopX + 3);
  CHECK(SendMessage(silent, LB_SETTABSTOPS, 1, reinterpret_cast<LPARAM>(stops)) == FALSE);

  // Multiple selection.
  HWND multi = createControl(parent, L"LISTBOX", LBS_MULTIPLESEL | LBS_NOTIFY | WS_VSCROLL, 340, 10, 100, 100, 503);
  for (const wchar_t *name : {L"a", L"b", L"c"})
    SendMessage(multi, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
  CHECK(SendMessage(multi, LB_SETCURSEL, 1, 0) == LB_ERR);
  CHECK(SendMessage(multi, LB_SETSEL, TRUE, -1) == 0);
  CHECK(SendMessage(multi, LB_GETSEL, 0, 0) > 0 && SendMessage(multi, LB_GETSEL, 2, 0) > 0);
  CHECK(SendMessage(multi, LB_SETSEL, FALSE, 1) == 0 && SendMessage(multi, LB_GETSEL, 1, 0) == 0);
  CHECK(SendMessage(multi, LB_GETSEL, 3, 0) == LB_ERR && SendMessage(multi, LB_SETSEL, TRUE, 5) == LB_ERR);
  QListWidget *multiList = widgetOf<QListWidget>(multi);
  multiList->doItemsLayout();
  events.clear();
  click(multiList->viewport(), itemCenter(multiList, 0));
  CHECK(SendMessage(multi, LB_GETSEL, 0, 0) == 0 && SendMessage(multi, LB_GETSEL, 2, 0) > 0);
  CHECK((notifications() == std::vector<Notification>{{multi, 503, LBN_SELCHANGE}}));
  click(multiList->viewport(), itemCenter(multiList, 1));
  CHECK(SendMessage(multi, LB_GETSEL, 1, 0) > 0 && SendMessage(multi, LB_GETCURSEL, 0, 0) == 1);

  CHECK(DestroyWindow(parent));
}

void testListSubclassing() {
  HWND parent = createParent();
  HWND a = createControl(parent, L"LISTBOX", WS_VSCROLL | LBS_NOTIFY, 10, 10, 120, 100, 601);
  HWND b = createControl(parent, L"LISTBOX", WS_VSCROLL | LBS_NOTIFY, 140, 10, 120, 100, 602);
  for (int i = 0; i < 50; i++) {
    const std::wstring text = L"Item " + std::to_wstring(i);
    SendMessage(a, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessage(b, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
  }
  QCoreApplication::processEvents();

  originalListProc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(a, GWLP_WNDPROC));
  CHECK(originalListProc && originalListProc == reinterpret_cast<WNDPROC>(GetWindowLongPtr(b, GWLP_WNDPROC)));
  syncedLists = {a, b};
  SetWindowLongPtr(a, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(syncListProc));
  SetWindowLongPtr(b, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(syncListProc));

  QListWidget *listA = widgetOf<QListWidget>(a);
  // The wheel over the list.
  sendWheel(listA->viewport(), -120);
  const LRESULT topAfterWheel = SendMessage(a, LB_GETTOPINDEX, 0, 0);
  CHECK(topAfterWheel > 0 && SendMessage(b, LB_GETTOPINDEX, 0, 0) == topAfterWheel);

  // The scroll bar.
  listA->verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepAdd);
  const LRESULT topAfterPage = SendMessage(a, LB_GETTOPINDEX, 0, 0);
  CHECK(topAfterPage > topAfterWheel && SendMessage(b, LB_GETTOPINDEX, 0, 0) == topAfterPage);

  // Keys, and WM_VSCROLL sent by the application.
  SendMessage(a, LB_SETCURSEL, 0, 0);
  SendMessage(a, LB_SETTOPINDEX, 0, 0);
  for (int i = 0; i < 20; i++)
    sendKey(listA, Qt::Key_Down);
  CHECK(SendMessage(a, LB_GETCURSEL, 0, 0) == 20);
  CHECK(SendMessage(a, LB_GETTOPINDEX, 0, 0) > 0 && SendMessage(b, LB_GETTOPINDEX, 0, 0) == SendMessage(a, LB_GETTOPINDEX, 0, 0));
  SendMessage(b, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
  CHECK(SendMessage(a, LB_GETTOPINDEX, 0, 0) == SendMessage(b, LB_GETTOPINDEX, 0, 0));
  SendMessage(b, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
  CHECK(SendMessage(a, LB_GETTOPINDEX, 0, 0) == 0);

  // A subclass that swallows WM_KEYDOWN stops the list from handling the key.
  swallowKeys = true;
  sendKey(listA, Qt::Key_Down);
  CHECK(SendMessage(a, LB_GETCURSEL, 0, 0) == 20);
  swallowKeys = false;

  CHECK(SendMessage(a, LB_SETTOPINDEX, 60, 0) == LB_ERR);
  syncedLists.clear();
  CHECK(DestroyWindow(parent));
}

/* ------------------------------------------------------------------ */

void testCaptureAndCursor() {
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = parentProc;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"ControlsTestCanvas";
  CHECK(RegisterClassEx(&wc) != 0);

  HWND parent = createParent();
  HWND canvas = CreateWindowEx(0, L"ControlsTestCanvas", L"", WS_CHILD | WS_VISIBLE, 0, 0, 500, 300, parent,
                               nullptr, meos_qt::applicationInstance(), nullptr);
  HWND button = createControl(canvas, L"BUTTON", BS_PUSHBUTTON, 100, 50, 80, 24, 701, L"B");
  QWidget *buttonWidget = clientOf(button);

  // While the canvas has the capture, input over the button goes to the canvas.
  SetCapture(canvas);
  events.clear();
  sendMouse(buttonWidget, QEvent::MouseMove, QPoint(5, 6));
  const Event *move = lastOf(canvas, WM_MOUSEMOVE);
  CHECK(move && GET_X_LPARAM(move->lParam) == 105 && GET_Y_LPARAM(move->lParam) == 56);
  click(buttonWidget, QPoint(5, 6));
  CHECK(countOf(canvas, WM_LBUTTONDOWN) == 1 && countOf(canvas, WM_LBUTTONUP) == 1 && notifications().empty());
  ReleaseCapture();
  click(buttonWidget, QPoint(5, 6));
  CHECK((notifications() == std::vector<Notification>{{button, 701, BN_CLICKED}}));

  // Cursors: system cursors only; SetCursor returns the previous one.
  const HCURSOR hand = LoadCursor(nullptr, IDC_HAND);
  const HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
  CHECK(hand && arrow && hand != arrow && LoadCursor(nullptr, MAKEINTRESOURCE(1)) == nullptr);
  SetCapture(canvas);
  SetCursor(arrow);
  CHECK(SetCursor(hand) == arrow);
  CHECK(clientOf(canvas)->cursor().shape() == Qt::PointingHandCursor);
  ReleaseCapture();
  // The next mouse movement sets the class cursor again.
  sendMouse(clientOf(canvas), QEvent::MouseMove, QPoint(10, 10));
  CHECK(clientOf(canvas)->cursor().shape() == Qt::ArrowCursor);
  SetCursor(LoadCursor(nullptr, IDC_WAIT));
  CHECK(QApplication::overrideCursor() && QApplication::overrideCursor()->shape() == Qt::WaitCursor);
  SetCursor(arrow);
  CHECK(!QApplication::overrideCursor());

  CHECK(DestroyWindow(parent));
}

/* ------------------------------------------------------------------ */

void sendToolTipEvent(QWidget *widget, QPoint position) {
  QHelpEvent event(QEvent::ToolTip, position, widget->mapToGlobal(position));
  QCoreApplication::sendEvent(widget, &event);
}

void testToolTips() {
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = parentProc;
  wc.lpszClassName = L"ControlsTestToolCanvas";
  CHECK(RegisterClassEx(&wc) != 0);

  HWND parent = CreateWindowEx(0, L"ControlsTestToolCanvas", L"", WS_POPUP | WS_VISIBLE, 50, 50, 400, 300, nullptr,
                               nullptr, meos_qt::applicationInstance(), nullptr);
  HWND button = createControl(parent, L"BUTTON", BS_PUSHBUTTON, 200, 10, 80, 24, 801, L"B");
  HWND tip = CreateWindow(TOOLTIPS_CLASS, nullptr, TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                          CW_USEDEFAULT, nullptr, nullptr, meos_qt::applicationInstance(), nullptr);
  CHECK(tip != nullptr);

  TOOLINFOW buttonTool = {};
  buttonTool.cbSize = sizeof(buttonTool);
  buttonTool.uFlags = TTF_IDISHWND;
  buttonTool.hwnd = parent;
  buttonTool.uId = reinterpret_cast<UINT_PTR>(button);
  std::wstring buttonText = L"Button help";
  buttonTool.lpszText = &buttonText[0];
  CHECK(SendMessage(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&buttonTool)) == TRUE);

  TOOLINFOW areaTool = {};
  areaTool.cbSize = sizeof(areaTool);
  areaTool.uFlags = TTF_SUBCLASS;
  areaTool.hwnd = parent;
  areaTool.uId = 1;
  areaTool.rect = RECT{10, 10, 60, 30};
  std::wstring areaText = L"Area help";
  areaTool.lpszText = &areaText[0];
  SendMessage(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&areaTool));
  // The tooltip copies the text.
  buttonText = L"Changed";

  sendToolTipEvent(clientOf(button), QPoint(3, 3));
  CHECK(QToolTip::isVisible() && QToolTip::text().contains(QStringLiteral("Button help")));
  sendToolTipEvent(clientOf(parent), QPoint(20, 20));
  CHECK(QToolTip::isVisible() && QToolTip::text().contains(QStringLiteral("Area help")));
  sendToolTipEvent(clientOf(parent), QPoint(100, 100));
  CHECK(waitFor([] { return !QToolTip::isVisible(); }));

  // Rectangles move with TTM_NEWTOOLRECTW; texts change with TTM_UPDATETIPTEXTW.
  areaTool.rect = RECT{90, 90, 120, 120};
  SendMessage(tip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&areaTool));
  std::wstring updated = L"Line one\nline two";
  areaTool.lpszText = &updated[0];
  SendMessage(tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&areaTool));
  CHECK(SendMessage(tip, TTM_SETMAXTIPWIDTH, 0, 250) == -1);
  sendToolTipEvent(clientOf(parent), QPoint(100, 100));
  CHECK(QToolTip::isVisible() && QToolTip::text().contains(QStringLiteral("Line one<br>line two")));

  SendMessage(tip, TTM_DELTOOL, 0, reinterpret_cast<LPARAM>(&buttonTool));
  sendToolTipEvent(clientOf(button), QPoint(3, 3));
  CHECK(waitFor([] { return !QToolTip::isVisible(); }));

  CHECK(DestroyWindow(tip));
  CHECK(DestroyWindow(parent));
}

/* ------------------------------------------------------------------ */

// A bitmap filled with one colour, for the image list.
HBITMAP colourBitmap(int width, int height, COLORREF colour) {
  const HDC screen = GetDC(nullptr);
  const HDC memory = CreateCompatibleDC(screen);
  const HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
  ReleaseDC(nullptr, screen);
  const HGDIOBJ previous = SelectObject(memory, bitmap);
  const HBRUSH brush = CreateSolidBrush(colour);
  SelectObject(memory, brush);
  SelectObject(memory, GetStockObject(NULL_PEN));
  Rectangle(memory, 0, 0, width + 1, height + 1);
  SelectObject(memory, previous);
  DeleteObject(brush);
  DeleteDC(memory);
  return bitmap;
}

void testTabControl() {
  HWND parent = createParent();
  HWND tabs = createControl(parent, WC_TABCONTROL, 0, 0, 0, 300, 24, 200, L"tabs");
  QTabBar *bar = widgetOf<QTabBar>(tabs);
  CHECK(bar != nullptr);
  if (!bar) {
    DestroyWindow(parent);
    return;
  }

  // Notifications arrive as WM_NOTIFY with an NMHDR; the id is in wParam as well.
  // The selection is already the new one in TCN_SELCHANGE and still the old one
  // in TCN_SELCHANGING.
  std::vector<Notification> notes;
  bool refuse = false;
  int selectionWhileChanging = -2;
  int selectionWhileChanged = -2;
  parentHandler = [&](HWND, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
    if (message != WM_NOTIFY)
      return 0;
    const auto *header = reinterpret_cast<const NMHDR *>(lParam);
    notes.push_back(Notification{header->hwndFrom, int(header->idFrom), int(header->code)});
    CHECK(int(wParam) == int(header->idFrom));
    if (header->code == TCN_SELCHANGING) {
      selectionWhileChanging = TabCtrl_GetCurSel(tabs);
      return refuse ? TRUE : 0;
    }
    if (header->code == TCN_SELCHANGE)
      selectionWhileChanged = TabCtrl_GetCurSel(tabs);
    return 0;
  };

  // An empty control has no selection.
  CHECK(TabCtrl_GetItemCount(tabs) == 0);
  CHECK(TabCtrl_GetCurSel(tabs) == -1);

  auto insert = [&](int index, const wchar_t *text, int image) {
    TCITEM item = {};
    item.mask = UINT(TCIF_TEXT | (image >= 0 ? TCIF_IMAGE : 0));
    item.pszText = const_cast<LPWSTR>(text);
    item.iImage = image;
    return TabCtrl_InsertItem(tabs, index, &item);
  };

  // Inserting: the first tab becomes the selected one, without a notification.
  // An index beyond the end appends.
  CHECK(insert(0, L"Competition", -1) == 0);
  CHECK(TabCtrl_GetCurSel(tabs) == 0);
  CHECK(insert(1, L"Runners", -1) == 1);
  CHECK(insert(9, L"Classes", -1) == 2);
  CHECK(TabCtrl_GetItemCount(tabs) == 3);
  CHECK(bar->tabText(1) == QString("Runners"));
  CHECK(notes.empty());

  // A selection by the application returns the previous index and notifies
  // nothing; an invalid index changes nothing.
  CHECK(TabCtrl_SetCurSel(tabs, 2) == 0);
  CHECK(TabCtrl_GetCurSel(tabs) == 2);
  CHECK(TabCtrl_SetCurSel(tabs, 7) == -1 && TabCtrl_GetCurSel(tabs) == 2);
  CHECK(TabCtrl_SetCurSel(tabs, -1) == -1 && TabCtrl_GetCurSel(tabs) == 2);
  CHECK(notes.empty());

  // A click by the user: TCN_SELCHANGING, then TCN_SELCHANGE.
  click(bar, bar->tabRect(0).center());
  CHECK(TabCtrl_GetCurSel(tabs) == 0);
  const Notification changing = {tabs, 200, int(TCN_SELCHANGING)};
  const Notification changed = {tabs, 200, int(TCN_SELCHANGE)};
  CHECK(notes.size() == 2);
  CHECK(notes.size() == 2 && notes[0] == changing && notes[1] == changed);
  CHECK(selectionWhileChanging == 2 && selectionWhileChanged == 0);

  // A click on the selected tab changes nothing and notifies nothing.
  notes.clear();
  click(bar, bar->tabRect(0).center());
  CHECK(notes.empty() && TabCtrl_GetCurSel(tabs) == 0);

  // The parent refuses the change (MeOS does that for a page with unsaved input).
  notes.clear();
  refuse = true;
  click(bar, bar->tabRect(1).center());
  CHECK(notes.size() == 1 && notes[0].code == int(TCN_SELCHANGING));
  CHECK(TabCtrl_GetCurSel(tabs) == 0);
  refuse = false;

  // Image list: an index per image, a strip adds one image per width, a
  // monochrome mask makes the pixels of its set bits transparent.
  HIMAGELIST list = ImageList_Create(16, 16, ILC_COLOR32, 2, 1);
  CHECK(list != nullptr);
  HBITMAP red = colourBitmap(16, 16, RGB(255, 0, 0));
  HBITMAP strip = colourBitmap(32, 16, RGB(0, 255, 0));
  // A monochrome mask: a set (white) bit is transparent.
  HBITMAP mask = colourBitmap(16, 16, RGB(255, 255, 255));
  CHECK(ImageList_Add(list, red, nullptr) == 0);
  CHECK(ImageList_Add(list, strip, nullptr) == 1);
  CHECK(ImageList_Add(list, red, mask) == 3);
  CHECK(ImageList_Add(nullptr, red, nullptr) == -1);

  CHECK(TabCtrl_SetImageList(tabs, list) == nullptr);
  CHECK(bar->tabIcon(0).isNull());
  CHECK(insert(3, L"Courses", 0) == 3);
  CHECK(!bar->tabIcon(3).isNull());
  CHECK(bar->tabIcon(3).pixmap(16, 16).toImage().pixelColor(8, 8) == QColor(255, 0, 0));
  // The image added with the mask is transparent where the mask is set.
  CHECK(insert(4, L"Controls", 3) == 4);
  CHECK(bar->tabIcon(4).pixmap(16, 16).toImage().pixelColor(8, 8).alpha() == 0);
  // A new image list replaces the icons and returns the previous one.
  HIMAGELIST second = ImageList_Create(16, 16, ILC_COLOR32, 1, 1);
  CHECK(ImageList_Add(second, colourBitmap(16, 16, RGB(0, 0, 255)), nullptr) == 0);
  CHECK(TabCtrl_SetImageList(tabs, second) == list);
  CHECK(bar->tabIcon(3).pixmap(16, 16).toImage().pixelColor(8, 8) == QColor(0, 0, 255));
  CHECK(ImageList_Destroy(list) && ImageList_Destroy(second));

  // The font of the row.
  const HFONT font = CreateFont(20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
  CHECK(SendMessage(tabs, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE) == 0);
  CHECK(reinterpret_cast<HFONT>(SendMessage(tabs, WM_GETFONT, 0, 0)) == font);
  CHECK(bar->font().family() == QString("Arial"));

  // Deleting everything leaves no selection and notifies nothing.
  notes.clear();
  CHECK(TabCtrl_DeleteAllItems(tabs));
  CHECK(TabCtrl_GetItemCount(tabs) == 0 && TabCtrl_GetCurSel(tabs) == -1);
  CHECK(notes.empty());

  parentHandler = nullptr;
  DeleteObject(font);
  DeleteObject(red);
  DeleteObject(strip);
  DeleteObject(mask);
  CHECK(DestroyWindow(parent));
}

/* ------------------------------------------------------------------ */

void testToolbarAndStatic() {
  HWND floater = CreateWindowEx(WS_EX_TOOLWINDOW, parentClass, L"Tools", WS_POPUP | WS_CAPTION, 100, 100, 300, 64,
                                nullptr, nullptr, meos_qt::applicationInstance(), nullptr);
  HWND toolbar = CreateWindowEx(0, TOOLBARCLASSNAME, nullptr, WS_CHILD | TBSTYLE_TOOLTIPS, 0, 0, 0, 0, floater,
                                nullptr, meos_qt::applicationInstance(), nullptr);
  CHECK(toolbar != nullptr);

  HIMAGELIST images = ImageList_Create(24, 24, ILC_COLOR24 | ILC_MASK, 1, 15);
  CHECK(images != nullptr);
  CHECK(SendMessage(toolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images)) == 0);
  CHECK(SendMessage(toolbar, TB_LOADIMAGES, IDB_STD_LARGE_COLOR, reinterpret_cast<LPARAM>(HINST_COMMCTRL)) == 15);
  // Bitmap resources follow in step 1.2.5.
  CHECK(ImageList_LoadImage(meos_qt::applicationInstance(), MAKEINTRESOURCE(1), 24, 17, CLR_DEFAULT, IMAGE_BITMAP,
                            LR_CREATEDIBSECTION) == nullptr);

  std::wstring copy = L"Copy", print = L"Print";
  TBBUTTON buttons[] = {
      {MAKELONG(STD_COPY, 0), 1013, TBSTATE_ENABLED, BTNS_AUTOSIZE, {0}, 0, reinterpret_cast<INT_PTR>(copy.c_str())},
      {MAKELONG(STD_PRINT, 0), 1014, TBSTATE_ENABLED, BTNS_AUTOSIZE, {0}, 0, reinterpret_cast<INT_PTR>(print.c_str())},
  };
  SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
  CHECK(SendMessage(toolbar, TB_ADDBUTTONS, 2, reinterpret_cast<LPARAM>(buttons)) == TRUE);
  const LRESULT size = SendMessage(toolbar, TB_GETBUTTONSIZE, 0, 0);
  CHECK(LOWORD(size) == 31 && HIWORD(size) == 30);
  SendMessage(toolbar, TB_AUTOSIZE, 0, 0);
  RECT rect;
  GetWindowRect(toolbar, &rect);
  CHECK(rect.bottom - rect.top == 34);

  QToolBar *bar = widgetOf<QToolBar>(toolbar);
  CHECK(bar && bar->actions().size() == 2 && !bar->actions()[1]->icon().isNull());
  CHECK(bar->actions()[0]->toolTip() == QStringLiteral("Copy"));
  QWidget *copyButton = bar->widgetForAction(bar->actions()[0]);
  sendToolTipEvent(copyButton, QPoint(2, 2));
  CHECK(QToolTip::isVisible() && QToolTip::text() == QStringLiteral("Copy"));
  QToolTip::hideText();
  events.clear();
  bar->actions()[1]->trigger();
  CHECK((notifications() == std::vector<Notification>{{toolbar, 1014, BN_CLICKED}}));
  CHECK(ImageList_Destroy(images) && !ImageList_Destroy(images));
  CHECK(DestroyWindow(floater));

  // STATIC is a canvas: MeOS' progress window draws on it with GetDC.
  HWND parent = createParent();
  HWND label = CreateWindowEx(WS_EX_TOPMOST, L"STATIC", L"Text", WS_VISIBLE | WS_CHILD, 10, 10, 100, 20, parent,
                              nullptr, meos_qt::applicationInstance(), nullptr);
  CHECK(label && windowText(label) == L"Text");
  CHECK(UpdateWindow(label));
  HDC dc = GetDC(label);
  CHECK(dc != nullptr);
  SelectObject(dc, GetStockObject(DC_BRUSH));
  SetDCBrushColor(dc, RGB(10, 20, 30));
  Rectangle(dc, 0, 0, 10, 10);
  ReleaseDC(label, dc);
  {
    const std::shared_ptr<meos_qt::Surface> surface = meos_qt::findWindow(label)->surface;
    std::lock_guard<std::mutex> lock(surface->mutex);
    CHECK(surface->image.pixel(5, 5) == qRgb(10, 20, 30));
  }
  CHECK(DestroyWindow(parent));
}

} // namespace

int main(int argc, char **argv) {
  const std::unique_ptr<QApplication> app = meos_qt::createApplication(argc, argv);

  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = parentProc;
  wc.hInstance = meos_qt::applicationInstance();
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = parentClass;
  if (!RegisterClassEx(&wc)) {
    std::fprintf(stderr, "cannot register the parent class\n");
    return 1;
  }

  testButtons();
  testEdit();
  testFocus();
  testComboBox();
  testListBox();
  testListSubclassing();
  testCaptureAndCursor();
  testToolTips();
  testToolbarAndStatic();
  testTabControl();

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("controls self test passed\n");
  return 0;
}
