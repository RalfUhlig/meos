/************************************************************************
    MeOS - Orienteering Software
    Linux port: internal interface of the Qt backend for GDI.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Device contexts, GDI objects and the pixel surfaces they draw on. Shared by
// win32_gdi.cpp, win32_text.cpp, win32_canvas.cpp and the self tests.
//
// Unlike the window functions, GDI may be used from any thread: MeOS draws text
// from a helper thread (TextFader). A device context belongs to one thread at a
// time; surfaces and the object table are guarded by mutexes. Every drawing call
// opens its own QPainter on the surface while holding the surface mutex, so no
// painter outlives a call or crosses threads.

#pragma once

// Qt first: the Win32 headers define many short macros.
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QRegion>

#include "win32_ui.h"

#include <mutex>

namespace meos_qt {

/* ---------------------------------------------------------------------
   Surfaces: win32_gdi.cpp
   --------------------------------------------------------------------- */

// The pixels of a bitmap, a DIB section or the backing store of a window. The
// image may have a device pixel ratio; GDI coordinates are logical pixels.
class Surface : public std::enable_shared_from_this<Surface> {
public:
  Surface(QImage image, HWND window) : image(std::move(image)), window(window) {}

  // Guards all members below.
  std::mutex mutex;
  QImage image;
  // Drawn since the last copy to the screen (backing stores only), logical coordinates.
  QRegion dirty;
  bool flushPending = false;

  // The window whose backing store this is, or nullptr for bitmaps.
  const HWND window;

  // The image rectangle in logical coordinates.
  QRect rect() const;
};

std::shared_ptr<Surface> createWindowSurface(HWND window);

// Gives a backing store a new size, keeping the content in the top left corner.
void resizeSurface(Surface &surface, QSize size, qreal devicePixelRatio);

// Copies a region of a backing store to the widget painter of the client area.
void copySurfaceToScreen(Surface &surface, QPainter &painter, const QRegion &region);

// Moves the pixels that end up in dest (logical coordinates) by (dx, dy).
void scrollSurface(Surface &surface, const QRect &dest, int dx, int dy);

/* ---------------------------------------------------------------------
   GDI objects and device contexts: win32_gdi.cpp
   --------------------------------------------------------------------- */

enum class GdiType { Pen, Brush, Font, Bitmap, DeviceContext };

class GdiObject {
public:
  explicit GdiObject(GdiType type) : type(type) {}
  virtual ~GdiObject() = default;
  GdiObject(const GdiObject &) = delete;
  GdiObject &operator=(const GdiObject &) = delete;

  const GdiType type;
  // Stock objects survive DeleteObject.
  bool stock = false;
};

class Pen : public GdiObject {
public:
  Pen(int style, int width, COLORREF color) : GdiObject(GdiType::Pen), style(style), width(width), color(color) {}
  const int style;
  const int width;
  const COLORREF color;
  // DC_PEN: draws with the pen colour of the device context.
  bool dcPen = false;
};

class Brush : public GdiObject {
public:
  Brush(bool null, COLORREF color) : GdiObject(GdiType::Brush), null(null), color(color) {}
  const bool null;
  const COLORREF color;
  // DC_BRUSH: fills with the brush colour of the device context.
  bool dcBrush = false;
};

// Text metrics in pixels, computed like GDI from the font tables.
struct FontMetrics {
  int em = 0;
  int ascent = 0;
  int descent = 0;
  int height = 0;
  int internalLeading = 0;
  int externalLeading = 0;
  int avgCharWidth = 0;
  int maxCharWidth = 0;
};

class Font : public GdiObject {
public:
  Font() : GdiObject(GdiType::Font) {}
  LOGFONT logFont = {};
  QFont font;
  FontMetrics metrics;
  bool fixedPitch = false;
};

class Bitmap : public GdiObject {
public:
  explicit Bitmap(std::shared_ptr<Surface> surface) : GdiObject(GdiType::Bitmap), surface(std::move(surface)) {}
  const std::shared_ptr<Surface> surface;
};

class DeviceContext : public GdiObject {
public:
  enum class Kind { Memory, Window, Paint, Screen };

  DeviceContext(Kind kind, HWND window) : GdiObject(GdiType::DeviceContext), kind(kind), window(window) {}

  const Kind kind;
  const HWND window;
  // Where drawing goes; nullptr for the screen DC, which only measures.
  std::shared_ptr<Surface> surface;

  // Selected objects. The DC keeps them alive, so an object deleted while
  // selected stays usable until it is replaced, as with GDI.
  HGDIOBJ penHandle = nullptr;
  HGDIOBJ brushHandle = nullptr;
  HGDIOBJ fontHandle = nullptr;
  HGDIOBJ bitmapHandle = nullptr;
  std::shared_ptr<Pen> pen;
  std::shared_ptr<Brush> brush;
  std::shared_ptr<Font> font;
  std::shared_ptr<Bitmap> bitmap;

  COLORREF dcPenColor = RGB(0, 0, 0);
  COLORREF dcBrushColor = RGB(255, 255, 255);
  COLORREF textColor = RGB(0, 0, 0);
  COLORREF bkColor = RGB(255, 255, 255);
  int bkMode = OPAQUE;
  QPoint position;

  // Clip region in DC coordinates, if any.
  bool clipped = false;
  QRegion clip;
};

// Handles are numbers, never addresses, as for windows.
HGDIOBJ registerGdiObject(std::shared_ptr<GdiObject> object);
std::shared_ptr<GdiObject> findGdiObject(HGDIOBJ handle);

template <typename T>
std::shared_ptr<T> findGdiObject(HGDIOBJ handle, GdiType type) {
  std::shared_ptr<GdiObject> object = findGdiObject(handle);
  return object && object->type == type ? std::static_pointer_cast<T>(object) : nullptr;
}

// A bitmap handle for an image (LoadBitmap).
HBITMAP createBitmap(QImage image);

inline std::shared_ptr<DeviceContext> findDc(HDC dc) {
  return findGdiObject<DeviceContext>(static_cast<HGDIOBJ>(dc), GdiType::DeviceContext);
}

// Creates a window, paint or screen DC with the default objects selected. clip
// limits drawing (the update region of BeginPaint).
HDC createDc(DeviceContext::Kind kind, HWND window, std::shared_ptr<Surface> surface, const QRegion *clip);

// Releases a DC created by createDc if it has the given kind.
bool releaseDc(HDC dc, DeviceContext::Kind kind);

// Fills an area with a class background brush (a brush handle or a system colour
// index + 1), clipped as the DC is.
void fillBackground(HDC dc, HBRUSH brush, const QRect &area);

inline QColor toQColor(COLORREF color) {
  return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
}

inline QRect toQRect(const RECT &rect) {
  return QRect(QPoint(rect.left, rect.top), QPoint(rect.right - 1, rect.bottom - 1));
}

inline RECT toRECT(const QRect &rect) {
  return RECT{rect.left(), rect.top(), rect.left() + rect.width(), rect.top() + rect.height()};
}

// Notes an area of a surface for the next copy to the screen. The surface mutex
// must be held.
void markDirty(Surface &surface, const QRect &area);

// Draws on the surface of a DC: opens a painter clipped to the DC's clip region,
// runs draw and notes area (DC coordinates) for the screen. Does nothing for a DC
// without a surface.
template <typename Draw>
void paint(DeviceContext &dc, const QRect &area, Draw &&draw) {
  if (!dc.surface)
    return;
  Surface &surface = *dc.surface;
  std::lock_guard<std::mutex> lock(surface.mutex);
  if (surface.image.isNull())
    return;
  {
    QPainter painter(&surface.image);
    if (dc.clipped)
      painter.setClipRegion(dc.clip);
    draw(painter);
  }
  markDirty(surface, dc.clipped ? (dc.clip & area).boundingRect() : area);
}

/* ---------------------------------------------------------------------
   Fonts: win32_text.cpp
   --------------------------------------------------------------------- */

std::shared_ptr<Font> createFont(const LOGFONT &logFont);

// The font of GetStockObject(index) (SYSTEM_FONT, DEFAULT_GUI_FONT, ...), or
// nullptr for other indices.
std::shared_ptr<Font> createStockFont(int index);

// The Qt font family that a GDI face name is drawn with.
QString substituteFamily(const std::wstring &faceName, BYTE pitchAndFamily);

} // namespace meos_qt
