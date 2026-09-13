/************************************************************************
    MeOS - Orienteering Software
    Linux port: self test of the GDI subset on Qt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Checks device contexts, shapes, bitmaps, the backing store of windows and text
// output of code/platform/qt pixel by pixel against the GDI behaviour MeOS relies
// on. Runs with QT_QPA_PLATFORM=offscreen under ctest. Exits with a non-zero
// status if a check fails.

#include "platform/qt/win32_gdi.h"

#include <QEventLoop>
#include <QPixmap>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

std::atomic<int> failures{0};

#define CHECK(expr)                                                                   \
  do {                                                                                \
    if (!(expr)) {                                                                    \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr);   \
      failures++;                                                                     \
    }                                                                                 \
  } while (false)

const COLORREF white = RGB(255, 255, 255);
const COLORREF black = RGB(0, 0, 0);
const COLORREF red = RGB(255, 0, 0);
const COLORREF green = RGB(0, 200, 0);
const COLORREF blue = RGB(0, 0, 255);

void processEvents() {
  for (int i = 0; i < 5; i++)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// A memory DC with a white top-down 32-bit DIB section selected.
class Dib {
public:
  Dib(int width, int height) : width(width), height(height) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *memory = nullptr;
    bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &memory, nullptr, 0);
    bits = static_cast<std::uint32_t *>(memory);
    dc = CreateCompatibleDC(nullptr);
    old = SelectObject(dc, bitmap);
    fill(white);
  }
  ~Dib() {
    SelectObject(dc, old);
    DeleteDC(dc);
    DeleteObject(bitmap);
  }
  Dib(const Dib &) = delete;
  Dib &operator=(const Dib &) = delete;

  void fill(COLORREF color) {
    for (int i = 0; i < width * height; i++)
      bits[i] = 0xFF000000u | (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color);
  }

  COLORREF pixel(int x, int y) const {
    const std::uint32_t value = bits[y * width + x];
    return RGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
  }

  // Bounding box of the pixels that differ from white; empty if there are none.
  QRect ink() const {
    QRect box;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        if (pixel(x, y) != white)
          box |= QRect(x, y, 1, 1);
      }
    }
    return box;
  }

  const int width;
  const int height;
  HBITMAP bitmap = nullptr;
  HDC dc = nullptr;
  HGDIOBJ old = nullptr;
  std::uint32_t *bits = nullptr;
};

bool near(COLORREF a, COLORREF b, int tolerance = 2) {
  return std::abs(GetRValue(a) - GetRValue(b)) <= tolerance && std::abs(GetGValue(a) - GetGValue(b)) <= tolerance &&
         std::abs(GetBValue(a) - GetBValue(b)) <= tolerance;
}

void useColors(HDC dc, COLORREF pen, COLORREF brush) {
  SelectObject(dc, GetStockObject(DC_PEN));
  SelectObject(dc, GetStockObject(DC_BRUSH));
  SetDCPenColor(dc, pen);
  SetDCBrushColor(dc, brush);
}

HFONT arial(int height, int escapement = 0) {
  return CreateFont(height, 0, escapement, escapement, FW_NORMAL, false, false, false, DEFAULT_CHARSET,
                    OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY, DEFAULT_PITCH | FF_ROMAN, L"Arial");
}

int textWidth(HDC dc, const wchar_t *text) {
  SIZE size = {};
  GetTextExtentPoint32(dc, text, int(std::wcslen(text)), &size);
  return size.cx;
}

/* ------------------------------------------------------------------ */

void testObjects() {
  CHECK(GetStockObject(WHITE_BRUSH) != nullptr && GetStockObject(WHITE_BRUSH) == GetStockObject(WHITE_BRUSH));
  CHECK(GetStockObject(DEFAULT_GUI_FONT) != nullptr && GetStockObject(9) == nullptr);
  CHECK(DeleteObject(GetStockObject(BLACK_PEN)) && GetStockObject(BLACK_PEN) != nullptr);

  Dib dib(20, 20);
  HGDIOBJ stockPen = GetStockObject(BLACK_PEN);
  HPEN pen = CreatePen(PS_SOLID, 1, blue);
  CHECK(SelectObject(dib.dc, pen) == stockPen);
  // Deleted while selected: the DC keeps drawing with it, the handle is gone.
  CHECK(DeleteObject(pen));
  MoveToEx(dib.dc, 0, 5, nullptr);
  CHECK(LineTo(dib.dc, 10, 5) && dib.pixel(3, 5) == blue);
  CHECK(SelectObject(dib.dc, pen) == nullptr);
  CHECK(SelectObject(dib.dc, stockPen) == pen);

  CHECK(SetBkMode(dib.dc, TRANSPARENT) == OPAQUE && SetBkMode(dib.dc, 7) == 0);
  CHECK(SetTextColor(dib.dc, red) == black && SetTextColor(dib.dc, black) == red);
  CHECK(SetDCPenColor(dib.dc, green) == black && GetDCPenColor(dib.dc) == green);
  CHECK(SetBkColor(nullptr, red) == CLR_INVALID);

  // DCs are released by the function that matches how they were obtained.
  HDC screen = GetDC(nullptr);
  CHECK(screen != nullptr && DeleteDC(screen) == FALSE && ReleaseDC(nullptr, dib.dc) == 0);
  CHECK(ReleaseDC(nullptr, screen) == 1 && ReleaseDC(nullptr, screen) == 0);

  // A bitmap is only selected into memory DCs; an empty size gives 1x1.
  HBITMAP empty = CreateCompatibleBitmap(dib.dc, 0, 0);
  CHECK(empty != nullptr);
  CHECK(DeleteObject(empty) && !DeleteObject(empty));

  // Only top-down 32-bit DIB sections are supported.
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = 4;
  info.bmiHeader.biHeight = 4;
  info.bmiHeader.biBitCount = 32;
  void *bits = &info;
  CHECK(CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0) == nullptr && bits == nullptr);

  CHECK(GetSysColor(COLOR_WINDOW) == white);
  CHECK(GetSysColor(COLOR_INFOBK) == RGB(255, 255, 225));
  CHECK(GetSysColor(COLOR_ACTIVECAPTION) == RGB(153, 180, 209));
  CHECK(GetSysColor(COLOR_3DFACE) == RGB(240, 240, 240));
}

void testRectangles() {
  Dib dib(40, 30);
  useColors(dib.dc, red, blue);

  // The pen draws the outermost pixels; right and bottom are exclusive.
  CHECK(Rectangle(dib.dc, 5, 5, 15, 12));
  CHECK(dib.pixel(5, 5) == red && dib.pixel(14, 5) == red && dib.pixel(5, 11) == red && dib.pixel(14, 11) == red);
  CHECK(dib.pixel(15, 5) == white && dib.pixel(5, 12) == white && dib.pixel(4, 4) == white);
  CHECK(dib.pixel(6, 6) == blue && dib.pixel(13, 10) == blue);

  // Without a pen, the filled area is one pixel smaller.
  SelectObject(dib.dc, GetStockObject(NULL_PEN));
  CHECK(Rectangle(dib.dc, 20, 5, 30, 12));
  CHECK(dib.pixel(20, 5) == blue && dib.pixel(28, 10) == blue);
  CHECK(dib.pixel(29, 10) == white && dib.pixel(28, 11) == white);

  // Without a brush, only the outline.
  dib.fill(white);
  SelectObject(dib.dc, GetStockObject(BLACK_PEN));
  SelectObject(dib.dc, GetStockObject(NULL_BRUSH));
  CHECK(Rectangle(dib.dc, 12, 10, 2, 2)); // corners given reversed
  CHECK(dib.pixel(2, 2) == black && dib.pixel(11, 9) == black && dib.pixel(5, 5) == white && dib.pixel(12, 10) == white);

  // The clip rectangle limits drawing.
  dib.fill(white);
  useColors(dib.dc, red, red);
  CHECK(IntersectClipRect(dib.dc, 10, 10, 20, 20) == SIMPLEREGION);
  Rectangle(dib.dc, 0, 0, 40, 30);
  CHECK(dib.ink() == QRect(10, 10, 10, 10));
  CHECK(IntersectClipRect(dib.dc, 30, 30, 35, 35) == NULLREGION);
}

void testLinesAndPolygons() {
  Dib dib(40, 40);
  SelectObject(dib.dc, GetStockObject(BLACK_PEN));

  // The end point is not drawn, in every direction.
  POINT previous = {};
  CHECK(MoveToEx(dib.dc, 2, 20, nullptr) && LineTo(dib.dc, 10, 20));
  CHECK(dib.pixel(2, 20) == black && dib.pixel(9, 20) == black && dib.pixel(10, 20) == white);
  CHECK(MoveToEx(dib.dc, 30, 25, &previous) && previous.x == 10 && previous.y == 20);
  CHECK(LineTo(dib.dc, 30, 20));
  CHECK(dib.pixel(30, 25) == black && dib.pixel(30, 21) == black && dib.pixel(30, 20) == white);
  MoveToEx(dib.dc, 0, 0, nullptr);
  CHECK(LineTo(dib.dc, 5, 5));
  CHECK(dib.pixel(0, 0) == black && dib.pixel(4, 4) == black && dib.pixel(5, 5) == white && dib.pixel(1, 0) == white);
  MoveToEx(dib.dc, 20, 10, nullptr);
  LineTo(dib.dc, 14, 10);
  CHECK(dib.pixel(20, 10) == black && dib.pixel(15, 10) == black && dib.pixel(14, 10) == white);

  // Polygons are closed and filled with the brush.
  dib.fill(white);
  useColors(dib.dc, black, green);
  const POINT triangle[] = {{5, 5}, {35, 5}, {20, 35}};
  CHECK(Polygon(dib.dc, triangle, 3));
  CHECK(dib.pixel(20, 12) == green && dib.pixel(5, 5) == black && dib.pixel(35, 5) == black && dib.pixel(20, 35) == black);
  CHECK(dib.pixel(20, 20) == green && dib.pixel(3, 20) == white);
  CHECK(!Polygon(dib.dc, triangle, 1));

  // Ellipses stay inside their bounding box.
  dib.fill(white);
  CHECK(Ellipse(dib.dc, 10, 10, 30, 20));
  CHECK(dib.ink() == QRect(10, 10, 20, 10));
  CHECK(dib.pixel(20, 15) == green);
}

void testGradients() {
  Dib dib(30, 30);
  TRIVERTEX vertices[2] = {{0, 0, 0xFF00, 0, 0, 0}, {10, 4, 0, 0, 0xFF00, 0}};
  GRADIENT_RECT rect = {0, 1};

  // The first column has the colour of the first vertex; the last step is one
  // tenth short of the second.
  CHECK(GradientFill(dib.dc, vertices, 2, &rect, 1, GRADIENT_FILL_RECT_H));
  CHECK(dib.pixel(0, 0) == RGB(255, 0, 0) && dib.pixel(0, 3) == RGB(255, 0, 0));
  CHECK(dib.pixel(9, 0) == RGB(25, 0, 229) && dib.pixel(5, 2) == RGB(127, 0, 127));
  CHECK(dib.pixel(10, 0) == white && dib.pixel(0, 4) == white);

  dib.fill(white);
  vertices[0].x = 5;
  vertices[0].y = 20;
  vertices[1].x = 25;
  vertices[1].y = 10;
  CHECK(GradientFill(dib.dc, vertices, 2, &rect, 1, GRADIENT_FILL_RECT_V));
  // Given bottom-up, the colours stay with their vertices.
  CHECK(dib.pixel(5, 10) == RGB(0, 0, 255) && dib.pixel(24, 19) == RGB(229, 0, 25));
  CHECK(dib.ink() == QRect(5, 10, 20, 10));

  GRADIENT_RECT bad = {0, 2};
  CHECK(!GradientFill(dib.dc, vertices, 2, &bad, 1, GRADIENT_FILL_RECT_H));
  CHECK(!GradientFill(dib.dc, vertices, 2, &rect, 1, 2 /* GRADIENT_FILL_TRIANGLE */));
}

void testBitmaps() {
  Dib target(20, 20);
  Dib source(8, 8);

  // Premultiplied alpha: 50 % red over white.
  for (int i = 0; i < 64; i++)
    source.bits[i] = 0x80800000u;
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  CHECK(AlphaBlend(target.dc, 2, 2, 4, 4, source.dc, 0, 0, 4, 4, blend));
  CHECK(near(target.pixel(2, 2), RGB(255, 127, 127)) && near(target.pixel(5, 5), RGB(255, 127, 127)));
  CHECK(target.pixel(6, 6) == white && target.pixel(1, 1) == white);

  // Constant alpha on top, and scaling to the target size.
  target.fill(white);
  blend.SourceConstantAlpha = 0;
  CHECK(AlphaBlend(target.dc, 0, 0, 16, 16, source.dc, 0, 0, 8, 8, blend) && target.ink().isEmpty());
  blend.SourceConstantAlpha = 255;
  CHECK(AlphaBlend(target.dc, 0, 0, 16, 16, source.dc, 0, 0, 8, 8, blend));
  CHECK(target.ink() == QRect(0, 0, 16, 16));

  // The source rectangle must lie inside the bitmap.
  CHECK(!AlphaBlend(target.dc, 0, 0, 4, 4, source.dc, 6, 6, 4, 4, blend));

  // BitBlt copies in both directions, clipped to the source.
  source.fill(blue);
  target.fill(white);
  CHECK(BitBlt(target.dc, 15, 15, 10, 10, source.dc, 0, 0, SRCCOPY));
  CHECK(target.pixel(15, 15) == blue && target.pixel(19, 19) == blue && target.pixel(14, 14) == white);
  CHECK(BitBlt(source.dc, 0, 0, 8, 8, target.dc, 12, 12, SRCCOPY));
  CHECK(source.pixel(0, 0) == white && source.pixel(3, 3) == blue && source.pixel(2, 2) == white);

  // A compatible bitmap in a memory DC.
  HDC memory = CreateCompatibleDC(target.dc);
  HBITMAP bitmap = CreateCompatibleBitmap(target.dc, 6, 6);
  HGDIOBJ old = SelectObject(memory, bitmap);
  CHECK(old != nullptr);
  CHECK(BitBlt(memory, 0, 0, 6, 6, target.dc, 14, 14, SRCCOPY));
  target.fill(white);
  CHECK(BitBlt(target.dc, 0, 0, 6, 6, memory, 0, 0, SRCCOPY));
  CHECK(target.pixel(0, 0) == white && target.pixel(1, 1) == blue && target.pixel(5, 5) == blue);
  CHECK(SelectObject(memory, old) == bitmap);
  CHECK(DeleteDC(memory) && DeleteObject(bitmap));
}

/* ------------------------------------------------------------------ */

COLORREF paintColor = white;
RECT lastPaintRect;
BOOL lastErase;

// Fills the update region with paintColor.
LRESULT CALLBACK fillProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window, &ps);
    lastPaintRect = ps.rcPaint;
    lastErase = ps.fErase;
    useColors(dc, paintColor, paintColor);
    SelectObject(dc, GetStockObject(NULL_PEN));
    Rectangle(dc, -1, -1, 1000, 1000);
    EndPaint(window, &ps);
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

// Paints nothing, and leaves erasing to DefWindowProc.
LRESULT CALLBACK emptyPaintProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_PAINT) {
    PAINTSTRUCT ps;
    BeginPaint(window, &ps);
    lastPaintRect = ps.rcPaint;
    lastErase = ps.fErase;
    EndPaint(window, &ps);
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

LRESULT CALLBACK noEraseProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_ERASEBKGND)
    return 0;
  return emptyPaintProc(window, message, wParam, lParam);
}

ATOM registerClass(const wchar_t *name, WNDPROC proc, HBRUSH background = nullptr) {
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = proc;
  wc.hInstance = meos_qt::applicationInstance();
  wc.hbrBackground = background;
  wc.lpszClassName = name;
  return RegisterClassEx(&wc);
}

HWND createWindow(const wchar_t *className, int width, int height) {
  return CreateWindowEx(0, className, L"", WS_POPUP | WS_VISIBLE, 0, 0, width, height, nullptr, nullptr,
                        meos_qt::applicationInstance(), nullptr);
}

// A pixel of the backing store of a window, read back with BitBlt.
COLORREF windowPixel(HWND window, int x, int y) {
  Dib dib(1, 1);
  HDC dc = GetDC(window);
  BitBlt(dib.dc, 0, 0, 1, 1, dc, x, y, SRCCOPY);
  ReleaseDC(window, dc);
  return dib.pixel(0, 0);
}

// A pixel as the client widget shows it on the screen.
COLORREF screenPixel(HWND window, int x, int y) {
  const QImage image = meos_qt::findWindow(window)->client->grab().toImage();
  const QColor color = image.pixelColor(x, y);
  return RGB(color.red(), color.green(), color.blue());
}

void testWindowPainting() {
  CHECK(registerClass(L"GdiTestFill", fillProc) != 0);
  HWND window = createWindow(L"GdiTestFill", 60, 50);
  paintColor = blue;
  CHECK(UpdateWindow(window));
  CHECK(windowPixel(window, 0, 0) == blue && windowPixel(window, 59, 49) == blue);

  // BeginPaint limits drawing to the update region.
  paintColor = green;
  RECT rect = {5, 6, 15, 26};
  CHECK(InvalidateRect(window, &rect, FALSE) && UpdateWindow(window));
  CHECK(EqualRect(&lastPaintRect, &rect) && !lastErase);
  CHECK(windowPixel(window, 5, 6) == green && windowPixel(window, 14, 25) == green);
  CHECK(windowPixel(window, 15, 6) == blue && windowPixel(window, 5, 26) == blue);

  // Drawing through GetDC, outside WM_PAINT, reaches the screen.
  HDC dc = GetDC(window);
  useColors(dc, red, red);
  Rectangle(dc, 30, 30, 40, 40);
  CHECK(ReleaseDC(window, dc) == 1);
  CHECK(windowPixel(window, 35, 35) == red);
  processEvents();
  CHECK(screenPixel(window, 35, 35) == red && screenPixel(window, 10, 10) == green && screenPixel(window, 50, 5) == blue);

  // BitBlt reads back from the window and writes into it.
  Dib dib(10, 10);
  dc = GetDC(window);
  CHECK(BitBlt(dib.dc, 0, 0, 10, 10, dc, 30, 30, SRCCOPY) && dib.pixel(0, 0) == red && dib.pixel(9, 9) == red);
  dib.fill(white);
  CHECK(BitBlt(dc, 0, 0, 10, 10, dib.dc, 0, 0, SRCCOPY));
  ReleaseDC(window, dc);
  CHECK(windowPixel(window, 9, 9) == white && windowPixel(window, 10, 10) == green);

  // Resizing keeps the content and paints the new area.
  paintColor = RGB(10, 20, 30);
  CHECK(SetWindowPos(window, nullptr, 0, 0, 80, 50, SWP_NOMOVE | SWP_NOZORDER));
  CHECK(UpdateWindow(window));
  CHECK(windowPixel(window, 70, 10) == RGB(10, 20, 30));
  CHECK(DestroyWindow(window));

  // WM_ERASEBKGND: DefWindowProc fills with the class brush.
  HBRUSH brush = CreateSolidBrush(RGB(1, 2, 3));
  CHECK(registerClass(L"GdiTestErase", emptyPaintProc, brush) != 0);
  window = createWindow(L"GdiTestErase", 30, 30);
  CHECK(UpdateWindow(window));
  CHECK(!lastErase && windowPixel(window, 29, 29) == RGB(1, 2, 3));
  CHECK(DestroyWindow(window));

  // A system colour index + 1 stands for the system colour.
  CHECK(registerClass(L"GdiTestNoErase", noEraseProc, reinterpret_cast<HBRUSH>(std::intptr_t(COLOR_INFOBK + 1))) != 0);
  window = createWindow(L"GdiTestNoErase", 30, 30);
  CHECK(UpdateWindow(window));
  CHECK(lastErase && windowPixel(window, 5, 5) == white);
  CHECK(DefWindowProc(window, WM_ERASEBKGND, WPARAM(GetDC(window)), 0) == 1);
  CHECK(windowPixel(window, 5, 5) == RGB(255, 255, 225));
  CHECK(DestroyWindow(window));
  DeleteObject(brush);
}

void testScrollWindow() {
  CHECK(registerClass(L"GdiTestScroll", emptyPaintProc) != 0);
  HWND window = createWindow(L"GdiTestScroll", 100, 100);
  HWND child = CreateWindowEx(0, L"GdiTestScroll", L"", WS_CHILD | WS_VISIBLE, 10, 60, 20, 10, window, nullptr,
                              meos_qt::applicationInstance(), nullptr);
  CHECK(UpdateWindow(window) && UpdateWindow(child));
  CHECK(meos_qt::findWindow(window)->updateRegion.isEmpty());

  HDC dc = GetDC(window);
  useColors(dc, red, red);
  Rectangle(dc, 0, 50, 100, 52);
  ReleaseDC(window, dc);

  // The content moves up, the uncovered strip at the bottom becomes invalid.
  RECT uncovered;
  CHECK(ScrollWindowEx(window, 0, -10, nullptr, nullptr, nullptr, &uncovered, SW_INVALIDATE) == SIMPLEREGION);
  CHECK(windowPixel(window, 50, 40) == red && windowPixel(window, 50, 41) == red && windowPixel(window, 50, 50) != red);
  CHECK(uncovered.left == 0 && uncovered.top == 90 && uncovered.right == 100 && uncovered.bottom == 100);
  CHECK(meos_qt::findWindow(window)->updateRegion == QRegion(0, 90, 100, 10));
  processEvents();
  CHECK(screenPixel(window, 50, 40) == red);

  // Without SW_INVALIDATE nothing is invalidated; a pending update region moves along.
  CHECK(UpdateWindow(window));
  dc = GetDC(window);
  useColors(dc, blue, blue);
  Rectangle(dc, 0, 40, 2, 42);
  ReleaseDC(window, dc);
  RECT pending = {0, 20, 10, 30};
  InvalidateRect(window, &pending, FALSE);
  CHECK(ScrollWindowEx(window, 5, 0, nullptr, nullptr, nullptr, nullptr, 0) == SIMPLEREGION);
  CHECK(meos_qt::findWindow(window)->updateRegion == QRegion(0, 20, 15, 10));
  CHECK(windowPixel(window, 0, 40) == blue && windowPixel(window, 5, 40) == blue && windowPixel(window, 6, 41) == blue);
  CHECK(windowPixel(window, 4, 40) == red && windowPixel(window, 7, 40) == red);

  // The scroll and clip rectangles limit the moved pixels; children move along.
  CHECK(UpdateWindow(window));
  RECT scroll = {0, 0, 50, 100};
  RECT clip = {0, 0, 100, 60};
  CHECK(ScrollWindowEx(window, 0, 10, &scroll, &clip, nullptr, &uncovered, SW_INVALIDATE | SW_SCROLLCHILDREN) ==
        SIMPLEREGION);
  CHECK(windowPixel(window, 20, 50) == red && windowPixel(window, 70, 50) != red && windowPixel(window, 70, 40) == red);
  CHECK(uncovered.left == 0 && uncovered.top == 0 && uncovered.right == 50 && uncovered.bottom == 10);
  RECT childRect;
  CHECK(GetWindowRect(child, &childRect) && childRect.top == 70 && childRect.left == 10);
  CHECK(DestroyWindow(window));
}

void testDrawingFromThread() {
  CHECK(registerClass(L"GdiTestThread", fillProc) != 0);
  paintColor = white;
  HWND window = createWindow(L"GdiTestThread", 120, 40);
  CHECK(UpdateWindow(window));

  // As TextFader: a helper thread draws through GetDC while the GUI thread paints.
  std::atomic<bool> done{false};
  std::thread helper([window, &done] {
    HDC dc = GetDC(window);
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < 40; i++) {
      SetTextColor(dc, RGB(0, 0, i));
      TextOut(dc, 60, 5, L"Fade", 4);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ReleaseDC(window, dc);
    done = true;
  });
  RECT left = {0, 0, 50, 40};
  while (!done) {
    InvalidateRect(window, &left, FALSE);
    UpdateWindow(window);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
  }
  helper.join();
  processEvents();

  Dib dib(60, 40);
  HDC dc = GetDC(window);
  BitBlt(dib.dc, 0, 0, 60, 40, dc, 60, 0, SRCCOPY);
  ReleaseDC(window, dc);
  CHECK(!dib.ink().isEmpty() && dib.ink().left() >= 0);
  CHECK(DestroyWindow(window));
}

/* ------------------------------------------------------------------ */

void testFontHeights() {
  HDC dc = GetDC(nullptr);
  HFONT cell = arial(20);
  HFONT em = arial(-20);
  SelectObject(dc, cell);
  SIZE size = {};
  CHECK(GetTextExtentPoint32(dc, L"Hello", 5, &size) && size.cx > 20);
  // A positive height is the cell height, a negative one the em height.
  CHECK(std::abs(size.cy - 20) <= 1);
  SelectObject(dc, em);
  const int widthEm = textWidth(dc, L"Hello");
  CHECK(GetTextExtentPoint32(dc, L"", 0, &size) && size.cx == 0 && size.cy >= 22);
  CHECK(widthEm > textWidth(dc, L"Hell"));

  // Neither kerning nor ligatures.
  CHECK(textWidth(dc, L"AV") == textWidth(dc, L"A") + textWidth(dc, L"V"));
  CHECK(textWidth(dc, L"fi") == textWidth(dc, L"f") + textWidth(dc, L"i"));
  CHECK(textWidth(dc, L"Tä") == textWidth(dc, L"T") + textWidth(dc, L"ä"));

  // The narrow variant measures the CP1252 text.
  SIZE narrow = {};
  CHECK(GetTextExtentPoint32A(dc, "Mr. Fantom", 10, &narrow) && narrow.cx == textWidth(dc, L"Mr. Fantom"));

  // Wider for bold, and the stock GUI font is small.
  HFONT bold = CreateFont(20, 0, 0, 0, FW_BOLD, false, false, false, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                          CLIP_DEFAULT_PRECIS, PROOF_QUALITY, DEFAULT_PITCH | FF_ROMAN, L"Arial");
  SelectObject(dc, cell);
  const int normalWidth = textWidth(dc, L"Orienteering");
  SelectObject(dc, bold);
  CHECK(textWidth(dc, L"Orienteering") > normalWidth);
  SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
  CHECK(GetTextExtentPoint32(dc, L"M", 1, &size) && size.cy >= 12 && size.cy <= 15);

  ReleaseDC(nullptr, dc);
  DeleteObject(cell);
  DeleteObject(em);
  DeleteObject(bold);

  // Substitutes exist for the faces MeOS asks for.
  CHECK(!meos_qt::substituteFamily(L"Segoe UI", 0).isEmpty());
  CHECK(!meos_qt::substituteFamily(L"", FF_MODERN).isEmpty());
}

void testDrawTextLayout() {
  HDC dc = GetDC(nullptr);
  HFONT font = arial(16);
  SelectObject(dc, font);
  SIZE line = {};
  GetTextExtentPoint32(dc, L"Hello", 5, &line);
  const int h = line.cy;

  // DT_CALCRECT keeps left and top and bounds the text.
  RECT rc = {10, 20, 10, 20};
  CHECK(DrawText(dc, L"Hello", -1, &rc, DT_CALCRECT | DT_NOPREFIX) == h);
  CHECK(rc.left == 10 && rc.top == 20 && rc.right == 10 + line.cx && rc.bottom == 20 + h);
  rc = {0, 0, 1000, 1000};
  CHECK(DrawText(dc, L"Hello", 5, &rc, DT_CALCRECT | DT_CENTER | DT_NOPREFIX) == h && rc.right == line.cx);

  // Word breaks between words; the width shrinks to the widest line.
  const int oneTwo = textWidth(dc, L"one two");
  rc = {0, 0, oneTwo, 0};
  CHECK(DrawText(dc, L"one two three", -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) == 2 * h);
  CHECK(rc.right == std::max(oneTwo, textWidth(dc, L"three")) && rc.bottom == 2 * h);
  rc = {0, 0, oneTwo - 1, 0};
  CHECK(DrawText(dc, L"one two three", -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) == 3 * h);
  CHECK(rc.right == textWidth(dc, L"three"));
  rc = {0, 0, 1000, 0};
  CHECK(DrawText(dc, L"one two three", -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) == h);
  CHECK(rc.right == textWidth(dc, L"one two three"));

  // A word wider than the rectangle is not broken; the rectangle grows.
  rc = {0, 0, textWidth(dc, L"a"), 0};
  CHECK(DrawText(dc, L"a extraordinarily b", -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) == 3 * h);
  CHECK(rc.right == textWidth(dc, L"extraordinarily"));

  // The space at a break counts if it still fits, except for centred text
  // (measured on Windows).
  const int aaaSpace = textWidth(dc, L"aaa ");
  rc = {0, 0, aaaSpace, 0};
  CHECK(DrawText(dc, L"aaa bb", -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) == 2 * h && rc.right == aaaSpace);
  rc = {0, 0, aaaSpace - 1, 0};
  CHECK(DrawText(dc, L"aaa bb", -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) == 2 * h);
  CHECK(rc.right == textWidth(dc, L"aaa"));
  rc = {0, 0, aaaSpace, 0};
  CHECK(DrawText(dc, L"aaa bb", -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX) == 2 * h);
  CHECK(rc.right == textWidth(dc, L"aaa"));

  // A word wider than the rectangle widens it for all lines.
  rc = {0, 0, 10, 0};
  CHECK(DrawText(dc, L"abcdefghijklmnop ab cd", -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) == 2 * h);
  CHECK(rc.right == textWidth(dc, L"abcdefghijklmnop"));

  // Several spaces at a break are dropped.
  rc = {0, 0, textWidth(dc, L"one"), 0};
  CHECK(DrawText(dc, L"one   two", -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) == 2 * h);
  CHECK(rc.right == std::max(textWidth(dc, L"one"), textWidth(dc, L"two")));

  // Line breaks, except with DT_SINGLELINE; a final line break adds no line.
  rc = {0, 0, 0, 0};
  CHECK(DrawText(dc, L"a\nbb", -1, &rc, DT_CALCRECT | DT_NOPREFIX) == 2 * h && rc.right == textWidth(dc, L"bb"));
  rc = {0, 0, 0, 0};
  CHECK(DrawText(dc, L"a\r\nb\n", -1, &rc, DT_CALCRECT | DT_NOPREFIX) == 2 * h);
  rc = {0, 0, 0, 0};
  CHECK(DrawText(dc, L"a\nbb", -1, &rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX) == h);
  CHECK(rc.right == textWidth(dc, L"abb"));

  // Empty text.
  rc = {5, 5, 50, 50};
  CHECK(DrawText(dc, L"", 0, &rc, DT_CALCRECT | DT_SINGLELINE) == 0 && rc.right == 5 && rc.bottom == 5 + h);
  rc = {5, 5, 50, 50};
  CHECK(DrawText(dc, L"", -1, &rc, DT_CALCRECT | DT_WORDBREAK) == 0 && rc.right == 5 && rc.bottom == 5);

  // Prefixes: "&&" is "&", "&x" underlines x.
  rc = {0, 0, 0, 0};
  DrawText(dc, L"a&&b", -1, &rc, DT_CALCRECT);
  CHECK(rc.right == textWidth(dc, L"a&b"));
  rc = {0, 0, 0, 0};
  DrawText(dc, L"&File", -1, &rc, DT_CALCRECT);
  CHECK(rc.right == textWidth(dc, L"File"));
  rc = {0, 0, 0, 0};
  DrawText(dc, L"&File", -1, &rc, DT_CALCRECT | DT_NOPREFIX);
  CHECK(rc.right == textWidth(dc, L"&File"));

  // DT_CALCRECT with DT_END_ELLIPSIS measures the shortened text, which keeps at
  // least one character (measured on Windows).
  rc = {0, 0, 0, 0};
  DrawText(dc, L"Orienteering", -1, &rc, DT_CALCRECT | DT_END_ELLIPSIS);
  CHECK(rc.right == textWidth(dc, L"O..."));
  rc = {0, 0, textWidth(dc, L"Orient..."), 0};
  DrawText(dc, L"Orienteering", -1, &rc, DT_CALCRECT | DT_END_ELLIPSIS);
  CHECK(rc.right == textWidth(dc, L"Orient..."));

  ReleaseDC(nullptr, dc);
  DeleteObject(font);
}

void testTextPixels() {
  HFONT font = arial(16);
  Dib dib(200, 60);
  SelectObject(dib.dc, font);
  SIZE size = {};
  GetTextExtentPoint32(dib.dc, L"Hello", 5, &size);

  // Transparent text stays inside its cell.
  SetBkMode(dib.dc, TRANSPARENT);
  CHECK(TextOut(dib.dc, 20, 10, L"Hello", 5));
  QRect ink = dib.ink();
  CHECK(!ink.isEmpty() && QRect(20, 10, size.cx, size.cy).contains(ink));
  CHECK(ink.left() <= 22 && ink.right() >= 20 + size.cx - 4);

  // An opaque background fills the cell.
  dib.fill(white);
  SetBkMode(dib.dc, OPAQUE);
  SetBkColor(dib.dc, green);
  SetTextColor(dib.dc, blue);
  CHECK(TextOut(dib.dc, 20, 10, L"Hello", 5));
  CHECK(dib.ink() == QRect(20, 10, size.cx, size.cy) && dib.pixel(20, 10) == green);
  SetBkMode(dib.dc, TRANSPARENT);
  SetTextColor(dib.dc, black);

  // DrawText clips to the rectangle unless DT_NOCLIP is given.
  dib.fill(white);
  RECT rc = {10, 10, 30, 40};
  CHECK(DrawText(dib.dc, L"WWWWWW", -1, &rc, DT_LEFT | DT_NOPREFIX) == size.cy);
  CHECK(!dib.ink().isEmpty() && dib.ink().right() < 30);
  dib.fill(white);
  CHECK(DrawText(dib.dc, L"WWWWWW", -1, &rc, DT_LEFT | DT_NOCLIP | DT_NOPREFIX) == size.cy);
  CHECK(dib.ink().right() >= 40);

  // Alignment within the rectangle.
  dib.fill(white);
  rc = {0, 0, 200, 30};
  DrawText(dib.dc, L"Hello", -1, &rc, DT_RIGHT | DT_NOPREFIX);
  CHECK(dib.ink().right() >= 195 && dib.ink().left() >= 200 - size.cx);
  dib.fill(white);
  DrawText(dib.dc, L"Hello", -1, &rc, DT_CENTER | DT_NOPREFIX);
  CHECK(std::abs(dib.ink().center().x() - 100) <= 3);

  // The ellipsis shortens the text to the rectangle, also without clipping.
  dib.fill(white);
  rc = {0, 0, 60, 30};
  DrawText(dib.dc, L"Orienteering competition", -1, &rc, DT_END_ELLIPSIS | DT_NOCLIP);
  CHECK(!dib.ink().isEmpty() && dib.ink().right() < 61 && dib.ink().right() > 40);

  // Word-broken text goes down line by line.
  dib.fill(white);
  rc = {0, 0, textWidth(dib.dc, L"one two"), 60};
  CHECK(DrawText(dib.dc, L"one two three", -1, &rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX) == 2 * size.cy);
  CHECK(dib.ink().bottom() >= size.cy + 5 && dib.ink().bottom() < 2 * size.cy);

  // An escapement of 900 turns the text upwards from its origin.
  dib.fill(white);
  HFONT turned = arial(16, 900);
  SelectObject(dib.dc, turned);
  CHECK(TextOut(dib.dc, 30, 55, L"Hello", 5));
  ink = dib.ink();
  CHECK(!ink.isEmpty() && ink.height() > ink.width() && ink.left() >= 29 && ink.bottom() <= 55 &&
        ink.right() < 30 + size.cy);
  SelectObject(dib.dc, GetStockObject(SYSTEM_FONT));
  DeleteObject(turned);
  DeleteObject(font);
}

int CALLBACK countFonts(const LOGFONT *logFont, const TEXTMETRIC *metric, DWORD type, LPARAM data) {
  auto *counts = reinterpret_cast<std::pair<int, int> *>(data);
  counts->first++;
  if (logFont->lfFaceName[0] && metric->tmAveCharWidth > 0 && metric->tmHeight > 0 && type == TRUETYPE_FONTTYPE)
    counts->second++;
  return 1;
}

int CALLBACK stopAtFirst(const LOGFONT *, const TEXTMETRIC *, DWORD, LPARAM data) {
  (*reinterpret_cast<int *>(data))++;
  return 0;
}

void testFontEnumerationAndData() {
  HDC dc = GetDC(nullptr);
  LOGFONT logFont = {};
  logFont.lfCharSet = DEFAULT_CHARSET;
  std::pair<int, int> counts;
  CHECK(EnumFontFamiliesEx(dc, &logFont, countFonts, LPARAM(&counts), 0) == 1);
  CHECK(counts.first > 0 && counts.second > 0);
  int calls = 0;
  CHECK(EnumFontFamiliesEx(dc, &logFont, stopAtFirst, LPARAM(&calls), 0) == 0 && calls == 1);
  std::wcscpy(logFont.lfFaceName, L"No Such Font Family");
  counts = {};
  CHECK(EnumFontFamiliesEx(dc, &logFont, countFonts, LPARAM(&counts), 0) == 1 && counts.first == 0);

  // The whole font file (table 0) and single tables.
  HFONT font = arial(14);
  SelectObject(dc, font);
  const DWORD size = GetFontData(dc, 0, 0, nullptr, 0);
  CHECK(size != GDI_ERROR && size > 1000);
  if (size != GDI_ERROR && size > 12) {
    std::vector<unsigned char> file(size);
    CHECK(GetFontData(dc, 0, 0, file.data(), size) == size);
    const bool trueType = file[0] == 0 && file[1] == 1 && file[2] == 0 && file[3] == 0;
    const bool openType = file[0] == 'O' && file[1] == 'T' && file[2] == 'T' && file[3] == 'O';
    CHECK(trueType || openType);
    CHECK((file[4] << 8 | file[5]) > 5);
  }
  const DWORD headTag = 'h' | ('e' << 8) | ('a' << 16) | (DWORD('d') << 24);
  CHECK(GetFontData(dc, headTag, 0, nullptr, 0) == 54);
  unsigned char magic[4] = {};
  CHECK(GetFontData(dc, headTag, 12, magic, 4) == 4 && magic[0] == 0x5F && magic[1] == 0x0F && magic[2] == 0x3C);
  CHECK(GetFontData(dc, 'x' | ('x' << 8) | ('x' << 16) | (DWORD('x') << 24), 0, nullptr, 0) == GDI_ERROR);
  ReleaseDC(nullptr, dc);
  DeleteObject(font);
}

} // namespace

int main(int argc, char **argv) {
  const std::unique_ptr<QApplication> app = meos_qt::createApplication(argc, argv);

  testObjects();
  testRectangles();
  testLinesAndPolygons();
  testGradients();
  testBitmaps();
  testWindowPainting();
  testScrollWindow();
  testDrawingFromThread();
  testFontHeights();
  testDrawTextLayout();
  testTextPixels();
  testFontEnumerationAndData();

  meos_qt::destroyAllWindows();
  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures.load());
    return 1;
  }
  std::printf("gdi self test passed\n");
  return 0;
}
