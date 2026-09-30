/************************************************************************
    MeOS - Orienteering Software
    Linux port: runs the tests of registerTests() without user interaction.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// The tests in tests.cpp run inside MeOS: "Run tests" on the competition page (debug
// builds or -test) starts them in the main window. This program gives them the same
// surroundings without meos.cpp (decision E11 of stage 1.3): the application frame of
// the workbench (app_frame.cpp), a main window whose WM_USER empties the card queue as
// meos.cpp does (TestMeOS::insertCard posts the card there), a work space with the main
// gdioutput and the 100 ms interface timer, which also keeps the message loop of
// mainMessageLoop(0, time) running. It runs every test, writes the result page as
// text to stdout and exits with 1 if a test failed or none passed.
// Uses the Win32 API only, so it builds with MSVC as well. Settings go to the folder
// "MeOS GUI Tests" in the user's application data folder; CTest points XDG_DATA_HOME
// to the build tree.

#include "stdafx.h"

#include <cstdio>
#include <string>

#include "app_frame.h"
#include "gdioutput.h"
#include "image.h"
#include "meosexception.h"
#include "oEvent.h"
#include "resource.h"
#include "SportIdent.h"
#include "testmeos.h"

extern gdioutput *gdi_main;
extern oEvent *gEvent;
extern SportIdent *gSI;
extern HWND hWndMain;
extern Image image;

void InsertSICard(gdioutput &gdi, SICard &sic);

namespace {

HWND hWndWorkspace = nullptr;
// The timer id meos.cpp uses for AutoTask::interfaceTimeout.
constexpr UINT_PTR interfaceTimer = 2;

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      if (hWndWorkspace)
        MoveWindow(hWndWorkspace, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
      return 0;
    case WM_TIMER:
      if (wParam == interfaceTimer)
        app_frame::interfaceTimeout();
      return 0;
    case WM_USER: {
      // As meos.cpp: SportIdent::addCard queues the card and posts WM_USER.
      SICard sic(ConvertedTimeStatus::Unknown);
      while (gSI && gSI->getCard(sic))
        InsertSICard(*gdi_main, sic);
      return 0;
    }
    case WM_DESTROY:
      KillTimer(hWnd, interfaceTimer);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
  }
}

// Writes the texts of a page as lines; texts on the same line are separated by blanks.
int printPage(const gdioutput &gdi) {
  int failed = 0;
  int lastY = -1;
  std::string line;
  for (const TextInfo &text : gdi.getTL()) {
    if (text.getY() != lastY && !line.empty()) {
      std::printf("%s\n", line.c_str());
      line.clear();
    }
    lastY = text.getY();
    if (!line.empty())
      line += ' ';
    line += gdioutput::toUTF8(text.text);
    if (text.text.compare(0, 7, L"FAILED ") == 0)
      failed++;
  }
  if (!line.empty())
    std::printf("%s\n", line.c_str());
  return failed;
}

} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nCmdShow) {
  if (!app_frame::initialize(hInstance, L"MeOS GUI Tests"))
    return 1;
  app_frame::registerWorkSpaceClass(hInstance);

  WNDCLASSEX wcex = {};
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = MainWndProc;
  wcex.hInstance = hInstance;
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszClassName = L"MeosGuiTests";
  RegisterClassEx(&wcex);

  hWndMain = CreateWindowEx(0, L"MeosGuiTests", L"MeOS GUI Tests",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 60, 40, 1100, 800, NULL, NULL,
                            hInstance, NULL);
  if (!hWndMain)
    return 1;

  app_frame::installKeyboardHook();
  ShowWindow(hWndMain, nCmdShow);
  UpdateWindow(hWndMain);

  hWndWorkspace = CreateWindowEx(0, app_frame::workSpaceClassName(), L"WorkSpace",
                                 WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 200, 100, hWndMain, NULL,
                                 hInstance, NULL);
  ShowWindow(hWndWorkspace, nCmdShow);
  UpdateWindow(hWndWorkspace);

  RECT rc;
  GetClientRect(hWndMain, &rc);
  SendMessage(hWndMain, WM_SIZE, 0, MAKELONG(rc.right, rc.bottom));

  gdi_main->setFont(gEvent->getPropertyInt("TextSize", 0), gEvent->getPropertyString("UIFont", L"Segoe UI"));
  gdi_main->init(hWndWorkspace, hWndMain, NULL);
  image.loadImage(IDI_MEOSEDIT, Image::ImageMethod::Default);
  SetTimer(hWndMain, interfaceTimer, 100, 0);

  int failed = 0;
  int count = 0;
  // An installation in which MeOS has run before. TestMeOS::runProtected loads the
  // competition page with these settings before it switches to the defaults. On a
  // fresh installation every test would start on the welcome page, which multiplies
  // the scale of the main window by 1.4 times the current scale each time (finding 1
  // of stage 1.3.4, the same under Windows), and the page would ask whether to use
  // Eventor (2: the user said no).
  gEvent->setProperty("FirstTime", 0);
  gEvent->setProperty("UseEventor", 2);
  try {
    // As "Run tests" on the competition page (TabCompetition.cpp).
    TestMeOS tm(gEvent, "ALL");
    std::vector<std::pair<std::wstring, size_t>> tests;
    tm.getTests(tests);
    count = int(tests.size()) - 1; // without "ALL"
    tm.runAll();
    gEvent->clear();
    tm.publish(*gdi_main);
    failed = printPage(*gdi_main);
  }
  catch (meosException &ex) {
    std::fprintf(stderr, "exception: %s\n", gdioutput::toUTF8(ex.wwhat()).c_str());
    failed++;
  }
  catch (std::exception &ex) {
    std::fprintf(stderr, "exception: %s\n", ex.what());
    failed++;
  }

  delete gSI;
  gSI = nullptr;
  gEvent->newCompetition(L"");
  app_frame::shutdown();

  if (count <= 0) {
    std::fprintf(stderr, "no tests registered\n");
    return 1;
  }
  return failed ? 1 : 0;
}
