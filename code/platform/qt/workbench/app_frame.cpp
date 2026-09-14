/************************************************************************
    MeOS - Orienteering Software
    Linux port: the application frame of the GUI workbench and the load test.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Stand-ins for the definitions in meos.cpp, see app_frame.h. Where a function exists
// there, the implementation follows it closely; code/meos.cpp is the reference when
// an upstream drop changes it.

#include "stdafx.h"

#include "app_frame.h"

#include <shlobj.h>

#include <cassert>
#include <cstdio>
#include <set>

#include "autocomplete.h"
#include "gdiconstants.h"
#include "gdioutput.h"
#include "image.h"
#include "localizer.h"
#include "meos_util.h"
#include "meosexception.h"
#include "metalist.h"
#include "oEvent.h"
#include "progress.h"
#include "random.h"
#include "restserver.h"
#include "xmlparser.h"

class SportIdent;

/* ---------------------------------------------------------------------
   Globals of meos.cpp
   --------------------------------------------------------------------- */

int defaultCodePage = 1252;

Image image;
gdioutput *gdi_main = nullptr;
oEvent *gEvent = nullptr;
SportIdent *gSI = nullptr;
Localizer lang;
bool enableTests = false;

std::vector<gdioutput *> gdi_extra;

HWND hWndMain = nullptr;
HINSTANCE hInst = nullptr;

wchar_t programPath[MAX_PATH];
wchar_t exePath[MAX_PATH];

namespace {

wchar_t settingsFile[MAX_PATH];
std::wstring userFolder;
size_t currentFocusIx = 0;
HHOOK keyboardHook = nullptr;

std::set<std::wstring> tempFiles;
std::wstring tempPath;

void removeTempFiles() {
  std::vector<std::wstring> dir;
  for (const std::wstring &file : tempFiles) {
    wchar_t c = *file.rbegin();
    if (c == '/' || c == '\\')
      dir.push_back(file);
    else
      DeleteFile(file.c_str());
  }
  tempFiles.clear();
  bool removed = true;
  while (removed) {
    removed = false;
    for (size_t k = 0; k < dir.size(); k++) {
      if (!dir[k].empty() && RemoveDirectory(dir[k].c_str()) != 0) {
        removed = true;
        dir[k].clear();
      }
    }
  }

  if (!tempPath.empty()) {
    RemoveDirectory(tempPath.c_str());
    tempPath.clear();
  }
}

void scrollVertical(gdioutput *gdi, int yInc, HWND hWnd) {
  SCROLLINFO si;
  si.cbSize = sizeof(si);
  si.fMask = SIF_ALL;
  GetScrollInfo(hWnd, SB_VERT, &si);
  if (si.nPage == 0)
    yInc = 0;

  int yPos = gdi->getOffsetY();
  int a = si.nMax - signed(si.nPage - 1) - yPos;

  if ((yInc = max(-yPos, min(yInc, a))) != 0) {
    yPos += yInc;
    RECT ScrollArea, ClipArea;
    GetClientRect(hWnd, &ScrollArea);
    ClipArea = ScrollArea;

    ScrollArea.top = -gdi->getHeight() - 100;
    ScrollArea.bottom += gdi->getHeight();
    ScrollArea.right = gdi->getWidth() - gdi->getOffsetX() + 15;
    ScrollArea.left = -2000;
    gdi->setOffsetY(yPos);

    RECT invalidArea;
    ScrollWindowEx(hWnd, 0, -yInc, &ScrollArea, &ClipArea, (HRGN)NULL, &invalidArea,
                   SW_SCROLLCHILDREN | SW_INVALIDATE);

    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = yPos;

    SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
    UpdateWindow(hWnd);
  }
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code < 0)
    return CallNextHookEx(0, code, wParam, lParam);

  gdioutput *gdi = nullptr;
  if (currentFocusIx < gdi_extra.size())
    gdi = gdi_extra[currentFocusIx];
  if (!gdi)
    gdi = gdi_main;

  HWND hWnd = gdi ? gdi->getHWNDTarget() : 0;

  bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) == 0x8000;
  bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) == 0x8000;

  if (wParam == VK_TAB) {
    if ((lParam & (1 << 31))) {
      SHORT state = GetKeyState(VK_SHIFT);
      if (gdi) {
        if (state & (1 << 16))
          gdi->TabFocus(-1);
        else
          gdi->TabFocus(1);
      }
    }
    return 1;
  }
  else if (wParam == VK_RETURN && (lParam & (1 << 31))) {
    if (gdi)
      gdi->enter();
  }
  else if (wParam == VK_UP) {
    bool c = false;
    if (gdi && (lParam & (1 << 31)))
      c = gdi->upDown(1);

    if (gdi && gdi->hasAutoComplete())
      return 1;

    if (!c && !(lParam & (1 << 31)) && !(gdi && gdi->lockUpDown))
      SendMessage(hWnd, WM_VSCROLL, MAKELONG(SB_LINEUP, 0), 0);
  }
  else if (wParam == VK_NEXT && !(lParam & (1 << 31)) && !(gdi && gdi->lockUpDown)) {
    SendMessage(hWnd, WM_VSCROLL, MAKELONG(SB_PAGEDOWN, 0), 0);
  }
  else if (wParam == VK_PRIOR && !(lParam & (1 << 31)) && !(gdi && gdi->lockUpDown)) {
    SendMessage(hWnd, WM_VSCROLL, MAKELONG(SB_PAGEUP, 0), 0);
  }
  else if (wParam == VK_DOWN) {
    bool c = false;
    if (gdi && (lParam & (1 << 31)))
      c = gdi->upDown(-1);

    if (gdi && gdi->hasAutoComplete())
      return 1;

    if (!c && !(lParam & (1 << 31)) && !(gdi && gdi->lockUpDown))
      SendMessage(hWnd, WM_VSCROLL, MAKELONG(SB_LINEDOWN, 0), 0);
  }
  else if (wParam == VK_LEFT && !(lParam & (1 << 31))) {
    if (!gdi || !gdi->hasEditControl())
      SendMessage(hWnd, WM_HSCROLL, MAKELONG(SB_LINEUP, 0), 0);
  }
  else if (wParam == VK_RIGHT && !(lParam & (1 << 31))) {
    if (!gdi || !gdi->hasEditControl())
      SendMessage(hWnd, WM_HSCROLL, MAKELONG(SB_LINEDOWN, 0), 0);
  }
  else if (wParam == VK_ESCAPE && (lParam & (1 << 31))) {
    if (gdi)
      gdi->escape();
  }
  else if (wParam == VK_F2) {
    ProgressWindow pw(hWnd, 1.0);
    pw.init();
    for (int k = 0; k <= 20; k++) {
      pw.setProgress(k * 50);
      Sleep(100);
    }
  }
  else if (ctrlPressed && (wParam == VK_ADD || wParam == VK_SUBTRACT || wParam == VK_F5 || wParam == VK_F6)) {
    if (gdi) {
      if (wParam == VK_ADD || wParam == VK_F5)
        gdi->scaleSize(1.1);
      else
        gdi->scaleSize(1.0 / 1.1);
    }
  }
  else if (wParam == 'C' && ctrlPressed) {
    if (gdi)
      gdi->keyCommand(KC_COPY);
  }
  else if (wParam == 'V' && ctrlPressed) {
    if (gdi)
      gdi->keyCommand(KC_PASTE);
  }
  else if (wParam == 'F' && ctrlPressed) {
    if (gdi)
      gdi->keyCommand(shiftPressed ? KC_FINDBACK : KC_FIND);
  }
  else if (wParam == 'A' && ctrlPressed) {
    if (gdi)
      gdi->keyCommand(KC_MARKALL);
  }
  else if (wParam == 'D' && ctrlPressed) {
    if (gdi)
      gdi->keyCommand(KC_CLEARALL);
  }
  else if (wParam == VK_DELETE) {
    if (gdi)
      gdi->keyCommand(KC_DELETE);
  }
  else if (wParam == 'I' && ctrlPressed) {
    if (gdi)
      gdi->keyCommand(KC_INSERT);
  }
  else if (wParam == 'P' && ctrlPressed) {
    if (gdi)
      gdi->keyCommand(KC_PRINT);
  }
  else if (wParam == VK_F5 && !ctrlPressed) {
    if (gdi)
      gdi->keyCommand(KC_REFRESH);
  }
  else if (wParam == 'M' && ctrlPressed) {
    if (gdi)
      gdi->keyCommand(KC_SPEEDUP);
  }
  else if (wParam == 'N' && ctrlPressed) {
    if (gdi)
      gdi->keyCommand(KC_SLOWDOWN);
  }
  else if (wParam == ' ' && ctrlPressed) {
    if (gdi)
      gdi->keyCommand(KC_AUTOCOMPLETE);
  }

  return 0;
}

} // namespace

/* ---------------------------------------------------------------------
   Functions of meos.cpp
   --------------------------------------------------------------------- */

// No auto tasks run in a tool.
void resetSaveTimer() {}

// Tools have no accelerator table; MeOS calls this with hAccelTable == 0 anyway.
void mainMessageLoop(HACCEL /*hAccelTable*/, DWORD time) {
  MSG msg;
  BOOL bRet;
  uint64_t timeLimit = 0;
  if (time > 0)
    timeLimit = time + GetTickCount64();

  while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0) {
    if (bRet == -1)
      return;
    if (gEvent != 0)
      RestServer::computeRequested(*gEvent);

    TranslateMessage(&msg);
    DispatchMessage(&msg);
    if (timeLimit != 0 && GetTickCount64() > timeLimit)
      return;
  }
}

void flushEvent(const string &id, const string &origin, DWORD data, int extraData) {
  for (size_t k = 0; k < gdi_extra.size(); k++) {
    if (gdi_extra[k])
      gdi_extra[k]->makeEvent(id, origin, data, extraData, false);
  }
}

void destroyExtraWindows() {
  for (size_t k = 1; k < gdi_extra.size(); k++) {
    if (gdi_extra[k])
      DestroyWindow(gdi_extra[k]->getHWNDMain());
  }
}

string uniqueTag(const char *base) {
  int j = 0;
  string b = base;
  while (true) {
    string tag = b + itos(j++);
    if (getExtraWindow(tag, false) == 0)
      return tag;
  }
}

vector<string> getExtraWindows() {
  vector<string> res;
  for (size_t k = 0; k < gdi_extra.size(); k++) {
    if (gdi_extra[k])
      res.push_back(gdi_extra[k]->getTag());
  }
  return res;
}

gdioutput *getExtraWindow(const string &tag, bool toForeGround) {
  for (size_t k = 0; k < gdi_extra.size(); k++) {
    if (gdi_extra[k] && gdi_extra[k]->hasTag(tag)) {
      if (toForeGround)
        SetForegroundWindow(gdi_extra[k]->getHWNDMain());
      return gdi_extra[k];
    }
  }
  return 0;
}

gdioutput *createExtraWindow(const string &tag, const wstring &title, int max_x, int max_y, bool fixedSize) {
  if (getExtraWindow(tag, false) != 0)
    throw meosException("Window already exists");

  HWND hDskTop = GetDesktopWindow();
  RECT rc;
  GetClientRect(hDskTop, &rc);

  int xp = gEvent->getPropertyInt("xpos", 50) + 16;
  int yp = gEvent->getPropertyInt("ypos", 20) + 32;

  for (size_t k = 0; k < gdi_extra.size(); k++) {
    if (gdi_extra[k]) {
      RECT rcc;
      if (GetWindowRect(gdi_extra[k]->getHWNDTarget(), &rcc)) {
        xp = max<int>(rcc.left + 16, xp);
        yp = max<int>(rcc.top + 32, yp);
      }
    }
  }

  if (xp > rc.right - 100)
    xp = rc.right - 100;
  if (yp > rc.bottom - 100)
    yp = rc.bottom - 100;

  int xs = max_x, ys = max_y;
  if (!fixedSize) {
    xs = gEvent->getPropertyInt("xsize", max(850, min(int(rc.right) - yp, 1124)));
    ys = gEvent->getPropertyInt("ysize", max(650, min(int(rc.bottom) - yp - 40, 800)));
    if (max_x > 0)
      xs = min(max_x, xs);
    if (max_y > 0)
      ys = min(max_y, ys);
  }

  HWND hWnd = CreateWindowEx(0, app_frame::workSpaceClassName(), title.c_str(),
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, xp, yp, max(xs, 200),
                             max(ys, 100), 0, NULL, hInst, NULL);
  if (!hWnd)
    return 0;

  ShowWindow(hWnd, SW_SHOWNORMAL);
  UpdateWindow(hWnd);
  gdioutput *gdi = new gdioutput(tag, 1.0);
  gdi->setFont(gEvent->getPropertyInt("TextSize", 0), gEvent->getPropertyString("UIFont", L"Segoe UI"));
  gdi->init(hWnd, hWnd, 0);
  SetWindowLongPtr(hWnd, GWLP_USERDATA, gdi_extra.size());
  currentFocusIx = gdi_extra.size();
  gdi_extra.push_back(gdi);
  return gdi;
}

const string &getLastExtraWindow() {
  if (gdi_extra.empty())
    throw meosException("Empty");
  return gdi_extra.back()->getTag();
}

// There is no tab control in a tool.
void createTabs(bool, bool, bool, bool, bool, bool, bool, bool, bool) {}
void hideTabs() {}

void updateScrollInfo(HWND hWnd, gdioutput &gdi, int nHeight, int nWidth) {
  SCROLLINFO si;
  si.cbSize = sizeof(si);
  si.fMask = SIF_PAGE | SIF_RANGE;

  int maxx, maxy;
  gdi.clipOffset(nWidth, nHeight, maxx, maxy);

  si.nMin = 0;
  if (maxy > 0) {
    si.nMax = maxy + nHeight;
    si.nPos = gdi.getOffsetY();
    si.nPage = nHeight;
  }
  else {
    si.nMax = 0;
    si.nPos = 0;
    si.nPage = 0;
  }
  SetScrollInfo(hWnd, SB_VERT, &si, true);

  si.nMin = 0;
  if (maxx > 0) {
    si.nMax = maxx + nWidth;
    si.nPos = gdi.getOffsetX();
    si.nPage = nWidth;
  }
  else {
    si.nMax = 0;
    si.nPos = 0;
    si.nPage = 0;
  }
  SetScrollInfo(hWnd, SB_HORZ, &si, true);
}

// The default files MeOS installs into the user folder are not needed by a tool.
void Setup(bool, bool) {}
void exportSetup() {}

wstring getMeOSFile(const wchar_t *fileName) {
  wstring out = programPath;
  size_t i = out.length();
  if (i > 0 && out[i - 1] != '\\' && out[i - 1] != '/')
    out.push_back('\\');
  out += fileName;
  return out;
}

bool getUserFile(wchar_t *fileNamePath, const wchar_t *fileName) {
  wcscpy_s(fileNamePath, MAX_PATH, (userFolder + fileName).c_str());
  return true;
}

bool getDesktopFile(wchar_t *fileNamePath, const wchar_t *fileName, const wchar_t *subFolder) {
  wchar_t path[MAX_PATH];
  if (SHGetSpecialFolderPath(hWndMain, path, CSIDL_DESKTOPDIRECTORY, 1)) {
    wstring appPath = wstring(path) + L"\\Meos\\";
    CreateDirectory(appPath.c_str(), NULL);
    if (subFolder) {
      appPath += wstring(subFolder) + L"\\";
      CreateDirectory(appPath.c_str(), NULL);
    }
    wcscpy_s(fileNamePath, MAX_PATH, (appPath + fileName).c_str());
  }
  else
    wcscpy_s(fileNamePath, MAX_PATH, fileName);
  return true;
}

wstring getTempPath() {
  wchar_t tempFile[MAX_PATH];
  if (tempPath.empty()) {
    wchar_t path[MAX_PATH];
    GetTempPath(MAX_PATH, path);
    GetTempFileName(path, L"meos", 0, tempFile);
    DeleteFile(tempFile);
    if (CreateDirectory(tempFile, NULL))
      tempPath = tempFile;
    else
      throw std::runtime_error("Failed to create temporary file.");
  }
  return tempPath;
}

void registerTempFile(const wstring &tempFile) {
  tempFiles.insert(tempFile);
}

wstring getTempFile() {
  getTempPath();
  wchar_t tempFile[MAX_PATH];
  if (GetTempFileName(tempPath.c_str(), L"ix", 0, tempFile)) {
    tempFiles.insert(tempFile);
    return tempFile;
  }
  throw std::runtime_error("Failed to create temporary file.");
}

void removeTempFile(const wstring &file) {
  DeleteFile(file.c_str());
  tempFiles.erase(file);
}

/* ---------------------------------------------------------------------
   Initialization and work spaces
   --------------------------------------------------------------------- */

namespace app_frame {

bool initialize(HINSTANCE instance, const wchar_t *dataFolder, const std::wstring &language) {
  hInst = instance;

  for (int k = 0; k < 100; k++)
    RunnerStatusOrderMap[k] = 0;
  RunnerStatusOrderMap[StatusOK] = 0;
  RunnerStatusOrderMap[StatusNoTiming] = 1;
  RunnerStatusOrderMap[StatusOutOfCompetition] = 2;
  RunnerStatusOrderMap[StatusMAX] = 3;
  RunnerStatusOrderMap[StatusMP] = 4;
  RunnerStatusOrderMap[StatusDNF] = 5;
  RunnerStatusOrderMap[StatusDQ] = 6;
  RunnerStatusOrderMap[StatusCANCEL] = 7;
  RunnerStatusOrderMap[StatusDNS] = 8;
  RunnerStatusOrderMap[StatusUnknown] = 9;
  RunnerStatusOrderMap[StatusNotCompeting] = 10;

  GetCurrentDirectory(MAX_PATH, programPath);
  GetModuleFileName(NULL, exePath, MAX_PATH);
  int lastDiv = -1;
  for (int i = 0; i < MAX_PATH && exePath[i] != 0; i++) {
    if (exePath[i] == '\\' || exePath[i] == '/')
      lastDiv = i;
  }
  if (lastDiv != -1)
    exePath[lastDiv] = 0;
  else
    exePath[0] = 0;

  wchar_t appData[MAX_PATH];
  if (SHGetSpecialFolderPath(NULL, appData, CSIDL_APPDATA, 1)) {
    userFolder = wstring(appData) + L"\\" + dataFolder + L"\\";
    CreateDirectory(userFolder.c_str(), NULL);
  }
  else
    userFolder.clear();

  // clubnamemap.csv comes with a MeOS installation, not with the sources; without it
  // loadNameMap shows an error. An empty map is enough for a tool.
  wchar_t nameMap[MAX_PATH];
  getUserFile(nameMap, L"clubnamemap.csv");
  if (!userFolder.empty() && !fileExists(nameMap)) {
    HANDLE file = CreateFile(nameMap, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
      CloseHandle(file);
  }

  oClub::loadNameMap();
  lang.init();
  StringCache::getInstance().init();

  for (RunnerStatus st : getAllRunnerStatus()) {
    if (st != StatusOK)
      assert(RunnerStatusOrderMap[st] > 0);
    oAbstractRunner::encodeStatus(st);
  }

  getUserFile(settingsFile, L"meoswpref.xml");
  int rInit = (GetTickCount() / 100);
  InitRanom(rInit, rInit / 379);

  gdi_main = new gdioutput("main", 1.0);
  gdi_extra.push_back(gdi_main);

  try {
    gEvent = new oEvent(*gdi_main);
    gEvent->setMainEvent();
  }
  catch (meosException &ex) {
    std::fprintf(stderr, "Failed to create base event: %s\n", gdioutput::toUTF8(ex.wwhat()).c_str());
    return false;
  }
  catch (std::exception &ex) {
    std::fprintf(stderr, "Failed to create base event: %s\n", ex.what());
    return false;
  }

  if (fileExists(settingsFile))
    gEvent->loadProperties(settingsFile);
  gEvent->clear();

  lang.get().addLangResource(L"English", L"104");
  lang.get().addLangResource(L"Svenska", L"103");
  lang.get().addLangResource(L"Deutsch", L"105");
  lang.get().addLangResource(L"Dansk", L"106");
  lang.get().addLangResource(L"Český", L"108");
  lang.get().addLangResource(L"Français", L"110");
  lang.get().addLangResource(L"Español", L"111");
  lang.get().addLangResource(L"Russian", L"107");
  lang.get().addLangResource(L"українська", L"112");
  lang.get().addLangResource(L"Português", L"113");
  lang.get().addLangResource(L"български", L"114");
  lang.get().addLangResource(L"Català", L"115");

  // Without a settings file the language is the one asked for; oEvent's default is
  // the old resource number of Swedish.
  if (!fileExists(settingsFile))
    gEvent->setProperty("Language", language);
  wstring defLang = gEvent->getPropertyString("Language", language);
  defaultCodePage = gEvent->getPropertyInt("CodePage", 1252);

  // Backward compatibility, as in meos.cpp
  if (defLang == L"103")
    defLang = L"Svenska";
  else if (defLang == L"104")
    defLang = L"English";
  gEvent->setProperty("Language", defLang);
  try {
    lang.get().loadLangResource(defLang);
  }
  catch (std::exception &ex) {
    std::fprintf(stderr, "Failed to load language %s: %s\n", gdioutput::toUTF8(defLang).c_str(), ex.what());
    lang.get().loadLangResource(L"Svenska");
  }

  // List definitions next to the program, as in meos.cpp.
  try {
    vector<wstring> res;
    if (exePath[0]) {
      expandDirectory(exePath, L"*.lxml", res);
      expandDirectory(exePath, L"*.listdef", res);
    }
    for (const wstring &file : res) {
      xmlparser xml;
      xml.read(file);
      xmlobject xlist = xml.getObject(0);
      gEvent->getListContainer().load(MetaListContainer::InternalList, xlist, true);
    }
  }
  catch (std::exception &ex) {
    std::fprintf(stderr, "Failed to load list definitions: %s\n", ex.what());
  }
  return true;
}

void registerWorkSpaceClass(HINSTANCE instance) {
  WNDCLASSEX wcex;
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wcex.lpfnWndProc = WorkSpaceWndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = instance;
  wcex.hIcon = 0;
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = 0;
  wcex.lpszMenuName = 0;
  wcex.lpszClassName = workSpaceClassName();
  wcex.hIconSm = 0;
  RegisterClassEx(&wcex);

  AutoCompleteInfo::registerAutoClass();
}

const wchar_t *workSpaceClassName() {
  return L"MeosWorkSpace";
}

void interfaceTimeout() {
  static bool lock = false;
  if (lock)
    return;
  lock = true;
  try {
    uint64_t tick = GetTickCount64();
    for (size_t k = 0; k < gdi_extra.size(); k++) {
      if (gdi_extra[k])
        gdi_extra[k]->CheckInterfaceTimeouts(tick);
    }
  }
  catch (meosException &ex) {
    if (gdi_main)
      gdi_main->alert(ex.wwhat());
  }
  catch (std::exception &ex) {
    if (gdi_main)
      gdi_main->alert(ex.what());
  }
  lock = false;
}

void installKeyboardHook() {
  if (!keyboardHook)
    keyboardHook = SetWindowsHookEx(WH_KEYBOARD, KeyboardProc, 0, GetCurrentThreadId());
}

void shutdown() {
  if (keyboardHook) {
    UnhookWindowsHookEx(keyboardHook);
    keyboardHook = nullptr;
  }

  // Unlike meos.cpp, the event is deleted before the main gdioutput: ~oEvent still
  // uses it (clear -> checkDB -> updateTabs, setWindowTitle), which reads freed memory
  // when gdi_main is deleted first.
  for (size_t k = 1; k < gdi_extra.size(); k++) {
    if (gdi_extra[k]) {
      HWND hWnd = gdi_extra[k]->getHWNDMain();
      if (hWnd)
        DestroyWindow(hWnd);
      // WM_DESTROY of a work space deletes its gdioutput and shortens gdi_extra.
      if (k < gdi_extra.size() && gdi_extra[k]) {
        delete gdi_extra[k];
        gdi_extra[k] = 0;
      }
    }
  }

  if (gEvent && settingsFile[0])
    gEvent->saveProperties(settingsFile);

  delete gEvent;
  gEvent = nullptr;

  if (!gdi_extra.empty() && gdi_extra[0]) {
    HWND hWnd = gdi_extra[0]->getHWNDMain();
    // Fails without harm if the window is already gone.
    if (hWnd)
      DestroyWindow(hWnd);
    delete gdi_extra[0];
  }
  gdi_main = nullptr;
  gdi_extra.clear();

  removeTempFiles();
  StringCache::getInstance().clear();
  lang.unload();
}

LRESULT CALLBACK WorkSpaceWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  LONG_PTR ix = GetWindowLongPtr(hWnd, GWLP_USERDATA);
  gdioutput *gdi = 0;
  if (ix < LONG_PTR(gdi_extra.size()))
    gdi = gdi_extra[ix];

  if (gdi) {
    LRESULT res = gdi->ProcessMsg(message, lParam, wParam);
    if (res)
      return res;
  }

  switch (message) {
    case WM_CREATE:
      break;

    case WM_SIZE:
      if (gdi)
        updateScrollInfo(hWnd, *gdi, HIWORD(lParam), LOWORD(lParam));
      InvalidateRect(hWnd, NULL, true);
      break;

    case WM_VSCROLL: {
      if (!gdi)
        break;
      int nScrollCode = (int)LOWORD(wParam);
      int yInc;
      int yPos = gdi->getOffsetY();
      RECT rc;
      GetClientRect(hWnd, &rc);
      int pagestep = max(50, int(0.9 * rc.bottom));

      switch (nScrollCode) {
        case SB_PAGEUP:
          yInc = -pagestep;
          break;
        case SB_PAGEDOWN:
          yInc = pagestep;
          break;
        case SB_LINEUP:
          yInc = -10;
          break;
        case SB_LINEDOWN:
          yInc = 10;
          break;
        case SB_THUMBTRACK: {
          SCROLLINFO si;
          ZeroMemory(&si, sizeof(si));
          si.cbSize = sizeof(si);
          si.fMask = SIF_TRACKPOS;
          if (!GetScrollInfo(hWnd, SB_VERT, &si))
            return 1;
          yInc = si.nTrackPos - yPos;
          break;
        }
        default:
          yInc = 0;
      }

      scrollVertical(gdi, yInc, hWnd);
      gdi->storeAutoPos(gdi->getOffsetY());
      break;
    }

    case WM_HSCROLL: {
      if (!gdi)
        break;
      int nScrollCode = (int)LOWORD(wParam);
      int xInc;
      int xPos = gdi->getOffsetX();

      switch (nScrollCode) {
        case SB_ENDSCROLL:
          InvalidateRect(hWnd, 0, false);
          return 0;
        case SB_PAGEUP:
          xInc = -80;
          break;
        case SB_PAGEDOWN:
          xInc = 80;
          break;
        case SB_LINEUP:
          xInc = -10;
          break;
        case SB_LINEDOWN:
          xInc = 10;
          break;
        case SB_THUMBTRACK: {
          SCROLLINFO si;
          ZeroMemory(&si, sizeof(si));
          si.cbSize = sizeof(si);
          si.fMask = SIF_TRACKPOS;
          if (!GetScrollInfo(hWnd, SB_HORZ, &si))
            return 1;
          xInc = si.nTrackPos - xPos;
          break;
        }
        default:
          xInc = 0;
      }

      SCROLLINFO si;
      si.cbSize = sizeof(si);
      si.fMask = SIF_ALL;
      GetScrollInfo(hWnd, SB_HORZ, &si);

      if (si.nPage == 0)
        xInc = 0;

      int a = si.nMax - signed(si.nPage - 1) - xPos;

      if ((xInc = max(-xPos, min(xInc, a))) != 0) {
        xPos += xInc;
        RECT ClipArea;
        GetClientRect(hWnd, &ClipArea);
        gdi->setOffsetX(xPos);
        ScrollWindowEx(hWnd, -xInc, 0, 0, &ClipArea, (HRGN)NULL, (LPRECT)NULL, SW_INVALIDATE | SW_SCROLLCHILDREN);
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        si.nPos = xPos;
        SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
        UpdateWindow(hWnd);
      }
      break;
    }

    case WM_MOUSEWHEEL:
      if (gdi) {
        scrollVertical(gdi, -GET_WHEEL_DELTA_WPARAM(wParam), hWnd);
        gdi->storeAutoPos(gdi->getOffsetY());
      }
      break;

    case WM_ACTIVATE:
      if (LOWORD(wParam) != WA_INACTIVE)
        currentFocusIx = ix;
      return DefWindowProc(hWnd, message, wParam, lParam);

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT rt;
      GetClientRect(hWnd, &rt);
      if (gdi && (ps.rcPaint.right | ps.rcPaint.left | ps.rcPaint.top | ps.rcPaint.bottom) != 0)
        gdi->draw(hdc, rt, ps.rcPaint);
      EndPaint(hWnd, &ps);
      break;
    }

    case WM_ERASEBKGND:
      return 0;

    case WM_DESTROY:
      if (ix > 0 && gdi) {
        gdi->makeEvent("CloseWindow", "meos", 0, 0, false);
        gdi_extra[ix] = 0;
        delete gdi;
        while (!gdi_extra.empty() && gdi_extra.back() == 0)
          gdi_extra.pop_back();
      }
      break;

    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

} // namespace app_frame
