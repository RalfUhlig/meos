/************************************************************************
    MeOS - Orienteering Software
    Linux port: load test of the competition core.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Links all of MeOS except meos.cpp and runs a competition through the core:
// creates the demo competition, saves it as a .meos file, opens it again, checks
// runners, clubs, classes and results, generates a result list into a gdioutput and
// packs the file into a ZIP archive and back (minizip through the Win32 file API).
// Runs offscreen in CTest; XDG_DATA_HOME points to a folder of the build tree.

#include "StdAfx.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <list>
#include <set>

#include "app_frame.h"
#include "csvparser.h"
#include "demo_competition.h"
#include "gdioutput.h"
#include "meos_util.h"
#include "meosexception.h"
#include "oEvent.h"
#include "oListInfo.h"
#include "timeconstants.hpp"

extern gdioutput *gdi_main;
extern oEvent *gEvent;
extern HWND hWndMain;

namespace {

int failures = 0;

#define CHECK(expr)                                                                   \
  do {                                                                                \
    if (!(expr)) {                                                                    \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr);   \
      failures++;                                                                     \
    }                                                                                 \
  } while (false)

std::string readFile(const std::wstring &file) {
  std::ifstream in(meosPath(file), std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void checkCompetition(oEvent &oe) {
  CHECK(oe.getName() == L"Workbench Cup");
  CHECK(oe.getNumRunners() == int(demo_competition::entries().size()));
  CHECK(oe.getNumClasses() == demo_competition::numClasses());
  std::vector<pClub> clubs;
  oe.getClubs(clubs, false);
  CHECK(int(clubs.size()) == demo_competition::numClubs());

  // H21: François 33:59, Erik 36:30, Håkon 37:01, Günther 43:12.
  pRunner erik = oe.getRunnerByName(L"Erik Sjöberg");
  CHECK(erik != nullptr);
  if (erik) {
    CHECK(erik->getClub() == L"OK Älvdal");
    CHECK(erik->getCardNo() == 2100005);
    CHECK(erik->getStatus() == StatusOK);
    CHECK(erik->getRunningTime(false) == (36 * 60 + 30) * timeConstSecond);
    CHECK(erik->getPlace() == 2);
  }
  pRunner francois = oe.getRunnerByName(L"François Lefèvre");
  CHECK(francois != nullptr && francois->getPlace() == 1);
  pRunner doerthe = oe.getRunnerByName(L"Dörthe Müller");
  CHECK(doerthe != nullptr && doerthe->getStatus() == StatusDNS);
  pRunner lukasz = oe.getRunnerByName(L"Łukasz Żółw");
  CHECK(lukasz != nullptr && lukasz->getClass(false) == L"H45" && lukasz->getPlace() == 3);
}

// Fixed-length string fields of oData keep every length they can hold. They live
// in a byte array of each object; unless that array is aligned for wchar_t, glibc's
// vectorised wcslen and wcscmp measure wrong (a 20-character result module tag of
// a class came back with 19 characters).
void checkFixedStrings(oBase &object, const char *field, int maxChars) {
  for (int length = 1; length <= maxChars; length++) {
    std::wstring value;
    for (int k = 0; k < length; k++)
      value.push_back(wchar_t(L'a' + (k + length) % 26));
    object.getDI().setString(field, value);
    if (object.getDCI().getString(field) != value) {
      std::fprintf(stderr, "%s: wrong value at length %d\n", field, length);
      failures++;
      break;
    }
  }
  object.getDI().setString(field, L"");
}

void checkFixedStrings(oEvent &oe) {
  std::vector<pClass> classes;
  oe.getClasses(classes, false);
  for (pClass cls : classes) {
    checkFixedStrings(*cls, "Result", 24);
    checkFixedStrings(*cls, "TextA", 40);
  }
  std::vector<pRunner> runners;
  oe.getRunners(0, 0, runners, false);
  for (pRunner runner : runners)
    checkFixedStrings(*runner, "Country", 23);
}

// A CSV file reads the same in each encoding csvparser::parse accepts. MeOS reads
// UTF-16 files (Excel's "Unicode text", byte order mark FF FE) two bytes per
// character, which is a wchar_t only on Windows.
std::string utf16le(const std::wstring &text) {
  std::string out("\xFF\xFE", 2);
  auto unit = [&out](unsigned u) {
    out.push_back(char(u & 0xFF));
    out.push_back(char(u >> 8));
  };
  for (wchar_t c : text) {
    unsigned code = unsigned(c);
    if (code > 0xFFFF) {
      code -= 0x10000;
      unit(0xD800 + (code >> 10));
      unit(0xDC00 + (code & 0x3FF));
    }
    else
      unit(code);
  }
  return out;
}

void checkCsvEncoding(const std::string &bytes, const std::list<std::vector<std::wstring>> &expected) {
  const std::wstring file = getTempFile() + L".csv";
  {
    std::ofstream out(meosPath(file), std::ios::binary);
    out << bytes;
  }
  csvparser csv;
  std::list<std::vector<std::wstring>> rows;
  csv.parse(file, rows);
  removeTempFile(file);
  CHECK(rows == expected);
  if (rows != expected) {
    for (const auto &row : rows)
      for (const std::wstring &cell : row)
        std::fprintf(stderr, "  cell: %s\n", gdioutput::toUTF8(cell).c_str());
  }
}

void checkCsvEncodings() {
  const std::list<std::vector<std::wstring>> latin = {{L"Müller", L"Jürgen", L"500101"},
                                                       {L"Weiß", L"Zoë", L"ESV Köln"}};
  std::list<std::vector<std::wstring>> all = latin;
  all.push_back({L"Żółw", L"Łukasz", L"Ωμέγα \U0001F600"});
  const std::wstring allText = L"Müller;Jürgen;500101\r\nWeiß;Zoë;ESV Köln\r\nŻółw;Łukasz;Ωμέγα \U0001F600\r\n";

  checkCsvEncoding("\xEF\xBB\xBF" + gdioutput::toUTF8(allText), all);
  checkCsvEncoding(gdioutput::toUTF8(allText), all);
  checkCsvEncoding("M\xFCller;J\xFCrgen;500101\r\nWei\xDF;Zo\xEB;ESV K\xF6ln\r\n", latin);
  checkCsvEncoding(utf16le(allText), all);
}

// The texts of a generated result list, in output order.
std::vector<std::wstring> resultListTexts(gdioutput &gdi, oEvent &oe) {
  gdi.clearPage(false);
  oListInfo li;
  oe.generateListInfo(gdi, EStdResultList, 0, li);
  oe.generateList(gdi, true, li, false);
  std::vector<std::wstring> texts;
  for (const TextInfo &ti : gdi.getTL())
    texts.push_back(ti.text);
  return texts;
}

std::ptrdiff_t indexOf(const std::vector<std::wstring> &texts, const std::wstring &text) {
  for (size_t k = 0; k < texts.size(); k++) {
    if (texts[k] == text)
      return std::ptrdiff_t(k);
  }
  return -1;
}

} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  if (!app_frame::initialize(hInstance, L"MeOS Load Test")) {
    std::fprintf(stderr, "initialization failed\n");
    return 1;
  }
  app_frame::registerWorkSpaceClass(hInstance);
  hWndMain = CreateWindowEx(0, app_frame::workSpaceClassName(), L"Load test", WS_OVERLAPPEDWINDOW, 0, 0, 800, 600,
                            NULL, NULL, hInstance, NULL);
  CHECK(hWndMain != nullptr);
  gdi_main->init(hWndMain, hWndMain, NULL);

  std::wstring file, zipFile;
  try {
    demo_competition::create(*gEvent);
    checkCompetition(*gEvent);

    file = getTempFile() + L".meos";
    CHECK(gEvent->save(file, true, false));

    // Open the saved file in a fresh competition.
    gEvent->newCompetition(L"");
    CHECK(gEvent->getNumRunners() == 0);
    CHECK(gEvent->open(file, false, false, false));
    checkCompetition(*gEvent);

    const std::vector<std::wstring> texts = resultListTexts(*gdi_main, *gEvent);
    const std::ptrdiff_t first = indexOf(texts, L"François Lefèvre");
    const std::ptrdiff_t second = indexOf(texts, L"Erik Sjöberg");
    const std::ptrdiff_t fourth = indexOf(texts, L"Günther Weiß");
    CHECK(first >= 0 && second > first && fourth > second);
    CHECK(indexOf(texts, L"36:30") > second);
    // Outside CHECK: its message is a narrow CP1252 literal, which cannot hold these letters.
    const std::ptrdiff_t polish = indexOf(texts, L"Łukasz Żółw");
    CHECK(polish >= 0);
    if (failures) {
      for (const std::wstring &text : texts)
        std::fprintf(stderr, "  list: %s\n", gdioutput::toUTF8(text).c_str());
    }

    // ZIP archive round trip (zip.cpp, used for backups and IOF archives).
    zipFile = getTempFile() + L".zip";
    CHECK(zip(zipFile.c_str(), nullptr, {file}) == 0);
    std::vector<std::wstring> extracted;
    unzip(zipFile.c_str(), nullptr, extracted);
    CHECK(extracted.size() == 1);
    if (extracted.size() == 1) {
      CHECK(extracted[0].find(L".meos") != std::wstring::npos);
      CHECK(!readFile(file).empty() && readFile(extracted[0]) == readFile(file));
      removeTempFile(extracted[0]);
    }

    checkFixedStrings(*gEvent);
    checkCsvEncodings();
  }
  catch (meosException &ex) {
    std::fprintf(stderr, "exception: %s\n", gdioutput::toUTF8(ex.wwhat()).c_str());
    failures++;
  }
  catch (std::exception &ex) {
    std::fprintf(stderr, "exception: %s\n", ex.what());
    failures++;
  }

  if (!file.empty())
    removeTempFile(file);
  if (!zipFile.empty())
    removeTempFile(zipFile);
  gEvent->newCompetition(L"");
  app_frame::shutdown();

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("core load test passed\n");
  return 0;
}
