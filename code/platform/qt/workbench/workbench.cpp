/************************************************************************
    MeOS - Orienteering Software
    Linux port: GUI workbench.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// A Win32 program that shows real gdioutput pages without the MeOS application frame,
// to check the Qt backend by hand and to compare it with Windows (the same program
// builds with MSVC). Pages:
//   1. text in every gdiFonts format and colour, rectangles, images from resources;
//   2. every kind of control with callbacks, info boxes, a timer, tooltips and the
//      common dialogs;
//   3. a competition (the file given on the command line, or a built-in demo) with
//      the runner table (sort, edit, copy to the clipboard).
//
// Usage: meos_gui_workbench [competition file] [-page N]
// Settings go to the folder "MeOS Workbench" in the user's application data folder.
// Like MeOS, the program reads installation files (sportident.cardsystem, needed by
// the runner table) from the current folder; start it in a MeOS installation folder.

#include "stdafx.h"

#include <cstdio>

#include "app_frame.h"
#include "demo_competition.h"
#include "gdioutput.h"
#include "image.h"
#include "localizer.h"
#include "meos_util.h"
#include "meosexception.h"
#include "oEvent.h"
#include "resource.h"
#include "Table.h"

extern gdioutput *gdi_main;
extern oEvent *gEvent;
extern HWND hWndMain;
extern Image image;

namespace {

HWND hWndWorkspace = nullptr;
// The timer id meos.cpp uses for AutoTask::interfaceTimeout.
constexpr UINT_PTR interfaceTimer = 2;
std::wstring competitionFile;
int eventCount = 0;

enum class Page { Text = 1, Controls = 2, Table = 3 };

void loadPage(gdioutput &gdi, Page page);

void setStatus(gdioutput &gdi, const std::wstring &text) {
  eventCount++;
  gdi.setText("Status", L"#" + itow(eventCount) + L": " + text, true);
}

int navigationCB(gdioutput *gdi, GuiEventType type, BaseInfo *data) {
  if (type != GUI_BUTTON)
    return 0;
  const ButtonInfo &bi = dynamic_cast<ButtonInfo &>(*data);
  if (bi.id == "PageText")
    loadPage(*gdi, Page::Text);
  else if (bi.id == "PageControls")
    loadPage(*gdi, Page::Controls);
  else if (bi.id == "PageTable")
    loadPage(*gdi, Page::Table);
  return 0;
}

// Ends a row filled to the right: continues at the left, below its lowest widget.
void endRow(gdioutput &gdi) {
  gdi.fillDown();
  gdi.popX();
  gdi.setCY(gdi.getHeight());
  gdi.dropLine(0.5);
}

void addNavigation(gdioutput &gdi, Page page) {
  gdi.fillRight();
  gdi.addButton("PageText", L"1 Text and graphics", navigationCB).isEdit(page == Page::Text);
  gdi.addButton("PageControls", L"2 Controls", navigationCB).isEdit(page == Page::Controls);
  gdi.addButton("PageTable", L"3 Competition table", navigationCB).isEdit(page == Page::Table);
  gdi.fillDown();
  gdi.popX();
  gdi.dropLine(2.5);
}

/* ---------------------------------------------------------------------
   Page 1: text, colours, rectangles, images
   --------------------------------------------------------------------- */

void showTextPage(gdioutput &gdi) {
  gdi.addStringUT(boldLarge, L"Text formats");
  gdi.dropLine(0.3);

  const std::pair<int, const wchar_t *> formats[] = {
    {normalText, L"normalText"},       {boldText, L"boldText"},
    {boldLarge, L"boldLarge"},         {boldHuge, L"boldHuge"},
    {boldSmall, L"boldSmall"},         {italicText, L"italicText"},
    {italicMediumPlus, L"italicMediumPlus"}, {monoText, L"monoText"},
    {fontLarge, L"fontLarge"},         {fontMedium, L"fontMedium"},
    {fontSmall, L"fontSmall"},         {fontMediumPlus, L"fontMediumPlus"},
    {italicSmall, L"italicSmall"},
  };
  const int left = gdi.getCX();
  for (const auto &format : formats) {
    const int y = gdi.getCY();
    gdi.addStringUT(y, left, normalText, format.second);
    gdi.addStringUT(y, left + gdi.scaleLength(140), format.first,
                    L"Åsa Öberg 12:34:56 – Jörg Müller, Łódź (0123456789)");
  }

  gdi.dropLine();
  gdi.addStringUT(boldLarge, L"Alignment, breaking and ellipsis");
  int y = gdi.getCY();
  const int boxWidth = gdi.scaleLength(220);
  gdi.addRectangle(left, y, left + boxWidth, y + gdi.scaleLength(90), colorLightBlue);
  gdi.addStringUT(y + 2, left + boxWidth - 4, textRight, L"Right aligned");
  gdi.addStringUT(y + 2 + gdi.getLineHeight(), left + boxWidth / 2, textCenter, L"Centered");
  gdi.addStringUT(y + 2 + 2 * gdi.getLineHeight(), left + 4, breakLines,
                  L"This line is broken at spaces when it does not fit into the box.", boxWidth - 8);
  gdi.addStringUT(y + 2, left + boxWidth + gdi.scaleLength(20), textLimitEllipsis,
                  L"A long text that ends with an ellipsis", gdi.scaleLength(160));
  gdi.setCY(y + gdi.scaleLength(100));

  gdi.addStringUT(boldLarge, L"Colours");
  const std::pair<GDICOLOR, const wchar_t *> colors[] = {
    {colorBlack, L"Black"}, {colorRed, L"Red"}, {colorGreen, L"Green"}, {colorDarkBlue, L"Dark blue"},
    {colorGreyBlue, L"Grey blue"}, {colorDarkRed, L"Dark red"}, {colorMediumDarkRed, L"Medium dark red"},
    {colorYellow, L"Yellow"},
  };
  gdi.fillRight();
  for (const auto &color : colors)
    gdi.addStringUT(boldText, color.second).setColor(color.first);
  gdi.fillDown();
  gdi.popX();
  gdi.dropLine(1.5);

  y = gdi.getCY();
  const GDICOLOR backgrounds[] = {colorLightBlue, colorLightRed, colorLightGreen, colorLightYellow,
                                  colorLightCyan, colorLightMagenta, colorMediumRed, colorMediumGreen,
                                  colorMediumYellow};
  int x = left;
  for (GDICOLOR background : backgrounds) {
    gdi.addRectangle(x, y, x + gdi.scaleLength(50), y + gdi.scaleLength(30), background, true, false, colorDarkGrey);
    x += gdi.scaleLength(56);
  }
  RECT gradient = {x, y, x + gdi.scaleLength(120), y + gdi.scaleLength(30)};
  gdi.addRectangle(gradient, colorLightBlue, false).setColor2(colorDarkBlue);
  gdi.setCY(y + gdi.scaleLength(40));

  gdi.addStringUT(boldLarge, L"Images from resources");
  y = gdi.getCY();
  gdi.addImage("", y, left, 0, itow(IDI_MEOSIMAGE), gdi.scaleLength(64), gdi.scaleLength(64));
  gdi.addImage("", y, left + gdi.scaleLength(80), 0, itow(IDI_MEOSEDIT), gdi.scaleLength(32), gdi.scaleLength(32));
  gdi.addImage("", y, left + gdi.scaleLength(130), 0, itow(IDI_SPLASHIMAGE), gdi.scaleLength(200));
  gdi.setCY(y + gdi.scaleLength(140));
}

/* ---------------------------------------------------------------------
   Page 2: controls, info boxes, timer, tooltips, dialogs
   --------------------------------------------------------------------- */

int controlsCB(gdioutput *gdi, GuiEventType type, BaseInfo *data) {
  switch (type) {
    case GUI_BUTTON: {
      ButtonInfo &bi = dynamic_cast<ButtonInfo &>(*data);
      if (bi.id == "Check")
        setStatus(*gdi, L"Checkbox is " + std::wstring(gdi->isChecked(bi.id) ? L"checked" : L"not checked"));
      else if (bi.id == "Push")
        setStatus(*gdi, L"Button pressed; name is '" + gdi->getText("Name") + L"'");
      else if (bi.id == "Ask") {
        bool yes = gdi->ask(L"Continue with a very long button text?", "Yes, continue with this", "No, stop here");
        setStatus(*gdi, std::wstring(L"ask() answered ") + (yes ? L"yes" : L"no"));
      }
      else if (bi.id == "Alert") {
        gdi->alert(L"This is an alert.");
        setStatus(*gdi, L"alert() closed");
      }
      else if (bi.id == "Info")
        gdi->addInfoBox("", L"An info box that disappears after 5 seconds.", L"Second line", BoxStyle::Header,
                        5000, controlsCB);
      else if (bi.id == "Warning")
        gdi->addInfoBox("", L"A warning box without time-out.", L"Click to close", BoxStyle::HeaderWarning, 0,
                        controlsCB);
      else if (bi.id == "Open") {
        std::vector<std::pair<std::wstring, std::wstring>> filter = {
            {L"Competitions", L"*.meos;*.meosxml;*.xml"}, {L"All files", L"*.*"}};
        setStatus(*gdi, L"Open: " + gdi->browseForOpen(filter, L"meos"));
      }
      else if (bi.id == "Save") {
        std::vector<std::pair<std::wstring, std::wstring>> filter = {{L"Web page", L"*.html"},
                                                                     {L"Text", L"*.txt"}};
        int filterIndex = 1;
        std::wstring file = gdi->browseForSave(filter, L"html", filterIndex);
        setStatus(*gdi, L"Save: " + file + L" (filter " + itow(filterIndex) + L")");
      }
      else if (bi.id == "Folder")
        setStatus(*gdi, L"Folder: " + gdi->browseForFolder(L"", L"Choose a folder"));
      else if (bi.id == "Color") {
        std::wstring def;
        DWORD color = gdi->selectColor(def, RGB(200, 100, 50));
        wchar_t text[32];
        swprintf_s(text, L"#%06X", color);
        setStatus(*gdi, std::wstring(L"Colour: ") + text);
      }
      else if (bi.id == "Clear")
        loadPage(*gdi, Page::Controls);
      break;
    }
    case GUI_INPUTCHANGE: {
      InputInfo &ii = dynamic_cast<InputInfo &>(*data);
      setStatus(*gdi, L"Input " + gdi->widen(ii.id) + L" changed to '" + ii.text + L"'");
      break;
    }
    case GUI_INPUT: {
      InputInfo &ii = dynamic_cast<InputInfo &>(*data);
      setStatus(*gdi, L"Input " + gdi->widen(ii.id) + L" left with '" + ii.text + L"'");
      break;
    }
    case GUI_LISTBOX: {
      ListBoxInfo &lbi = dynamic_cast<ListBoxInfo &>(*data);
      if (lbi.id == "Multi") {
        std::set<int> selection;
        gdi->getSelection(lbi.id, selection);
        setStatus(*gdi, L"Multiple selection: " + itow(int(selection.size())) + L" items");
      }
      else
        setStatus(*gdi, L"Selected in " + gdi->widen(lbi.id) + L": '" + lbi.text + L"' (data " +
                            itow(lbi.getDataInt()) + L")");
      break;
    }
    case GUI_LISTBOXSELECT: {
      ListBoxInfo &lbi = dynamic_cast<ListBoxInfo &>(*data);
      setStatus(*gdi, L"Double click in " + gdi->widen(lbi.id) + L": '" + lbi.text + L"'");
      break;
    }
    case GUI_COMBOCHANGE: {
      ListBoxInfo &lbi = dynamic_cast<ListBoxInfo &>(*data);
      setStatus(*gdi, L"Combo text changed to '" + lbi.text + L"'");
      break;
    }
    case GUI_INFOBOX:
      setStatus(*gdi, L"Info box clicked");
      break;
    case GUI_TIMEOUT:
      setStatus(*gdi, L"Timer expired");
      break;
    default:
      break;
  }
  return 0;
}

void showControlsPage(gdioutput &gdi) {
  gdi.addStringUT(boldLarge, L"Controls");
  gdi.addStringUT(0, L"Status:");
  gdi.addStringUT(0, L"-").id = "Status";
  gdi.dropLine(0.5);

  gdi.fillRight();
  gdi.addInput("Name", L"Jörg Müller", 20, controlsCB, L"Name:", L"Tooltip of the name field");
  gdi.addInput("Time", L"12:34:56", 8, controlsCB, L"Time:");
  gdi.addInput("Password", L"secret", 10, controlsCB, L"Password:").setPassword(true);
  endRow(gdi);

  gdi.addInputBox("Notes", 300, 50, L"A multi-line edit control.\r\nSecond line.", controlsCB, L"Notes:");
  gdi.dropLine(1);

  const std::vector<std::pair<std::wstring, size_t>> items = {
      {L"D21", 1}, {L"H21", 2}, {L"D45", 3}, {L"H45", 4}, {L"Öppen motion", 5}, {L"Élite", 6}};

  gdi.fillRight();
  gdi.addSelection("Class", 150, 200, controlsCB, L"Selection:", L"Tooltip of the selection");
  gdi.setItems("Class", items);
  gdi.selectItemByData("Class", 2);
  gdi.addCombo("Club", 150, 200, controlsCB, L"Combo box:");
  gdi.setItems("Club", items);
  gdi.setText("Club", L"Free text");
  endRow(gdi);

  gdi.fillRight();
  gdi.addListBox("List", 150, 120, controlsCB, L"List box:");
  gdi.setItems("List", items);
  gdi.addListBox("Multi", 150, 120, controlsCB, L"Multiple selection:", L"", true);
  gdi.setItems("Multi", items);
  endRow(gdi);

  gdi.fillRight();
  gdi.addCheckbox("Check", L"Checkbox", controlsCB, true, L"Tooltip of the checkbox");
  gdi.addButton("Push", L"Button", controlsCB, L"Tooltip of the button");
  gdi.addButton("Ask", L"ask() with renamed buttons", controlsCB);
  gdi.addButton("Alert", L"alert()", controlsCB);
  endRow(gdi);

  gdi.fillRight();
  gdi.addButton("Info", L"Info box", controlsCB);
  gdi.addButton("Warning", L"Warning box", controlsCB);
  gdi.addButton("Open", L"Open file…", controlsCB);
  gdi.addButton("Save", L"Save file…", controlsCB);
  gdi.addButton("Folder", L"Folder…", controlsCB);
  gdi.addButton("Color", L"Colour…", controlsCB);
  gdi.addButton("Clear", L"Reload page", controlsCB);
  endRow(gdi);

  gdi.addStringUT(0, L"Timer since page load, and a timer that reports its expiry after 10 s in the status line:");
  const int y = gdi.getCY();
  // zeroTime is the time shown at the start, in seconds; the time-out counts from there.
  gdi.addTimer(y, gdi.getCX(), timeSeconds, 0);
  gdi.addTimer(y, gdi.getCX() + gdi.scaleLength(120), timeSeconds, 0, L"", 0, controlsCB, 10);
  gdi.dropLine(1.5);
  gdi.addStringUT(italicSmall, L"Keys: Tab/Shift+Tab move the focus, Enter and Escape reach gdioutput, "
                               L"Ctrl+Plus/Minus (Ctrl+F5/F6) scale, F2 shows a progress window.");
}

/* ---------------------------------------------------------------------
   Page 3: competition and runner table
   --------------------------------------------------------------------- */

int tableCB(gdioutput *gdi, GuiEventType type, BaseInfo *data) {
  if (type != GUI_BUTTON)
    return 0;
  const ButtonInfo &bi = dynamic_cast<ButtonInfo &>(*data);
  if (bi.id == "OpenCompetition") {
    std::vector<std::pair<std::wstring, std::wstring>> filter = {{L"MeOS competitions", L"*.meos;*.meosxml;*.xml"}};
    std::wstring file = gdi->browseForOpen(filter, L"meos");
    if (!file.empty()) {
      competitionFile = file;
      loadPage(*gdi, Page::Table);
    }
  }
  else if (bi.id == "Demo") {
    competitionFile.clear();
    loadPage(*gdi, Page::Table);
  }
  return 0;
}

void showTablePage(gdioutput &gdi) {
  std::wstring error;
  try {
    if (competitionFile.empty())
      demo_competition::create(*gEvent);
    else if (!gEvent->open(competitionFile, false, false, false))
      error = L"Could not open " + competitionFile;
  }
  catch (meosException &ex) {
    error = ex.wwhat();
  }
  catch (std::exception &ex) {
    error = gdioutput::widen(ex.what());
  }

  gdi.addStringUT(boldLarge, competitionFile.empty() ? L"Demo competition" : competitionFile);
  gdi.addStringUT(0, itow(gEvent->getNumRunners()) + L" runners, " + itow(gEvent->getNumClasses()) + L" classes. " +
                         L"Sort by clicking a column header, edit a cell with a double click, copy with Ctrl+C.");
  if (!error.empty())
    gdi.addStringUT(boldText, error).setColor(colorRed);

  gdi.fillRight();
  gdi.addButton("OpenCompetition", L"Open competition…", tableCB);
  gdi.addButton("Demo", L"Demo competition", tableCB);
  endRow(gdi);
  gdi.addTable(oRunner::getTable(gEvent), gdi.getCX(), gdi.getCY());
}

void loadPage(gdioutput &gdi, Page page) {
  gdi.clearPage(false);
  gdi.pushX();
  addNavigation(gdi, page);
  try {
    switch (page) {
      case Page::Text:
        showTextPage(gdi);
        break;
      case Page::Controls:
        showControlsPage(gdi);
        break;
      case Page::Table:
        showTablePage(gdi);
        break;
    }
  }
  catch (meosException &ex) {
    gdi.addStringUT(boldText, L"Error: " + ex.wwhat()).setColor(colorRed);
  }
  catch (std::exception &ex) {
    gdi.addStringUT(boldText, L"Error: " + gdioutput::widen(ex.what())).setColor(colorRed);
  }
  gdi.refresh();
}

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
    case WM_DESTROY:
      KillTimer(hWnd, interfaceTimer);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
  }
}

} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR lpCmdLine, int nCmdShow) {
  Page startPage = Page::Text;
  std::string commandLine = lpCmdLine ? lpCmdLine : "";
  const size_t pageOption = commandLine.find("-page ");
  if (pageOption != std::string::npos) {
    int number = atoi(commandLine.c_str() + pageOption + 6);
    if (number >= 1 && number <= 3)
      startPage = Page(number);
    commandLine.erase(pageOption, commandLine.find_first_of(' ', pageOption + 6) - pageOption);
  }
  commandLine = trim(commandLine);
  if (commandLine.size() > 1 && commandLine.front() == '"' && commandLine.back() == '"')
    commandLine = commandLine.substr(1, commandLine.size() - 2);
  if (!commandLine.empty()) {
    // WinMain gets the command line in the ANSI code page (also on Linux, win32_main.cpp).
    std::wstring wide(commandLine.size() + 1, L'\0');
    int length = MultiByteToWideChar(CP_ACP, 0, commandLine.c_str(), int(commandLine.size()), &wide[0],
                                     int(wide.size()));
    competitionFile = wide.substr(0, std::max(length, 0));
    if (pageOption == std::string::npos)
      startPage = Page::Table;
  }

  if (!app_frame::initialize(hInstance, L"MeOS Workbench"))
    return 1;
  app_frame::registerWorkSpaceClass(hInstance);

  WNDCLASSEX wcex = {};
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = MainWndProc;
  wcex.hInstance = hInstance;
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszClassName = L"MeosWorkbench";
  RegisterClassEx(&wcex);

  hWndMain = CreateWindowEx(0, L"MeosWorkbench", L"MeOS GUI Workbench",
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

  loadPage(*gdi_main, startPage);
  SetTimer(hWndMain, interfaceTimer, 100, 0);

  extern void mainMessageLoop(HACCEL hAccelTable, DWORD time);
  mainMessageLoop(nullptr, 0);

  app_frame::shutdown();
  return 0;
}
