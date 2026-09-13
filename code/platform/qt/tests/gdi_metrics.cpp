/************************************************************************
    MeOS - Orienteering Software
    Linux port: text metrics of GDI, for comparing Windows with the Qt backend.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// A plain Win32 program: it measures text in the fonts gdioutput uses
// (GDIImplFontSet) and writes the results as CSV. The same source runs with real
// GDI on Windows and on the Qt backend on Linux; gdi_metrics_compare.cpp compares
// the two outputs.
//
// Windows (Developer Command Prompt):
//   cl /EHsc /utf-8 /DUNICODE /D_UNICODE gdi_metrics.cpp user32.lib gdi32.lib /link /SUBSYSTEM:WINDOWS
//   gdi_metrics.exe gdi_metrics_windows.csv
// Linux: built by CMake as meos_gdi_metrics and run by ctest (offscreen).

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace {

const wchar_t *const faces[] = {L"Arial", L"Segoe UI", L"Times New Roman", L"Lucida Console"};

// The cell heights of GDIImplFontSet at scale 1 (11, 12, 14, 18, 24, 34) and those
// it uses for Segoe UI, which it scales by 1.1.
const int heights[] = {11, 12, 13, 14, 15, 18, 19, 24, 26, 34, 37};

struct Style {
  const char *name;
  int weight;
  bool italic;
};
const Style styles[] = {{"normal", FW_NORMAL, false}, {"bold", FW_BOLD, false}, {"italic", FW_NORMAL, true}};

// Names with umlauts, times, digits, the texts MeOS measures itself, and texts for
// line breaks.
const wchar_t *const texts[] = {
    L"Anna Müller",
    L"Åsa Öberg-Ähnlich",
    L"Tävlingsklubben Göteborg OK",
    L"12:34:56",
    L"1234567890",
    L"+0:12",
    L"Mr. Fantom of 43 Years",
    L"Goliat Meze 1234:5678",
    L"123456789ABCDEFGHIJHKLMNOPQRSTUVXYZ abcdefghijklmnopqrstuvxyz",
    L"M",
    L"WWWWW iiiii",
    L"Löpare, klass och sträcka: resultat efter kontroll 3",
    L"one two three four five six seven eight nine ten eleven twelve",
    L"a extraordinarilylongwordwithoutanyspace b",
    L"Line one\nLine two",
};

int CALLBACK firstFont(const LOGFONT * /*logFont*/, const TEXTMETRIC *metric, DWORD /*type*/, LPARAM data) {
  *reinterpret_cast<TEXTMETRIC *>(data) = *metric;
  return 0;
}

std::string narrow(const wchar_t *text) {
  std::string result;
  for (; *text; text++)
    result += char(*text);
  return result;
}

void row(FILE *out, const wchar_t *face, int height, const char *style, const char *measure, int text, long width,
         long textHeight) {
  std::fprintf(out, "%s,%d,%s,%s,%d,%ld,%ld\n", narrow(face).c_str(), height, style, measure, text, width,
               textHeight);
}

void measureFont(FILE *out, HDC dc, const wchar_t *face, int height, const Style &style) {
  HFONT font = CreateFont(height, 0, 0, 0, style.weight, style.italic, false, false, DEFAULT_CHARSET,
                          OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY, DEFAULT_PITCH | FF_ROMAN, face);
  HGDIOBJ old = SelectObject(dc, font);

  const int count = int(sizeof(texts) / sizeof(texts[0]));
  for (int t = 0; t < count; t++) {
    const int length = int(std::wcslen(texts[t]));
    SIZE size = {};
    GetTextExtentPoint32(dc, texts[t], length, &size);
    row(out, face, height, style.name, "extent", t, size.cx, size.cy);

    RECT rc = {0, 0, 0, 0};
    DrawText(dc, texts[t], length, &rc, DT_CALCRECT | DT_NOPREFIX);
    row(out, face, height, style.name, "calcrect", t, rc.right, rc.bottom);

    rc = {0, 0, 150, 0};
    DrawText(dc, texts[t], length, &rc, DT_CALCRECT | DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
    row(out, face, height, style.name, "wordbreak150", t, rc.right, rc.bottom);
  }

  // gdioutput measures limited texts with DT_END_ELLIPSIS in an empty rectangle.
  RECT rc = {0, 0, 0, 0};
  DrawText(dc, texts[6], -1, &rc, DT_CALCRECT | DT_END_ELLIPSIS);
  row(out, face, height, style.name, "ellipsis0", 6, rc.right, rc.bottom);

  // Empty text, single and multiple lines.
  rc = {0, 0, 50, 50};
  DrawText(dc, L"", 0, &rc, DT_CALCRECT | DT_SINGLELINE);
  row(out, face, height, style.name, "emptysingle", -1, rc.right, rc.bottom);
  rc = {0, 0, 50, 50};
  DrawText(dc, L"", 0, &rc, DT_CALCRECT | DT_WORDBREAK);
  row(out, face, height, style.name, "emptymulti", -1, rc.right, rc.bottom);

  SelectObject(dc, old);
  DeleteObject(font);
}

} // namespace

int WINAPI WinMain(HINSTANCE /*instance*/, HINSTANCE /*prevInstance*/, LPSTR commandLine, int /*showCommand*/) {
  std::string path = commandLine && *commandLine ? commandLine : "gdi_metrics.csv";
  if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
    path = path.substr(1, path.size() - 2);
  FILE *out = std::fopen(path.c_str(), "w");
  if (!out)
    return 1;
  std::fprintf(out, "face,height,style,measure,text,width,height\n");

  HDC dc = GetDC(nullptr);
  for (const wchar_t *face : faces) {
    for (int height : heights) {
      for (const Style &style : styles)
        measureFont(out, dc, face, height, style);
    }

    // The metrics EnumFontFamiliesEx reports; MeOS derives a relative scale from them.
    LOGFONT logFont = {};
    logFont.lfCharSet = DEFAULT_CHARSET;
    std::wcsncpy(logFont.lfFaceName, face, LF_FACESIZE - 1);
    TEXTMETRIC metric = {};
    EnumFontFamiliesEx(dc, &logFont, firstFont, LPARAM(&metric), 0);
    row(out, face, 0, "normal", "enum", -1, metric.tmAveCharWidth, metric.tmHeight);
  }
  // The stock font of the buttons and input fields.
  SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
  SIZE size = {};
  GetTextExtentPoint32(dc, texts[8], int(std::wcslen(texts[8])), &size);
  row(out, L"DEFAULT_GUI_FONT", 0, "normal", "extent", 8, size.cx, size.cy);

  ReleaseDC(nullptr, dc);
  std::fclose(out);
  return 0;
}
