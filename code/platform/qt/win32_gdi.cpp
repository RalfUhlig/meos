/************************************************************************
    MeOS - Orienteering Software
    Linux port: GDI device contexts, objects, lines, shapes, gradients and
    bitmaps on QImage.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_gdi.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPolygon>

#include <array>
#include <unordered_map>
#include <utility>

namespace {

using meos_qt::Bitmap;
using meos_qt::Brush;
using meos_qt::DeviceContext;
using meos_qt::GdiObject;
using meos_qt::GdiType;
using meos_qt::Pen;
using meos_qt::Surface;

std::mutex objectMutex;
std::unordered_map<HGDIOBJ, std::shared_ptr<GdiObject>> objects;
// Above the values of class background brushes (system colour index + 1).
std::uintptr_t lastObjectHandle = 0x40000;

constexpr int stockCount = 20;
std::mutex stockMutex;
std::array<HGDIOBJ, stockCount> stockHandles{};
HGDIOBJ defaultBitmapHandle = nullptr;

HGDIOBJ registerStock(std::shared_ptr<GdiObject> object) {
  object->stock = true;
  return meos_qt::registerGdiObject(std::move(object));
}

std::shared_ptr<GdiObject> createStockObject(int index) {
  switch (index) {
  case WHITE_BRUSH: return std::make_shared<Brush>(false, RGB(255, 255, 255));
  case LTGRAY_BRUSH: return std::make_shared<Brush>(false, RGB(192, 192, 192));
  case 2: return std::make_shared<Brush>(false, RGB(128, 128, 128)); // GRAY_BRUSH
  case 3: return std::make_shared<Brush>(false, RGB(64, 64, 64));    // DKGRAY_BRUSH
  case 4: return std::make_shared<Brush>(false, RGB(0, 0, 0));       // BLACK_BRUSH
  case NULL_BRUSH: return std::make_shared<Brush>(true, 0);
  case 6: return std::make_shared<Pen>(PS_SOLID, 1, RGB(255, 255, 255)); // WHITE_PEN
  case BLACK_PEN: return std::make_shared<Pen>(PS_SOLID, 1, RGB(0, 0, 0));
  case NULL_PEN: return std::make_shared<Pen>(PS_NULL, 1, 0);
  case DC_BRUSH: {
    auto brush = std::make_shared<Brush>(false, 0);
    brush->dcBrush = true;
    return brush;
  }
  case DC_PEN: {
    auto pen = std::make_shared<Pen>(PS_SOLID, 1, 0);
    pen->dcPen = true;
    return pen;
  }
  default:
    return meos_qt::createStockFont(index);
  }
}

HGDIOBJ stockObject(int index) {
  if (index < 0 || index >= stockCount)
    return nullptr;
  std::lock_guard<std::mutex> lock(stockMutex);
  if (!stockHandles[index]) {
    if (std::shared_ptr<GdiObject> object = createStockObject(index))
      stockHandles[index] = registerStock(std::move(object));
  }
  return stockHandles[index];
}

// The 1x1 bitmap selected into a new memory DC.
HGDIOBJ defaultBitmap() {
  std::lock_guard<std::mutex> lock(stockMutex);
  if (!defaultBitmapHandle) {
    QImage image(1, 1, QImage::Format_RGB32);
    image.fill(Qt::black);
    defaultBitmapHandle = registerStock(std::make_shared<Bitmap>(std::make_shared<Surface>(image, nullptr)));
  }
  return defaultBitmapHandle;
}

void selectDefaults(DeviceContext &dc) {
  dc.penHandle = stockObject(BLACK_PEN);
  dc.pen = meos_qt::findGdiObject<Pen>(dc.penHandle, GdiType::Pen);
  dc.brushHandle = stockObject(WHITE_BRUSH);
  dc.brush = meos_qt::findGdiObject<Brush>(dc.brushHandle, GdiType::Brush);
  dc.fontHandle = stockObject(SYSTEM_FONT);
  dc.font = meos_qt::findGdiObject<meos_qt::Font>(dc.fontHandle, GdiType::Font);
}

bool unregisterObject(HGDIOBJ handle) {
  std::lock_guard<std::mutex> lock(objectMutex);
  return objects.erase(handle) > 0;
}

// The pen colour and width of a DC, or width 0 for a null pen.
int penOf(const DeviceContext &dc, QColor &color) {
  if (!dc.pen || dc.pen->style == PS_NULL)
    return 0;
  color = meos_qt::toQColor(dc.pen->dcPen ? dc.dcPenColor : dc.pen->color);
  return std::max(dc.pen->width, 1);
}

bool brushOf(const DeviceContext &dc, QColor &color) {
  if (!dc.brush || dc.brush->null)
    return false;
  color = meos_qt::toQColor(dc.brush->dcBrush ? dc.dcBrushColor : dc.brush->color);
  return true;
}

// A line of a one pixel pen from (x0, y0) to (x1, y1) without the end point, as
// GDI draws it.
void drawThinLine(QPainter &painter, const QColor &color, int x0, int y0, int x1, int y1) {
  if (y0 == y1) {
    if (x0 != x1)
      painter.fillRect(x1 > x0 ? QRect(x0, y0, x1 - x0, 1) : QRect(x1 + 1, y0, x0 - x1, 1), color);
    return;
  }
  if (x0 == x1) {
    painter.fillRect(y1 > y0 ? QRect(x0, y0, 1, y1 - y0) : QRect(x0, y1 + 1, 1, y0 - y1), color);
    return;
  }
  // Bresenham.
  const int dx = std::abs(x1 - x0);
  const int dy = -std::abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  int x = x0;
  int y = y0;
  while (x != x1 || y != y1) {
    painter.fillRect(QRect(x, y, 1, 1), color);
    const int e2 = 2 * error;
    if (e2 >= dy) {
      error += dy;
      x += sx;
    }
    if (e2 <= dx) {
      error += dx;
      y += sy;
    }
  }
}

void drawLine(QPainter &painter, const QColor &color, int width, QPoint from, QPoint to) {
  if (width <= 1) {
    drawThinLine(painter, color, from.x(), from.y(), to.x(), to.y());
    return;
  }
  // GDI draws wide solid pens with round caps and joins.
  painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawLine(QPointF(from) + QPointF(0.5, 0.5), QPointF(to) + QPointF(0.5, 0.5));
}

QRect lineBounds(QPoint a, QPoint b, int width) {
  const int margin = width / 2 + 1;
  return QRect(a, b).normalized().adjusted(-margin, -margin, margin, margin);
}

// fillRect would normalize a rectangle of negative size instead of skipping it.
void fillArea(QPainter &painter, int x, int y, int width, int height, const QColor &color) {
  if (width > 0 && height > 0)
    painter.fillRect(QRect(x, y, width, height), color);
}

int regionType(const QRegion &region) {
  if (region.isEmpty())
    return NULLREGION;
  return region.rectCount() == 1 ? SIMPLEREGION : COMPLEXREGION;
}

QRect pixelRect(const QRect &logical, qreal ratio) {
  if (ratio == 1.0)
    return logical;
  return QRect(qRound(logical.x() * ratio), qRound(logical.y() * ratio), qRound(logical.width() * ratio),
               qRound(logical.height() * ratio));
}

} // namespace

/* ---------------------------------------------------------------------
   Internal interface
   --------------------------------------------------------------------- */

QRect meos_qt::Surface::rect() const {
  if (image.isNull())
    return QRect();
  const qreal ratio = image.devicePixelRatio();
  return QRect(0, 0, qRound(image.width() / ratio), qRound(image.height() / ratio));
}

std::shared_ptr<Surface> meos_qt::createWindowSurface(HWND window) {
  return std::make_shared<Surface>(QImage(), window);
}

void meos_qt::resizeSurface(Surface &surface, QSize size, qreal devicePixelRatio) {
  std::lock_guard<std::mutex> lock(surface.mutex);
  const QSize pixels = size * devicePixelRatio;
  if (surface.image.size() == pixels && surface.image.devicePixelRatio() == devicePixelRatio)
    return;
  if (size.isEmpty()) {
    surface.image = QImage();
    return;
  }
  QImage image(pixels, QImage::Format_RGB32);
  image.setDevicePixelRatio(devicePixelRatio);
  image.fill(Qt::white);
  if (!surface.image.isNull()) {
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(QPointF(0, 0), surface.image);
  }
  surface.image = std::move(image);
}

void meos_qt::copySurfaceToScreen(Surface &surface, QPainter &painter, const QRegion &region) {
  std::lock_guard<std::mutex> lock(surface.mutex);
  surface.dirty -= region;
  if (surface.image.isNull())
    return;
  const qreal ratio = surface.image.devicePixelRatio();
  for (const QRect &rect : region)
    painter.drawImage(QRectF(rect), surface.image, QRectF(pixelRect(rect, ratio)));
}

void meos_qt::scrollSurface(Surface &surface, const QRect &dest, int dx, int dy) {
  std::lock_guard<std::mutex> lock(surface.mutex);
  const QRect area = dest & surface.rect();
  if (surface.image.isNull() || area.isEmpty())
    return;
  const qreal ratio = surface.image.devicePixelRatio();
  const QImage moved = surface.image.copy(pixelRect(area.translated(-dx, -dy), ratio));
  {
    QPainter painter(&surface.image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(QRectF(area), moved);
  }
  markDirty(surface, area);
}

void meos_qt::markDirty(Surface &surface, const QRect &area) {
  if (!surface.window)
    return;
  const QRect visible = area & surface.rect();
  if (visible.isEmpty())
    return;
  surface.dirty += visible;
  if (surface.flushPending || !QCoreApplication::instance())
    return;
  surface.flushPending = true;

  // The screen copy happens in the GUI thread, also for drawing from other threads.
  std::weak_ptr<Surface> weak = surface.weak_from_this();
  QMetaObject::invokeMethod(
      QCoreApplication::instance(),
      [weak] {
        const std::shared_ptr<Surface> target = weak.lock();
        if (!target)
          return;
        QRegion region;
        {
          std::lock_guard<std::mutex> lock(target->mutex);
          region = target->dirty;
          target->dirty = QRegion();
          target->flushPending = false;
        }
        const std::shared_ptr<Window> window = findWindow(target->window);
        if (window && window->surface == target && window->client && !region.isEmpty())
          window->client->update(region);
      },
      Qt::QueuedConnection);
}

HGDIOBJ meos_qt::registerGdiObject(std::shared_ptr<GdiObject> object) {
  std::lock_guard<std::mutex> lock(objectMutex);
  lastObjectHandle += 4;
  const HGDIOBJ handle = reinterpret_cast<HGDIOBJ>(lastObjectHandle);
  objects[handle] = std::move(object);
  return handle;
}

std::shared_ptr<GdiObject> meos_qt::findGdiObject(HGDIOBJ handle) {
  if (!handle)
    return nullptr;
  std::lock_guard<std::mutex> lock(objectMutex);
  const auto entry = objects.find(handle);
  return entry == objects.end() ? nullptr : entry->second;
}

HDC meos_qt::createDc(DeviceContext::Kind kind, HWND window, std::shared_ptr<Surface> surface, const QRegion *clip) {
  auto dc = std::make_shared<DeviceContext>(kind, window);
  dc->surface = std::move(surface);
  selectDefaults(*dc);
  if (clip) {
    dc->clipped = true;
    dc->clip = *clip;
  }
  return static_cast<HDC>(registerGdiObject(std::move(dc)));
}

bool meos_qt::releaseDc(HDC dc, DeviceContext::Kind kind) {
  const std::shared_ptr<DeviceContext> context = findDc(dc);
  if (!context || context->kind != kind)
    return false;
  return unregisterObject(static_cast<HGDIOBJ>(dc));
}

void meos_qt::fillBackground(HDC dc, HBRUSH brush, const QRect &area) {
  const std::shared_ptr<DeviceContext> context = findDc(dc);
  if (!context || !brush)
    return;
  QColor color;
  const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(brush);
  if (value <= 31) {
    color = toQColor(GetSysColor(int(value) - 1));
  }
  else {
    const std::shared_ptr<Brush> object = findGdiObject<Brush>(static_cast<HGDIOBJ>(brush), GdiType::Brush);
    if (!object || object->null)
      return;
    color = toQColor(object->dcBrush ? context->dcBrushColor : object->color);
  }
  paint(*context, area, [&](QPainter &painter) { painter.fillRect(area, color); });
}

/* ---------------------------------------------------------------------
   Device contexts and objects
   --------------------------------------------------------------------- */

HDC CreateCompatibleDC(HDC dc) {
  if (dc && !meos_qt::findDc(dc))
    return nullptr;
  auto context = std::make_shared<DeviceContext>(DeviceContext::Kind::Memory, nullptr);
  selectDefaults(*context);
  context->bitmapHandle = defaultBitmap();
  context->bitmap = meos_qt::findGdiObject<Bitmap>(context->bitmapHandle, GdiType::Bitmap);
  context->surface = context->bitmap->surface;
  return static_cast<HDC>(meos_qt::registerGdiObject(std::move(context)));
}

BOOL DeleteDC(HDC dc) {
  return meos_qt::releaseDc(dc, DeviceContext::Kind::Memory) ? TRUE : FALSE;
}

HGDIOBJ GetStockObject(int index) {
  return stockObject(index);
}

HGDIOBJ SelectObject(HDC dc, HGDIOBJ object) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  const std::shared_ptr<GdiObject> selected = meos_qt::findGdiObject(object);
  if (!context || !selected)
    return nullptr;

  HGDIOBJ previous = nullptr;
  switch (selected->type) {
  case GdiType::Pen:
    previous = context->penHandle;
    context->penHandle = object;
    context->pen = std::static_pointer_cast<Pen>(selected);
    break;
  case GdiType::Brush:
    previous = context->brushHandle;
    context->brushHandle = object;
    context->brush = std::static_pointer_cast<Brush>(selected);
    break;
  case GdiType::Font:
    previous = context->fontHandle;
    context->fontHandle = object;
    context->font = std::static_pointer_cast<meos_qt::Font>(selected);
    break;
  case GdiType::Bitmap:
    // Only memory DCs draw on bitmaps.
    if (context->kind != DeviceContext::Kind::Memory)
      return nullptr;
    previous = context->bitmapHandle;
    context->bitmapHandle = object;
    context->bitmap = std::static_pointer_cast<Bitmap>(selected);
    context->surface = context->bitmap->surface;
    break;
  case GdiType::DeviceContext:
    return nullptr;
  }
  return previous;
}

BOOL DeleteObject(HGDIOBJ object) {
  const std::shared_ptr<GdiObject> target = meos_qt::findGdiObject(object);
  if (!target || target->type == GdiType::DeviceContext)
    return FALSE;
  if (target->stock)
    return TRUE;
  // DCs that have the object selected keep it until it is replaced.
  return unregisterObject(object) ? TRUE : FALSE;
}

HPEN CreatePen(int style, int width, COLORREF color) {
  return static_cast<HPEN>(meos_qt::registerGdiObject(std::make_shared<Pen>(style, width, color)));
}

HBRUSH CreateSolidBrush(COLORREF color) {
  return static_cast<HBRUSH>(meos_qt::registerGdiObject(std::make_shared<Brush>(false, color)));
}

COLORREF SetDCPenColor(HDC dc, COLORREF color) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context)
    return CLR_INVALID;
  return std::exchange(context->dcPenColor, color);
}

COLORREF GetDCPenColor(HDC dc) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  return context ? context->dcPenColor : CLR_INVALID;
}

COLORREF SetDCBrushColor(HDC dc, COLORREF color) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context)
    return CLR_INVALID;
  return std::exchange(context->dcBrushColor, color);
}

COLORREF SetTextColor(HDC dc, COLORREF color) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context)
    return CLR_INVALID;
  return std::exchange(context->textColor, color);
}

COLORREF SetBkColor(HDC dc, COLORREF color) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context)
    return CLR_INVALID;
  return std::exchange(context->bkColor, color);
}

int SetBkMode(HDC dc, int mode) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context || (mode != TRANSPARENT && mode != OPAQUE))
    return 0;
  return std::exchange(context->bkMode, mode);
}

int IntersectClipRect(HDC dc, int left, int top, int right, int bottom) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context)
    return RGN_ERROR;
  const QRect rect = QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
  if (context->clipped) {
    context->clip &= rect;
  }
  else {
    // Without a clip region, the DC covers its surface.
    const QRect bounds = context->surface ? context->surface->rect() : QRect();
    context->clip = QRegion(rect.normalized() & bounds);
    context->clipped = true;
  }
  return regionType(context->clip);
}

/* ---------------------------------------------------------------------
   Lines and shapes
   --------------------------------------------------------------------- */

BOOL MoveToEx(HDC dc, int x, int y, LPPOINT previous) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context)
    return FALSE;
  if (previous)
    *previous = POINT{context->position.x(), context->position.y()};
  context->position = QPoint(x, y);
  return TRUE;
}

BOOL LineTo(HDC dc, int x, int y) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context)
    return FALSE;
  const QPoint from = context->position;
  const QPoint to(x, y);
  context->position = to;

  QColor color;
  const int width = penOf(*context, color);
  if (width > 0) {
    meos_qt::paint(*context, lineBounds(from, to, width),
                   [&](QPainter &painter) { drawLine(painter, color, width, from, to); });
  }
  return TRUE;
}

// GDI excludes the right and bottom edge. The pen draws on the outermost pixels;
// without a pen the filled area is one pixel smaller still.
BOOL Rectangle(HDC dc, int left, int top, int right, int bottom) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context)
    return FALSE;
  if (left > right)
    std::swap(left, right);
  if (top > bottom)
    std::swap(top, bottom);

  QColor penColor;
  QColor brushColor;
  const int penWidth = penOf(*context, penColor);
  const bool fill = brushOf(*context, brushColor);
  const QRect bounds(left, top, right - left, bottom - top);

  meos_qt::paint(*context, bounds.adjusted(-penWidth, -penWidth, penWidth, penWidth), [&](QPainter &painter) {
    if (penWidth == 0) {
      if (fill)
        fillArea(painter, left, top, right - left - 1, bottom - top - 1, brushColor);
      return;
    }
    if (fill)
      fillArea(painter, left + 1, top + 1, right - left - 2, bottom - top - 2, brushColor);
    if (penWidth == 1) {
      fillArea(painter, left, top, right - left, 1, penColor);
      fillArea(painter, left, bottom - 1, right - left, 1, penColor);
      fillArea(painter, left, top, 1, bottom - top, penColor);
      fillArea(painter, right - 1, top, 1, bottom - top, penColor);
    }
    else {
      painter.setPen(QPen(penColor, penWidth, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(QRectF(left + 0.5, top + 0.5, right - left - 1, bottom - top - 1));
    }
  });
  return TRUE;
}

BOOL Ellipse(HDC dc, int left, int top, int right, int bottom) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context)
    return FALSE;
  QColor penColor;
  QColor brushColor;
  const int penWidth = penOf(*context, penColor);
  const bool fill = brushOf(*context, brushColor);
  const QRect bounds = QRect(QPoint(left, top), QPoint(right - 1, bottom - 1)).normalized();

  meos_qt::paint(*context, bounds.adjusted(-penWidth, -penWidth, penWidth, penWidth), [&](QPainter &painter) {
    painter.setPen(penWidth == 0 ? QPen(Qt::NoPen) : QPen(penColor, penWidth == 1 ? 0 : penWidth));
    painter.setBrush(fill ? QBrush(brushColor) : QBrush(Qt::NoBrush));
    // The outline runs through the centres of the outermost pixels.
    painter.drawEllipse(QRectF(bounds).adjusted(0.5, 0.5, -0.5, -0.5));
  });
  return TRUE;
}

BOOL Polygon(HDC dc, const POINT *points, int count) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context || !points || count < 2)
    return FALSE;
  QPolygon polygon;
  for (int i = 0; i < count; i++)
    polygon << QPoint(points[i].x, points[i].y);

  QColor penColor;
  QColor brushColor;
  const int penWidth = penOf(*context, penColor);
  const bool fill = brushOf(*context, brushColor);
  const int margin = penWidth / 2 + 1;

  meos_qt::paint(*context, polygon.boundingRect().adjusted(-margin, -margin, margin, margin), [&](QPainter &painter) {
    if (fill) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(brushColor);
      painter.drawPolygon(polygon, Qt::OddEvenFill);
    }
    if (penWidth > 0) {
      for (int i = 0; i < count; i++)
        drawLine(painter, penColor, penWidth, polygon[i], polygon[(i + 1) % count]);
    }
  });
  return TRUE;
}

// Each column (GRADIENT_FILL_RECT_H) or row (_V) gets the colour interpolated
// between the two vertices; the first column has the colour of the first vertex.
BOOL GradientFill(HDC dc, PTRIVERTEX vertices, ULONG vertexCount, PVOID mesh, ULONG meshCount, ULONG mode) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context || !vertices || !mesh || (mode != GRADIENT_FILL_RECT_H && mode != GRADIENT_FILL_RECT_V))
    return FALSE;
  const auto *rects = static_cast<const GRADIENT_RECT *>(mesh);
  for (ULONG i = 0; i < meshCount; i++) {
    if (rects[i].UpperLeft >= vertexCount || rects[i].LowerRight >= vertexCount)
      return FALSE;
  }

  for (ULONG i = 0; i < meshCount; i++) {
    TRIVERTEX first = vertices[rects[i].UpperLeft];
    TRIVERTEX second = vertices[rects[i].LowerRight];
    const bool horizontal = mode == GRADIENT_FILL_RECT_H;
    // The colours stay with their vertices when the rectangle is given reversed.
    if ((horizontal && first.x > second.x) || (!horizontal && first.y > second.y))
      std::swap(first, second);
    const int left = std::min(first.x, second.x);
    const int top = std::min(first.y, second.y);
    const QRect bounds(left, top, std::max(first.x, second.x) - left, std::max(first.y, second.y) - top);
    if (bounds.isEmpty())
      continue;

    const int steps = horizontal ? bounds.width() : bounds.height();
    meos_qt::paint(*context, bounds, [&](QPainter &painter) {
      for (int step = 0; step < steps; step++) {
        auto channel = [&](COLOR16 a, COLOR16 b) {
          return int((std::int64_t(a) * (steps - step) + std::int64_t(b) * step) / steps) >> 8;
        };
        const QColor color(channel(first.Red, second.Red), channel(first.Green, second.Green),
                           channel(first.Blue, second.Blue));
        if (horizontal)
          painter.fillRect(QRect(bounds.left() + step, bounds.top(), 1, bounds.height()), color);
        else
          painter.fillRect(QRect(bounds.left(), bounds.top() + step, bounds.width(), 1), color);
      }
    });
  }
  return TRUE;
}

/* ---------------------------------------------------------------------
   Bitmaps
   --------------------------------------------------------------------- */

HBITMAP CreateCompatibleBitmap(HDC dc, int width, int height) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context || width < 0 || height < 0)
    return nullptr;
  qreal ratio = 1.0;
  if (context->surface) {
    std::lock_guard<std::mutex> lock(context->surface->mutex);
    if (!context->surface->image.isNull())
      ratio = context->surface->image.devicePixelRatio();
  }
  // Windows returns a 1x1 bitmap for an empty size. Bitmaps are always in colour
  // here, also those compatible with a fresh memory DC.
  width = std::max(width, 1);
  height = std::max(height, 1);
  QImage image(QSize(width, height) * ratio, QImage::Format_RGB32);
  if (image.isNull())
    return nullptr;
  image.setDevicePixelRatio(ratio);
  image.fill(Qt::black);
  return static_cast<HBITMAP>(
      meos_qt::registerGdiObject(std::make_shared<Bitmap>(std::make_shared<Surface>(std::move(image), nullptr))));
}

// Top-down 32-bit DIBs (negative biHeight), the only kind MeOS creates. The pixel
// memory is that of the image: BGRA bytes, premultiplied alpha for AlphaBlend.
HBITMAP CreateDIBSection(HDC /*dc*/, const BITMAPINFO *info, UINT usage, void **bits, HANDLE section,
                         DWORD /*offset*/) {
  if (bits)
    *bits = nullptr;
  if (!info || usage != DIB_RGB_COLORS || section) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  const BITMAPINFOHEADER &header = info->bmiHeader;
  if (header.biBitCount != 32 || header.biCompression != BI_RGB || header.biWidth <= 0 || header.biHeight >= 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  QImage image(header.biWidth, -header.biHeight, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  image.fill(Qt::transparent);
  auto surface = std::make_shared<Surface>(std::move(image), nullptr);
  if (bits)
    *bits = surface->image.bits();
  return static_cast<HBITMAP>(meos_qt::registerGdiObject(std::make_shared<Bitmap>(std::move(surface))));
}

BOOL BitBlt(HDC dst, int x, int y, int width, int height, HDC src, int srcX, int srcY, DWORD rop) {
  const std::shared_ptr<DeviceContext> target = meos_qt::findDc(dst);
  const std::shared_ptr<DeviceContext> source = meos_qt::findDc(src);
  if (!target || !source || rop != SRCCOPY)
    return FALSE;
  if (width <= 0 || height <= 0 || !target->surface || !source->surface)
    return TRUE;

  // A copy of the source pixels; source and target may be the same surface.
  QImage pixels;
  QRect destRect;
  {
    Surface &from = *source->surface;
    std::lock_guard<std::mutex> lock(from.mutex);
    const QRect area = QRect(srcX, srcY, width, height) & from.rect();
    if (area.isEmpty())
      return TRUE;
    pixels = from.image.copy(pixelRect(area, from.image.devicePixelRatio()));
    destRect = area.translated(x - srcX, y - srcY);
  }
  meos_qt::paint(*target, destRect, [&](QPainter &painter) {
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(QRectF(destRect), pixels);
  });
  return TRUE;
}

BOOL AlphaBlend(HDC dst, int x, int y, int width, int height, HDC src, int srcX, int srcY, int srcWidth,
                int srcHeight, BLENDFUNCTION blend) {
  const std::shared_ptr<DeviceContext> target = meos_qt::findDc(dst);
  const std::shared_ptr<DeviceContext> source = meos_qt::findDc(src);
  if (!target || !source || !source->surface || blend.BlendOp != AC_SRC_OVER)
    return FALSE;
  if (width <= 0 || height <= 0 || srcWidth <= 0 || srcHeight <= 0)
    return FALSE;

  QImage pixels;
  {
    Surface &from = *source->surface;
    std::lock_guard<std::mutex> lock(from.mutex);
    const QRect area(srcX, srcY, srcWidth, srcHeight);
    // Windows fails if the source rectangle is not inside the bitmap.
    if (srcX < 0 || srcY < 0 || !from.rect().contains(area))
      return FALSE;
    pixels = from.image.copy(pixelRect(area, from.image.devicePixelRatio()));
  }
  if (!(blend.AlphaFormat & AC_SRC_ALPHA))
    pixels = pixels.convertToFormat(QImage::Format_RGB32);

  const QRect destRect(x, y, width, height);
  meos_qt::paint(*target, destRect, [&](QPainter &painter) {
    painter.setOpacity(blend.SourceConstantAlpha / 255.0);
    painter.drawImage(QRectF(destRect), pixels);
  });
  return TRUE;
}
