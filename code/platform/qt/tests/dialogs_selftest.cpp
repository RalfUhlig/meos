/************************************************************************
    MeOS - Orienteering Software
    Linux port: self test of resources, clipboard, dialogs, menus, shell and
    screen functions on Qt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Checks the functions of code/platform/qt that MeOS uses outside drawing and
// controls: every resource of meos.rc and meoslang.rc (read here independently of
// cmake/Win32Resources.cmake), global memory and the clipboard with the formats of
// gdioutput::copyToClipboard, message boxes with the WH_CBT hook of gdioutput::ask,
// file, folder and colour dialogs, popup menus, ShellExecute, system metrics, window
// placement and the missing printer. Modal dialogs are operated from timers, as a
// user would. Usage: meos_dialogs_selftest <code directory>; runs with
// QT_QPA_PLATFORM=offscreen under ctest.

#include "platform/qt/win32_gdi.h"

#include <QAbstractButton>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QScreen>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <thread>

#include "commctrl.h"
#include "commdlg.h"
#include "shellapi.h"
#include "shlobj.h"

namespace {

int failures = 0;

#define CHECK(expr)                                                                   \
  do {                                                                                \
    if (!(expr)) {                                                                    \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr);   \
      failures++;                                                                     \
    }                                                                                 \
  } while (false)

const wchar_t *const ownerClass = L"DialogsTestOwner";

std::vector<UINT> ownerMessages;

LRESULT CALLBACK ownerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  ownerMessages.push_back(message);
  return DefWindowProc(window, message, wParam, lParam);
}

int countOf(UINT message) {
  return int(std::count(ownerMessages.begin(), ownerMessages.end(), message));
}

HWND createOwner(DWORD style = WS_POPUP | WS_VISIBLE, int x = 60, int y = 40, int width = 500, int height = 300) {
  return CreateWindowEx(0, ownerClass, L"Owner", style, x, y, width, height, nullptr, nullptr,
                        meos_qt::applicationInstance(), nullptr);
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

bool waitFor(const std::function<bool()> &condition, int milliseconds = 5000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  while (!condition()) {
    if (std::chrono::steady_clock::now() > deadline)
      return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return true;
}

void sendKey(QWidget *widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QCoreApplication::sendEvent(widget, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QCoreApplication::sendEvent(widget, &release);
}

std::wstring windowText(HWND window) {
  wchar_t buffer[256] = {};
  GetWindowText(window, buffer, 256);
  return buffer;
}

QRect windowRect(HWND window) {
  RECT rect;
  GetWindowRect(window, &rect);
  return QRect(QPoint(rect.left, rect.top), QPoint(rect.right - 1, rect.bottom - 1));
}

// Runs an action from a Win32 timer every 10 ms until it returns true, so that it
// runs inside the modal loop of a dialog. A dialog that never appears ends the test.
std::function<bool()> timerAction;
int timerTicks = 0;

void CALLBACK actionTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
  if (++timerTicks > 1000) {
    std::fprintf(stderr, "a dialog did not appear or close in time\n");
    std::_Exit(1);
  }
  const std::function<bool()> action = timerAction;
  if (action && action()) {
    KillTimer(nullptr, id);
    timerAction = nullptr;
  }
}

void whenReady(std::function<bool()> action) {
  timerAction = std::move(action);
  timerTicks = 0;
  SetTimer(nullptr, 0, 10, actionTimerProc);
}

// QFileDialog::accept, which appends the default suffix, is protected.
void accept(QDialog *dialog) {
  dialog->accept();
}

template <typename T>
T *activeModal() {
  QWidget *modal = QApplication::activeModalWidget();
  return modal && modal->isVisible() ? qobject_cast<T *>(modal) : nullptr;
}

/* ---------------------------------------------------------------------
   Resources
   --------------------------------------------------------------------- */

struct RcEntry {
  std::string name; // number or name
  std::string type; // number or type name
  std::filesystem::path file;
};

std::string stripCr(std::string line) {
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  return line;
}

std::filesystem::path findFileNoCase(const std::filesystem::path &path) {
  if (std::filesystem::exists(path))
    return path;
  std::string wanted = path.filename().string();
  std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  for (const auto &entry : std::filesystem::directory_iterator(path.parent_path())) {
    std::string name = entry.path().filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (name == wanted)
      return entry.path();
  }
  return path;
}

// The single-line file resources of the scripts, with symbols resolved.
std::vector<RcEntry> readResourceScripts(const std::filesystem::path &codeDir) {
  std::map<std::string, std::string> symbols;
  const std::regex define(R"(^\s*#\s*define\s+([A-Za-z_]\w*)\s+(0[xX][0-9a-fA-F]+|\d+)\s*$)");
  for (const char *file : {"resource.h", "meos.rc", "meoslang.rc"}) {
    std::ifstream in(codeDir / file);
    for (std::string line; std::getline(in, line);) {
      std::smatch match;
      line = stripCr(line);
      if (std::regex_match(line, match, define))
        symbols[match[1]] = std::to_string(std::stol(match[2], nullptr, 0));
    }
  }
  const std::map<std::string, std::string> predefined = {{"BITMAP", "2"}, {"ICON", "14"}, {"HTML", "23"},
                                                         {"RCDATA", "10"}, {"CURSOR", "12"}, {"FONT", "8"}};
  const std::regex resource(
      R"re(^\s*([A-Za-z0-9_]+)\s+([A-Za-z0-9_]+)(?:\s+(?:PRELOAD|LOADONCALL|FIXED|MOVEABLE|DISCARDABLE|PURE|IMPURE))*\s+"([^"]+)"\s*$)re");
  std::vector<RcEntry> entries;
  for (const char *file : {"meos.rc", "meoslang.rc"}) {
    std::ifstream in(codeDir / file);
    for (std::string line; std::getline(in, line);) {
      std::smatch match;
      line = stripCr(line);
      if (!std::regex_match(line, match, resource))
        continue;
      RcEntry entry{match[1], match[2], {}};
      if (symbols.count(entry.name))
        entry.name = symbols[entry.name];
      if (symbols.count(entry.type))
        entry.type = symbols[entry.type];
      if (predefined.count(entry.type))
        entry.type = predefined.at(entry.type);
      std::string path = match[3];
      std::replace(path.begin(), path.end(), '\\', '/');
      entry.file = findFileNoCase(codeDir / path);
      entries.push_back(entry);
    }
  }
  return entries;
}

bool isNumber(const std::string &text) {
  return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); });
}

// The ways MeOS names a resource: MAKEINTRESOURCE(id), "#id" or the name in any case.
std::vector<std::wstring> spellings(const std::string &id) {
  const std::wstring wide(id.begin(), id.end());
  if (isNumber(id))
    return {L"#" + wide};
  std::wstring lower = wide;
  for (wchar_t &c : lower)
    c = wchar_t(std::towlower(wint_t(c)));
  return {wide, lower};
}

void testResources(const std::filesystem::path &codeDir) {
  const std::vector<RcEntry> entries = readResourceScripts(codeDir);
  int languages = 0, pngs = 0, bitmaps = 0;
  for (const RcEntry &entry : entries) {
    languages += entry.type == "300";
    pngs += entry.type == "PNG";
    bitmaps += entry.type == "2";
  }
  CHECK(languages >= 12 && pngs >= 10 && bitmaps >= 2);

  for (const RcEntry &entry : entries) {
    std::ifstream in(entry.file, std::ios::binary);
    const std::vector<char> content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(!content.empty());
    // The resource compiler drops the BITMAPFILEHEADER of bitmaps.
    const std::size_t skip = entry.type == "2" ? 14 : 0;

    std::vector<LPCWSTR> names, types;
    std::vector<std::wstring> nameStrings = spellings(entry.name), typeStrings = spellings(entry.type);
    for (const std::wstring &s : nameStrings)
      names.push_back(s.c_str());
    for (const std::wstring &s : typeStrings)
      types.push_back(s.c_str());
    if (isNumber(entry.name))
      names.push_back(MAKEINTRESOURCE(std::stoi(entry.name)));
    if (isNumber(entry.type))
      types.push_back(MAKEINTRESOURCE(std::stoi(entry.type)));

    for (LPCWSTR name : names) {
      for (LPCWSTR type : types) {
        const HRSRC found = FindResource(nullptr, name, type);
        if (!found) {
          std::fprintf(stderr, "resource %s/%s (%s) not found\n", entry.type.c_str(), entry.name.c_str(),
                       entry.file.string().c_str());
          failures++;
          continue;
        }
        CHECK(FindResource(GetModuleHandle(nullptr), name, type) == found);
        const HGLOBAL loaded = LoadResource(nullptr, found);
        const auto data = static_cast<const char *>(LockResource(loaded));
        const DWORD size = SizeofResource(GetModuleHandle(nullptr), found);
        CHECK(data && size + skip == content.size());
        CHECK(data && std::memcmp(data, content.data() + skip, size) == 0);
      }
    }
  }

  // As localizer.cpp and image.cpp ask for them.
  HRSRC english = FindResource(nullptr, L"#104", L"#300");
  CHECK(english && SizeofResource(nullptr, english) > 1000);
  CHECK(FindResource(nullptr, MAKEINTRESOURCE(512), L"PNG") != nullptr);

  // Missing resources, as Windows reports them.
  SetLastError(0);
  CHECK(!FindResource(nullptr, L"#9999", L"#300") && GetLastError() == ERROR_RESOURCE_NAME_NOT_FOUND);
  SetLastError(0);
  CHECK(!FindResource(nullptr, L"#104", L"NOSUCHTYPE") && GetLastError() == ERROR_RESOURCE_TYPE_NOT_FOUND);
  CHECK(!FindResource(nullptr, L"#12x", L"#300"));
  CHECK(LoadResource(nullptr, nullptr) == nullptr && SizeofResource(nullptr, nullptr) == 0);
  CHECK(GetModuleHandle(nullptr) == meos_qt::applicationInstance());
  SetLastError(0);
  CHECK(GetModuleHandle(L"user32.dll") == nullptr && GetLastError() == ERROR_MOD_NOT_FOUND);

  // Bitmaps: gdioutput::addButton (LoadBitmap) and the table toolbar (image list).
  constexpr int bmpTest = 15;
  constexpr int ecoBitmap = 131;
  const HBITMAP strip = LoadBitmap(GetModuleHandle(nullptr), MAKEINTRESOURCE(bmpTest));
  CHECK(strip && meos_qt::bitmapPixmap(strip).size() == QSize(168, 24));
  const HBITMAP eco = LoadBitmap(GetModuleHandle(nullptr), MAKEINTRESOURCE(ecoBitmap));
  CHECK(eco && meos_qt::bitmapPixmap(eco).size() == QSize(24, 24));
  CHECK(LoadBitmap(GetModuleHandle(nullptr), MAKEINTRESOURCE(9999)) == nullptr);
  DeleteObject(strip);
  DeleteObject(eco);
  const HIMAGELIST list = ImageList_LoadImage(GetModuleHandle(nullptr), MAKEINTRESOURCE(bmpTest), 24, 17, CLR_DEFAULT,
                                              IMAGE_BITMAP, LR_CREATEDIBSECTION);
  CHECK(list != nullptr);
  ImageList_Destroy(list);
}

/* ---------------------------------------------------------------------
   Global memory and clipboard
   --------------------------------------------------------------------- */

void testGlobalMemory() {
  const HGLOBAL moveable = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 10);
  CHECK(moveable && GlobalSize(moveable) == 10);
  const auto memory = static_cast<char *>(GlobalLock(moveable));
  CHECK(memory && memory != moveable && memory[9] == 0);
  CHECK(GlobalLock(moveable) == memory);
  CHECK(GlobalUnlock(moveable) == TRUE);
  SetLastError(1);
  CHECK(GlobalUnlock(moveable) == FALSE && GetLastError() == ERROR_SUCCESS);
  CHECK(GlobalUnlock(moveable) == FALSE && GetLastError() != ERROR_SUCCESS);
  CHECK(GlobalFree(moveable) == nullptr);
  CHECK(GlobalFree(moveable) == moveable && GlobalSize(moveable) == 0 && !GlobalLock(moveable));

  const HGLOBAL fixed = GlobalAlloc(GMEM_FIXED, 4);
  CHECK(fixed && GlobalLock(fixed) == fixed && GlobalSize(fixed) == 4);
  CHECK(GlobalFree(fixed) == nullptr);
}

HGLOBAL globalCopy(const void *data, std::size_t size) {
  const HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size);
  std::memcpy(GlobalLock(block), data, size);
  GlobalUnlock(block);
  return block;
}

std::string blockBytes(HANDLE block) {
  const auto data = static_cast<const char *>(GlobalLock(block));
  std::string bytes(data ? data : "", data ? GlobalSize(block) : 0);
  GlobalUnlock(block);
  return bytes;
}

int cfHtmlOffset(const std::string &data, const std::string &key) {
  const std::size_t position = data.find(key + ":");
  return position == std::string::npos ? -1 : std::atoi(data.c_str() + position + key.size() + 1);
}

void testClipboard() {
  const HWND owner = createOwner();
  QClipboard *qtClipboard = QGuiApplication::clipboard();
  const UINT cfHtml = RegisterClipboardFormat(L"HTML format");
  CHECK(cfHtml >= 0xC000 && RegisterClipboardFormat(L"HTML Format") == cfHtml);
  CHECK(RegisterClipboardFormat(L"MeOS private") != cfHtml);

  SetLastError(0);
  CHECK(!SetClipboardData(CF_UNICODETEXT, globalCopy(L"x", sizeof(wchar_t))) &&
        GetLastError() == ERROR_CLIPBOARD_NOT_OPEN);
  CHECK(!GetClipboardData(CF_UNICODETEXT) && !CloseClipboard() && !EmptyClipboard());

  // gdioutput::copyToClipboard: a CF_HTML header with "\n" and 8 digits, the length
  // counting the terminating null, and CF_UNICODETEXT without a terminating null.
  const std::string html = "<table><tr><td>\xC3\x85sa</td><td>12:03</td></tr></table>";
  const std::wstring text = L"Åsa\t12:03";
  const char headerFormat[] = "Version:0.9\nStartHTML:%08u\nEndHTML:%08u\nStartFragment:%08u\nEndFragment:%08u\n";
  char header[256];
  std::snprintf(header, sizeof(header), headerFormat, 1u, 0u, 0u, 0u);
  const unsigned offset = unsigned(std::strlen(header));
  const unsigned length = unsigned(html.size() + 1);
  std::snprintf(header, sizeof(header), headerFormat, offset, offset + length, offset, offset + length);
  std::string cfHtmlBlock = std::string(header) + html;
  cfHtmlBlock.push_back('\0');

  CHECK(OpenClipboard(owner) && EmptyClipboard());
  CHECK(SetClipboardData(cfHtml, globalCopy(cfHtmlBlock.data(), cfHtmlBlock.size())) != nullptr);
  CHECK(SetClipboardData(CF_UNICODETEXT, globalCopy(text.data(), text.size() * sizeof(wchar_t))) != nullptr);
  // Readable before the clipboard is closed.
  CHECK(blockBytes(GetClipboardData(cfHtml)) == cfHtmlBlock);
  CHECK(CloseClipboard());

  const QMimeData *mime = qtClipboard->mimeData();
  CHECK(mime && mime->hasHtml() && mime->html() == QString::fromUtf8(html.c_str()));
  CHECK(mime && mime->text() == QString::fromStdWString(text));

  // Back as Table::importClipboard reads it, and converted.
  CHECK(OpenClipboard(owner));
  HANDLE unicode = GetClipboardData(CF_UNICODETEXT);
  CHECK(unicode && GlobalSize(unicode) == (text.size() + 1) * sizeof(wchar_t));
  CHECK(unicode && std::wstring(static_cast<const wchar_t *>(GlobalLock(unicode))) == text);
  GlobalUnlock(unicode);
  const std::string ansi = blockBytes(GetClipboardData(CF_TEXT));
  CHECK(ansi == std::string("\xC5sa\t12:03", 9) + '\0');
  const std::string htmlBack = blockBytes(GetClipboardData(cfHtml));
  const int startHtml = cfHtmlOffset(htmlBack, "StartHTML"), endHtml = cfHtmlOffset(htmlBack, "EndHTML");
  const int startFragment = cfHtmlOffset(htmlBack, "StartFragment"), endFragment = cfHtmlOffset(htmlBack, "EndFragment");
  CHECK(htmlBack.compare(0, 12, "Version:0.9\r") == 0 && htmlBack.back() == '\0');
  CHECK(startHtml > 0 && htmlBack.substr(std::size_t(startHtml), std::size_t(endHtml - startHtml)) == html);
  CHECK(startFragment == startHtml && endFragment == endHtml);
  CHECK(CloseClipboard());

  // Text set by another program; CF_TEXT as TabCompetition.cpp sets it.
  auto foreign = new QMimeData;
  foreign->setText(QStringLiteral("a\tb\r\nc\td"));
  foreign->setHtml(QStringLiteral("<html><body><!--StartFragment--><b>x</b><!--EndFragment--></body></html>"));
  qtClipboard->setMimeData(foreign);
  CHECK(OpenClipboard(owner));
  unicode = GetClipboardData(CF_UNICODETEXT);
  CHECK(unicode && std::wstring(static_cast<const wchar_t *>(GlobalLock(unicode))) == L"a\tb\r\nc\td");
  const std::string fragment = blockBytes(GetClipboardData(cfHtml));
  CHECK(fragment.substr(std::size_t(cfHtmlOffset(fragment, "StartFragment")),
                        std::size_t(cfHtmlOffset(fragment, "EndFragment") - cfHtmlOffset(fragment, "StartFragment"))) ==
        "<b>x</b>");
  CHECK(GetClipboardData(RegisterClipboardFormat(L"MeOS private")) == nullptr);
  CHECK(EmptyClipboard());
  CHECK(GetClipboardData(CF_UNICODETEXT) == nullptr);
  const char url[] = "http://localhost:2009/meos";
  CHECK(SetClipboardData(CF_TEXT, globalCopy(url, sizeof(url))) != nullptr);
  CHECK(CloseClipboard());
  CHECK(qtClipboard->text() == QString::fromLatin1(url) && !qtClipboard->mimeData()->hasHtml());

  // A registered format travels as it is; without EmptyClipboard the text stays.
  const UINT privateFormat = RegisterClipboardFormat(L"MeOS private");
  CHECK(OpenClipboard(owner));
  CHECK(SetClipboardData(privateFormat, globalCopy("\x01\x02\x00\x03", 4)) != nullptr);
  CHECK(CloseClipboard());
  CHECK(qtClipboard->text() == QString::fromLatin1(url));
  CHECK(OpenClipboard(owner));
  CHECK(blockBytes(GetClipboardData(privateFormat)) == std::string("\x01\x02\x00\x03", 4));
  CHECK(EmptyClipboard() && CloseClipboard());
  CHECK(qtClipboard->mimeData() == nullptr || qtClipboard->mimeData()->formats().isEmpty());

  DestroyWindow(owner);
}

/* ---------------------------------------------------------------------
   Message boxes
   --------------------------------------------------------------------- */

HWND hookedBox = nullptr;
int activations = 0;
std::wstring renameYes;
std::wstring renameNo;
SIZE idealYes = {};

// The hook of gdioutput::ask: renames buttons and widens them to the left.
LRESULT CALLBACK cbtHook(int code, WPARAM wParam, LPARAM lParam) {
  if (code == HCBT_ACTIVATE) {
    activations++;
    hookedBox = reinterpret_cast<HWND>(wParam);
    // Shown, but the focus is still in the owner.
    CHECK(IsWindowVisible(hookedBox) && GetFocus() != GetDlgItem(hookedBox, IDYES));
    CHECK(reinterpret_cast<const CBTACTIVATESTRUCT *>(lParam)->fMouse == FALSE);
    int movDiff = 0;
    auto update = [&movDiff, wParam](int id, const std::wstring &text) {
      HWND button = GetDlgItem((HWND)wParam, id);
      if (text != L"@")
        SetWindowText(button, text.c_str());
      SIZE sz;
      RECT rc;
      GetWindowRect(button, &rc);
      if (text != L"@") {
        Button_GetIdealSize(button, &sz);
        if (id == IDYES)
          idealYes = sz;
      }
      else {
        sz.cx = rc.right - rc.left;
        sz.cy = rc.bottom - rc.top;
      }
      POINT pt = {rc.left, rc.top};
      ScreenToClient((HWND)wParam, &pt);
      int wdActual = rc.right - rc.left;
      if (wdActual < sz.cx) {
        movDiff += sz.cx - wdActual;
        SetWindowPos(button, nullptr, pt.x - movDiff, pt.y, sz.cx, rc.bottom - rc.top, SWP_NOZORDER);
      }
      else if (movDiff > 0) {
        SetWindowPos(button, nullptr, pt.x - movDiff, pt.y, sz.cx, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOSIZE);
      }
    };
    if (!renameNo.empty())
      update(IDNO, renameNo);
    if (!renameYes.empty())
      update(IDYES, renameYes);
  }
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

bool clickButton(HWND box, int id) {
  const auto button = widgetOf<QAbstractButton>(GetDlgItem(box, id));
  if (!button)
    return false;
  button->click();
  return true;
}

HWND findMessageBox() {
  for (const std::shared_ptr<meos_qt::Window> &window : meos_qt::allWindows()) {
    if (window->windowClass && window->windowClass->name == L"#32770" && !window->destroying)
      return window->handle;
  }
  return nullptr;
}

void testMessageBox() {
  const HWND owner = createOwner();
  const HWND edit = CreateWindowEx(0, L"Edit", L"", WS_CHILD | WS_VISIBLE, 10, 10, 100, 20, owner,
                                   reinterpret_cast<HMENU>(INT_PTR(7)), meos_qt::applicationInstance(), nullptr);
  SetFocus(edit);
  CHECK(GetFocus() == edit);

  // ask() with renamed buttons, closed by a click on No. A message posted meanwhile
  // is dispatched inside the modal loop.
  const HHOOK hook = SetWindowsHookEx(WH_CBT, cbtHook, nullptr, GetCurrentThreadId());
  renameYes = L"Spara och stäng tävlingen";
  renameNo = L"@";
  bool checkedInside = false;
  bool posted = false;
  SetCapture(owner);
  whenReady([&] {
    if (!hookedBox || !IsWindowVisible(hookedBox))
      return false;
    if (!posted) {
      PostMessage(owner, WM_USER + 5, 0, 0);
      posted = true;
      return false;
    }
    if (countOf(WM_USER + 5) == 0)
      return false;
    const HWND yes = GetDlgItem(hookedBox, IDYES), no = GetDlgItem(hookedBox, IDNO);
    CHECK(yes && no && !GetDlgItem(hookedBox, IDCANCEL));
    CHECK(windowText(hookedBox) == L"MeOS");
    CHECK(windowText(yes) == renameYes && windowText(no) == widgetOf<QAbstractButton>(no)->text().toStdWString());
    CHECK(idealYes.cx > 88 && windowRect(yes).width() == idealYes.cx);
    CHECK(windowRect(yes).right() < windowRect(no).left());
    POINT origin = {0, 0};
    ClientToScreen(hookedBox, &origin);
    CHECK(windowRect(yes).left() >= origin.x);
    CHECK(GetFocus() == yes && GetCapture() == nullptr);
    checkedInside = true;
    return clickButton(hookedBox, IDNO);
  });
  CHECK(MessageBox(owner, L"Vill du spara \u00E4ndringarna i t\u00E4vlingen innan programmet avslutas?", L"MeOS",
                   MB_YESNO | MB_ICONQUESTION) == IDNO);
  CHECK(checkedInside && activations == 1);
  CHECK(!meos_qt::findWindow(hookedBox) && GetFocus() == edit);
  UnhookWindowsHookEx(hook);
  renameYes.clear();
  renameNo.clear();

  // Keys: Escape does nothing without Cancel or a lone OK; Tab and the arrows move
  // between the buttons; Enter chooses the focused button.
  whenReady([&] {
    const HWND box = findMessageBox();
    if (!box || !IsWindowVisible(box))
      return false;
    CHECK(GetFocus() == GetDlgItem(box, IDYES));
    sendKey(clientOf(GetFocus()), Qt::Key_Escape);
    CHECK(meos_qt::findWindow(box) != nullptr);
    sendKey(clientOf(GetFocus()), Qt::Key_Tab);
    CHECK(GetFocus() == GetDlgItem(box, IDNO));
    sendKey(clientOf(GetFocus()), Qt::Key_Right);
    CHECK(GetFocus() == GetDlgItem(box, IDYES));
    sendKey(clientOf(GetFocus()), Qt::Key_Left);
    CHECK(GetFocus() == GetDlgItem(box, IDNO));
    sendKey(clientOf(GetFocus()), Qt::Key_Return);
    return true;
  });
  CHECK(MessageBox(owner, L"Keys", L"MeOS", MB_YESNO) == IDNO);

  whenReady([&] {
    const HWND box = findMessageBox();
    if (!box || !IsWindowVisible(box))
      return false;
    sendKey(clientOf(GetFocus()), Qt::Key_Escape);
    return true;
  });
  CHECK(MessageBox(owner, L"Cancel", L"MeOS", MB_YESNOCANCEL | MB_ICONQUESTION) == IDCANCEL);

  whenReady([&] {
    const HWND box = findMessageBox();
    if (!box || !IsWindowVisible(box))
      return false;
    CHECK(!GetDlgItem(box, IDCANCEL) && GetDlgItem(box, IDOK));
    SendMessage(box, WM_CLOSE, 0, 0);
    return true;
  });
  CHECK(MessageBox(owner, L"Only OK", nullptr, MB_OK | MB_ICONINFORMATION) == IDOK);

  whenReady([&] {
    const HWND box = findMessageBox();
    if (!box || !IsWindowVisible(box))
      return false;
    CHECK(windowText(box) == L"Error"); // the default caption
    sendKey(clientOf(GetFocus()), Qt::Key_Return);
    return true;
  });
  CHECK(MessageBox(nullptr, L"No owner, Enter chooses OK", nullptr, MB_OKCANCEL) == IDOK);

  // A long text makes a wide and high box, as wide as the screen allows.
  whenReady([&] {
    const HWND box = findMessageBox();
    if (!box || !IsWindowVisible(box))
      return false;
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    CHECK(windowRect(box).width() > 300 && windowRect(box).width() <= screen.width());
    CHECK(screen.contains(windowRect(box)));
    return clickButton(box, IDOK);
  });
  std::wstring longText;
  for (int i = 0; i < 80; i++)
    longText += L"Klassen har inga str\u00E4ckor. ";
  CHECK(MessageBox(owner, longText.c_str(), L"MeOS", MB_OK) == IDOK);

  SetLastError(0);
  CHECK(MessageBox(owner, L"Bad style", L"MeOS", 0x0000000F) == 0 && GetLastError() == 1438);

  // From another thread (the SI reader): the GUI thread shows the box.
  std::atomic<int> threadResult{-1};
  std::thread worker([&] { threadResult = MessageBox(nullptr, L"From a thread", nullptr, MB_OK); });
  whenReady([&] {
    const HWND box = findMessageBox();
    return box && IsWindowVisible(box) && clickButton(box, IDOK);
  });
  CHECK(waitFor([&] { return threadResult != -1; }));
  worker.join();
  CHECK(threadResult == IDOK);

  // The owner is destroyed while its box is open.
  whenReady([&] {
    const HWND box = findMessageBox();
    if (!box || !IsWindowVisible(box))
      return false;
    DestroyWindow(owner);
    return true;
  });
  CHECK(MessageBox(owner, L"Owner goes away", L"MeOS", MB_OKCANCEL) == 0);
  CHECK(!meos_qt::findWindow(owner) && !findMessageBox());
  CHECK(MessageBeep(MB_OK) == TRUE);
}

/* ---------------------------------------------------------------------
   File, folder and colour dialogs
   --------------------------------------------------------------------- */

const wchar_t filterSpec[] = L"Textfil\0*.txt\0Webbdokument\0*.html;*.htm\0Alla filer\0*.*\0";

OPENFILENAME fileDialogSetup(HWND owner, wchar_t *buffer, DWORD size, DWORD filterIndex, LPCWSTR defExt) {
  OPENFILENAME of = {};
  of.lStructSize = sizeof(of);
  of.hwndOwner = owner;
  of.lpstrFilter = filterSpec;
  of.nFilterIndex = filterIndex;
  of.lpstrFile = buffer;
  of.nMaxFile = size;
  of.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
  of.lpstrDefExt = defExt;
  return of;
}

QComboBox *fileTypeCombo(QFileDialog *dialog) {
  return dialog->findChild<QComboBox *>(QStringLiteral("fileTypeCombo"));
}

// Types a name into the file name field. (QFileDialog::selectFile leaves the field
// alone while it has the focus, which depends on the timing of the activation.)
void typeFileName(QFileDialog *dialog, const QString &name) {
  const auto edit = dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
  CHECK(edit != nullptr);
  if (edit)
    edit->setText(name);
}

void testFileDialogs() {
  const QStringList filters = meos_qt::fileDialogFilters(filterSpec);
  CHECK(filters == QStringList({QStringLiteral("Textfil (*.txt)"), QStringLiteral("Webbdokument (*.html *.htm)"),
                                QStringLiteral("Alla filer (*)")}));
  CHECK(meos_qt::fileDialogFilters(nullptr).isEmpty() && meos_qt::fileDialogFilters(L"").isEmpty());

  QTemporaryDir directory;
  CHECK(directory.isValid());
  const QString base = directory.path();
  const HWND owner = createOwner();

  // Save: the user switches to the second filter and types a name without extension.
  wchar_t file[MAX_PATH] = {};
  OPENFILENAME of = fileDialogSetup(owner, file, MAX_PATH, 1, L"csv");
  whenReady([&] {
    const auto dialog = activeModal<QFileDialog>();
    if (!dialog)
      return false;
    CHECK(dialog->acceptMode() == QFileDialog::AcceptSave);
    CHECK(dialog->nameFilters() == filters && dialog->selectedNameFilter() == filters[0]);
    CHECK(dialog->defaultSuffix() == QStringLiteral("txt"));
    dialog->setDirectory(base);
    if (QComboBox *combo = fileTypeCombo(dialog)) {
      combo->setFocus();
      sendKey(combo, Qt::Key_Down);
    }
    CHECK(dialog->selectedNameFilter() == filters[1] && dialog->defaultSuffix() == QStringLiteral("html"));
    typeFileName(dialog, QStringLiteral("result"));
    accept(dialog);
    return true;
  });
  CHECK(GetSaveFileName(&of) == TRUE && CommDlgExtendedError() == 0);
  const std::wstring expected = (base + QStringLiteral("/result.html")).toStdWString();
  CHECK(file == expected && of.nFilterIndex == 2);
  CHECK(of.nFileOffset == expected.rfind(L'/') + 1 && of.nFileExtension == expected.size() - 4);

  // "All files" has no extension of its own: lpstrDefExt is appended.
  std::fill(std::begin(file), std::end(file), 0);
  of = fileDialogSetup(owner, file, MAX_PATH, 3, L"csv");
  whenReady([&] {
    const auto dialog = activeModal<QFileDialog>();
    if (!dialog)
      return false;
    CHECK(dialog->selectedNameFilter() == filters[2] && dialog->defaultSuffix() == QStringLiteral("csv"));
    dialog->setDirectory(base);
    typeFileName(dialog, QStringLiteral("list"));
    accept(dialog);
    return true;
  });
  CHECK(GetSaveFileName(&of) == TRUE && file == (base + QStringLiteral("/list.csv")).toStdWString());
  CHECK(of.nFilterIndex == 3);

  // A buffer too small and a cancelled dialog.
  wchar_t small[8] = {};
  of = fileDialogSetup(owner, small, 8, 1, nullptr);
  whenReady([&] {
    const auto dialog = activeModal<QFileDialog>();
    if (!dialog)
      return false;
    CHECK(dialog->defaultSuffix().isEmpty());
    dialog->setDirectory(base);
    typeFileName(dialog, QStringLiteral("a-long-file-name.txt"));
    accept(dialog);
    return true;
  });
  CHECK(GetSaveFileName(&of) == FALSE && CommDlgExtendedError() == FNERR_BUFFERTOOSMALL);
  CHECK(small[0] == wchar_t((base + QStringLiteral("/a-long-file-name.txt")).size() + 1));

  of = fileDialogSetup(owner, file, MAX_PATH, 1, L"");
  whenReady([&] {
    const auto dialog = activeModal<QFileDialog>();
    if (!dialog)
      return false;
    dialog->reject();
    return true;
  });
  CHECK(GetOpenFileName(&of) == FALSE && CommDlgExtendedError() == 0);

  // Open an existing file.
  QFile data(base + QStringLiteral("/data.xml"));
  CHECK(data.open(QIODevice::WriteOnly) && data.write("<xml/>") > 0);
  data.close();
  std::fill(std::begin(file), std::end(file), 0);
  of = fileDialogSetup(owner, file, MAX_PATH, 3, L"xml");
  of.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
  whenReady([&] {
    const auto dialog = activeModal<QFileDialog>();
    if (!dialog)
      return false;
    CHECK(dialog->acceptMode() == QFileDialog::AcceptOpen && dialog->fileMode() == QFileDialog::ExistingFile);
    dialog->setDirectory(base);
    typeFileName(dialog, QStringLiteral("data.xml"));
    accept(dialog);
    return true;
  });
  CHECK(GetOpenFileName(&of) == TRUE && file == data.fileName().toStdWString());

  // The folder browser starts in the folder MeOS passes in pszDisplayName.
  CHECK(QDir(base).mkdir(QStringLiteral("Resultat")));
  wchar_t folder[MAX_PATH] = {};
  const std::wstring start = base.toStdWString();
  std::wmemcpy(folder, start.c_str(), start.size() + 1);
  BROWSEINFO bi = {};
  bi.hwndOwner = owner;
  bi.pszDisplayName = folder;
  bi.lpszTitle = L"Välj katalog";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX | BIF_NEWDIALOGSTYLE;
  CHECK(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) == S_OK);
  whenReady([&] {
    const auto dialog = activeModal<QFileDialog>();
    if (!dialog)
      return false;
    CHECK(dialog->fileMode() == QFileDialog::Directory && QDir(dialog->directory()) == QDir(base));
    CHECK(dialog->windowTitle() == QString::fromStdWString(bi.lpszTitle));
    typeFileName(dialog, QStringLiteral("Resultat"));
    accept(dialog);
    return true;
  });
  const LPITEMIDLIST idList = SHBrowseForFolder(&bi);
  CHECK(idList != nullptr && std::wstring(folder) == L"Resultat");
  wchar_t path[MAX_PATH] = {};
  CHECK(SHGetPathFromIDList(idList, path) && path == (base + QStringLiteral("/Resultat")).toStdWString());
  LPMALLOC allocator = nullptr;
  CHECK(SHGetMalloc(&allocator) == S_OK && allocator);
  allocator->Free(idList);
  allocator->Release();

  whenReady([&] {
    const auto dialog = activeModal<QFileDialog>();
    if (!dialog)
      return false;
    dialog->reject();
    return true;
  });
  CHECK(SHBrowseForFolder(&bi) == nullptr);

  // Colours: the initial colour and the custom colours go both ways.
  COLORREF custom[16] = {};
  custom[3] = RGB(1, 2, 3);
  CHOOSECOLOR cc = {};
  cc.lStructSize = sizeof(cc);
  cc.hwndOwner = owner;
  cc.rgbResult = RGB(10, 20, 30);
  cc.Flags = CC_RGBINIT;
  cc.lpCustColors = custom;
  whenReady([&] {
    const auto dialog = activeModal<QColorDialog>();
    if (!dialog)
      return false;
    CHECK(dialog->currentColor() == QColor(10, 20, 30));
    CHECK(QColorDialog::customColor(3) == QColor(1, 2, 3));
    QColorDialog::setCustomColor(5, QColor(9, 8, 7));
    dialog->setCurrentColor(QColor(200, 100, 50));
    accept(dialog);
    return true;
  });
  CHECK(ChooseColor(&cc) == TRUE && cc.rgbResult == RGB(200, 100, 50));
  CHECK(custom[3] == RGB(1, 2, 3) && custom[5] == RGB(9, 8, 7));
  whenReady([&] {
    const auto dialog = activeModal<QColorDialog>();
    if (!dialog)
      return false;
    dialog->reject();
    return true;
  });
  CHECK(ChooseColor(&cc) == FALSE && cc.rgbResult == RGB(200, 100, 50) && CommDlgExtendedError() == 0);
  cc.lStructSize = 1;
  CHECK(ChooseColor(&cc) == FALSE && CommDlgExtendedError() == CDERR_STRUCTSIZE);

  DestroyWindow(owner);
}

/* ---------------------------------------------------------------------
   Popup menus
   --------------------------------------------------------------------- */

void testMenus() {
  const HWND owner = createOwner();
  const HMENU menu = CreatePopupMenu();
  CHECK(menu && AppendMenu(menu, MF_STRING, 1, L"Sortera stigande"));
  CHECK(AppendMenu(menu, MF_SEPARATOR, 0, L""));
  CHECK(AppendMenu(menu, MF_STRING, 7, L"&Kopiera\tCtrl+C"));
  CHECK(!AppendMenu(reinterpret_cast<HMENU>(std::intptr_t(0x7777)), MF_STRING, 2, L"x"));

  // gdioutput::popupMenu: the chosen command is returned.
  SetCapture(owner);
  whenReady([&] {
    const auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (!popup || !popup->isVisible())
      return false;
    const QList<QAction *> actions = popup->actions();
    CHECK(actions.size() == 3 && actions[1]->isSeparator());
    CHECK(actions[0]->text() == QStringLiteral("Sortera stigande") &&
          actions[2]->text() == QStringLiteral("&Kopiera\tCtrl+C"));
    CHECK(GetCapture() == nullptr);
    popup->setActiveAction(actions[2]);
    sendKey(popup, Qt::Key_Return);
    return true;
  });
  POINT point = {20, 30};
  ClientToScreen(owner, &point);
  CHECK(TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, owner,
                         nullptr) == 7);

  whenReady([&] {
    const auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (!popup || !popup->isVisible())
      return false;
    sendKey(popup, Qt::Key_Escape);
    return true;
  });
  CHECK(TrackPopupMenuEx(menu, TPM_RETURNCMD, point.x, point.y, owner, nullptr) == 0);

  // Without TPM_RETURNCMD the command is posted to the window.
  ownerMessages.clear();
  whenReady([&] {
    const auto popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (!popup || !popup->isVisible())
      return false;
    popup->setActiveAction(popup->actions()[0]);
    sendKey(popup, Qt::Key_Return);
    return true;
  });
  CHECK(TrackPopupMenuEx(menu, 0, point.x, point.y, owner, nullptr) == TRUE);
  CHECK(waitFor([] { return countOf(WM_COMMAND) == 1; }));

  CHECK(DestroyMenu(menu) && !DestroyMenu(menu));
  CHECK(!TrackPopupMenuEx(menu, TPM_RETURNCMD, 0, 0, owner, nullptr));
  DestroyWindow(owner);
}

/* ---------------------------------------------------------------------
   Shell, screen, window placement and printing
   --------------------------------------------------------------------- */

void testShellExecute() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const QString base = directory.path();

  CHECK(intptr_t(ShellExecute(nullptr, L"open", L"/nonexistent-meos/file.txt", nullptr, nullptr, SW_SHOWNORMAL)) ==
        ERROR_PATH_NOT_FOUND);
  const std::wstring missing = (base + QStringLiteral("/missing.txt")).toStdWString();
  CHECK(intptr_t(ShellExecute(nullptr, L"open", missing.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) ==
        ERROR_FILE_NOT_FOUND);
  CHECK(intptr_t(ShellExecute(nullptr, L"print", missing.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32);
  CHECK(intptr_t(ShellExecute(nullptr, L"open", L"", nullptr, nullptr, SW_SHOWNORMAL)) <= 32);

  // An export script with parameters (onlineresults.cpp), started in its folder.
  QFile script(base + QStringLiteral("/export.sh"));
  CHECK(script.open(QIODevice::WriteOnly));
  script.write("#!/bin/sh\necho \"$1|$2|$(pwd)\" > out.txt\n");
  script.close();
  script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  std::wstring windowsPath = script.fileName().toStdWString();
  std::replace(windowsPath.begin(), windowsPath.end(), L'/', L'\\');
  const std::wstring folder = base.toStdWString();
  CHECK(intptr_t(ShellExecute(nullptr, nullptr, windowsPath.c_str(), L"\"first arg\" second", folder.c_str(),
                              SW_HIDE)) > 32);
  const QString output = base + QStringLiteral("/out.txt");
  CHECK(waitFor([&] {
    QFile out(output);
    return out.open(QIODevice::ReadOnly) && out.readAll().endsWith('\n');
  }));
  QFile out(output);
  CHECK(out.open(QIODevice::ReadOnly) &&
        QString::fromUtf8(out.readAll()).trimmed() ==
            QStringLiteral("first arg|second|") + QDir(base).canonicalPath());

  // A relative program name is found in the given folder.
  QFile::remove(output);
  CHECK(intptr_t(ShellExecute(nullptr, L"open", L"export.sh", L"x", folder.c_str(), SW_HIDE)) > 32);
  CHECK(waitFor([&] { return QFile::exists(output); }));
}

std::vector<RECT> monitorRects;

BOOL CALLBACK monitorProc(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data) {
  CHECK(monitor != nullptr && dc == nullptr && data == 42);
  monitorRects.push_back(*rect);
  return TRUE;
}

BOOL CALLBACK stopAtFirst(HMONITOR, HDC, LPRECT rect, LPARAM) {
  monitorRects.push_back(*rect);
  return FALSE;
}

void testScreen() {
  const QRect primary = QGuiApplication::primaryScreen()->geometry();
  CHECK(GetSystemMetrics(SM_CXSCREEN) == primary.width() && GetSystemMetrics(SM_CYSCREEN) == primary.height());
  CHECK(GetSystemMetrics(SM_CXVIRTUALSCREEN) >= primary.width() &&
        GetSystemMetrics(SM_CYVIRTUALSCREEN) >= primary.height());
  CHECK(GetSystemMetrics(SM_CXEDGE) == 2 && GetSystemMetrics(SM_CYEDGE) == 2);

  CHECK(EnumDisplayMonitors(nullptr, nullptr, monitorProc, 42));
  CHECK(int(monitorRects.size()) == QGuiApplication::screens().size());
  CHECK(!monitorRects.empty() && monitorRects[0].left == primary.left() && monitorRects[0].top == primary.top() &&
        monitorRects[0].right == primary.left() + primary.width() &&
        monitorRects[0].bottom == primary.top() + primary.height());
  monitorRects.clear();
  CHECK(EnumDisplayMonitors(nullptr, nullptr, stopAtFirst, 0) && monitorRects.size() == 1);
  monitorRects.clear();
  RECT outside = {-5000, -5000, -4000, -4000};
  CHECK(EnumDisplayMonitors(nullptr, &outside, monitorProc, 42) && monitorRects.empty());

  // gdioutput::getWindowsPosition/setWindowsPosition and the toolbar floater.
  const HWND window = createOwner(WS_POPUP | WS_CAPTION | WS_VISIBLE, 100, 120, 400, 300);
  WINDOWPLACEMENT placement = {};
  placement.length = sizeof(placement);
  CHECK(GetWindowPlacement(window, &placement));
  RECT rect;
  GetWindowRect(window, &rect);
  CHECK(placement.showCmd == SW_SHOWNORMAL && EqualRect(&placement.rcNormalPosition, &rect));

  placement.rcNormalPosition = RECT{50, 60, 550, 460};
  CHECK(SetWindowPlacement(window, &placement));
  GetWindowRect(window, &rect);
  CHECK(EqualRect(&placement.rcNormalPosition, &rect) && IsWindowVisible(window));

  ShowWindow(window, SW_MAXIMIZE);
  CHECK(waitFor([&] { return meos_qt::findWindow(window)->frame->isMaximized(); }));
  WINDOWPLACEMENT maximized = {};
  maximized.length = sizeof(maximized);
  CHECK(GetWindowPlacement(window, &maximized) && maximized.showCmd == SW_SHOWMAXIMIZED);
  CHECK(EqualRect(&maximized.rcNormalPosition, &placement.rcNormalPosition));

  placement.rcNormalPosition = RECT{70, 80, 370, 280};
  placement.showCmd = SW_SHOWNORMAL;
  CHECK(SetWindowPlacement(window, &placement));
  CHECK(waitFor([&] { return !meos_qt::findWindow(window)->frame->isMaximized(); }));
  GetWindowRect(window, &rect);
  CHECK(EqualRect(&placement.rcNormalPosition, &rect));

  placement.length = 0;
  SetLastError(0);
  CHECK(!SetWindowPlacement(window, &placement) && GetLastError() == ERROR_INVALID_PARAMETER);

  // A hidden window is shown; a child window reports parent client coordinates.
  const HWND hidden = createOwner(WS_POPUP, 10, 10, 100, 100);
  placement.length = sizeof(placement);
  placement.rcNormalPosition = RECT{20, 30, 220, 130};
  CHECK(SetWindowPlacement(hidden, &placement) && IsWindowVisible(hidden));
  const HWND child = CreateWindowEx(0, ownerClass, L"", WS_CHILD | WS_VISIBLE, 15, 25, 60, 40, window, nullptr,
                                    meos_qt::applicationInstance(), nullptr);
  CHECK(GetWindowPlacement(child, &placement));
  CHECK(placement.rcNormalPosition.left == 15 && placement.rcNormalPosition.top == 25 &&
        placement.rcNormalPosition.right == 75 && placement.rcNormalPosition.bottom == 65);
  DestroyWindow(hidden);
  DestroyWindow(window);
}

void testPrinting() {
  PRINTDLG pd = {};
  pd.lStructSize = sizeof(pd);
  pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_PRINTSETUP;
  pd.hDC = reinterpret_cast<HDC>(std::intptr_t(1));
  CHECK(PrintDlg(&pd) == FALSE && CommDlgExtendedError() == PDERR_NODEFAULTPRN && pd.hDC == nullptr);
  PAGESETUPDLG psd = {};
  psd.lStructSize = sizeof(psd);
  CHECK(PageSetupDlg(&psd) == FALSE && CommDlgExtendedError() == PDERR_NODEFAULTPRN);
  CHECK(CreateDC(L"WINSPOOL", L"Printer", nullptr, nullptr) == nullptr);
  DOCINFO doc = {sizeof(DOCINFO), L"MeOS", nullptr, nullptr, 0};
  CHECK(StartDoc(nullptr, &doc) <= 0 && StartPage(nullptr) <= 0 && EndPage(nullptr) <= 0 && EndDoc(nullptr) <= 0);

  const HDC dc = CreateCompatibleDC(nullptr);
  const HBITMAP bitmap = CreateCompatibleBitmap(dc, 40, 30);
  const HGDIOBJ old = SelectObject(dc, bitmap);
  CHECK(GetDeviceCaps(dc, HORZRES) == 40 && GetDeviceCaps(dc, VERTRES) == 30);
  CHECK(GetDeviceCaps(dc, PHYSICALWIDTH) == 0 && GetDeviceCaps(nullptr, HORZRES) == 0);
  CHECK(SetMapMode(dc, MM_ISOTROPIC) == 0 && SetMapMode(nullptr, MM_ISOTROPIC) == 0);
  SIZE previous = {};
  CHECK(SetWindowExtEx(dc, 100, 100, &previous) && previous.cx == 1 && !SetViewportExtEx(nullptr, 1, 1, nullptr));
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: meos_dialogs_selftest <code directory>\n");
    return 2;
  }
  const std::filesystem::path codeDir = argv[1];
  const std::unique_ptr<QApplication> app = meos_qt::createApplication(argc, argv);

  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = ownerProc;
  wc.hInstance = meos_qt::applicationInstance();
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = ownerClass;
  if (!RegisterClassEx(&wc)) {
    std::fprintf(stderr, "cannot register the owner class\n");
    return 1;
  }

  testResources(codeDir);
  testGlobalMemory();
  testClipboard();
  testMessageBox();
  testFileDialogs();
  testMenus();
  testShellExecute();
  testScreen();
  testPrinting();

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("dialogs self test passed\n");
  return 0;
}
