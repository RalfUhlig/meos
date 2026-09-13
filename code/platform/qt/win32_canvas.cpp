/************************************************************************
    MeOS - Orienteering Software
    Linux port: widgets of windows with registered classes; input becomes
    window messages, the update region drives WM_PAINT, and a backing store
    keeps what GDI drew.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_gdi.h"

#include <QCloseEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QWheelEvent>

namespace {

using meos_qt::Window;

qreal backingStoreRatio(meos_qt::Surface &surface) {
  std::lock_guard<std::mutex> lock(surface.mutex);
  return surface.image.isNull() ? 0.0 : surface.image.devicePixelRatio();
}

std::shared_ptr<Window> windowOf(const QWidget *widget) {
  return meos_qt::findWindow(meos_qt::windowFromWidget(widget));
}

LPARAM pointParam(QPoint point) {
  // Signed 16-bit coordinates, read back with GET_X_LPARAM/GET_Y_LPARAM.
  return MAKELPARAM(point.x(), point.y());
}

void sendMouse(QWidget *source, QMouseEvent *event, UINT message) {
  event->accept();
  std::shared_ptr<Window> target = windowOf(source);
  QPoint position = event->position().toPoint();

  // While a window has the capture, it receives all mouse input.
  if (const std::shared_ptr<Window> capture = meos_qt::findWindow(GetCapture())) {
    if (capture != target && capture->client) {
      position = capture->client->mapFromGlobal(source->mapToGlobal(position));
      target = capture;
    }
  }
  if (target)
    meos_qt::callWindowProc(target, message, meos_qt::mouseKeyFlags(event->buttons(), event->modifiers()),
                            pointParam(position));
}

UINT buttonMessage(Qt::MouseButton button, bool down) {
  switch (button) {
  case Qt::LeftButton: return down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
  case Qt::RightButton: return down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
  case Qt::MiddleButton: return down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
  default: return 0;
  }
}

void sendWheel(QWidget *source, QWheelEvent *event) {
  const int delta = event->angleDelta().y();
  const std::shared_ptr<Window> target = windowOf(source);
  if (!delta || !target) {
    event->ignore();
    return;
  }
  event->accept();
  // WM_MOUSEWHEEL carries screen coordinates.
  const QPoint global = source->mapToGlobal(event->position().toPoint());
  meos_qt::callWindowProc(target, WM_MOUSEWHEEL,
                          MAKEWPARAM(meos_qt::mouseKeyFlags(event->buttons(), event->modifiers()), delta),
                          pointParam(global));
}

void sendKey(QWidget *source, QKeyEvent *event) {
  // Accepted here, so that the key does not reach the parent window as well.
  event->accept();
  const bool press = event->type() == QEvent::KeyPress;
  if (!press && event->isAutoRepeat())
    return;
  std::shared_ptr<Window> target = windowOf(source);
  if (!target)
    return;

  const int key = meos_qt::virtualKey(*event);
  const LPARAM param = meos_qt::keyMessageParam(*event);
  if (key)
    meos_qt::callWindowProc(target, press ? WM_KEYDOWN : WM_KEYUP, WPARAM(key), param);
  if (!press || key == VK_DELETE)
    return; // Windows produces no WM_CHAR for the delete key.

  // wchar_t holds whole code points on Linux.
  for (char32_t ch : event->text().toStdU32String()) {
    target = windowOf(source);
    if (!target)
      return;
    meos_qt::callWindowProc(target, WM_CHAR, WPARAM(ch), param);
  }
}

class CanvasClient : public QWidget {
public:
  explicit CanvasClient(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    // Windows gives the focus to a window only through SetFocus.
    setFocusPolicy(Qt::NoFocus);
  }

protected:
  // The window procedure paints the update region into the backing store, which
  // is then copied to the screen. Areas Qt repaints for other reasons come from
  // the backing store alone.
  void paintEvent(QPaintEvent *event) override {
    const std::shared_ptr<Window> window = windowOf(this);
    if (!window || !window->surface)
      return;
    const std::shared_ptr<meos_qt::Surface> surface = window->surface;
    if (devicePixelRatioF() != backingStoreRatio(*surface)) {
      meos_qt::resizeSurface(*surface, size(), devicePixelRatioF());
      meos_qt::invalidate(*window, rect(), true);
    }
    meos_qt::dispatchPaint(window);
    QPainter painter(this);
    meos_qt::copySurfaceToScreen(*surface, painter, event->region());
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    // WM_SETCURSOR precedes WM_MOUSEMOVE; its default processing sets the class cursor.
    if (const std::shared_ptr<Window> window = windowOf(this))
      meos_qt::setClassCursor(*window, this);
    sendMouse(this, event, WM_MOUSEMOVE);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (const UINT message = buttonMessage(event->button(), true))
      sendMouse(this, event, message);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (const UINT message = buttonMessage(event->button(), false))
      sendMouse(this, event, message);
  }

  // Without CS_DBLCLKS, Windows reports the second click as a plain button press.
  void mouseDoubleClickEvent(QMouseEvent *event) override {
    const std::shared_ptr<Window> window = windowOf(this);
    const bool doubleClicks = window && window->windowClass && (window->windowClass->style & CS_DBLCLKS);
    if (event->button() == Qt::LeftButton && doubleClicks)
      sendMouse(this, event, WM_LBUTTONDBLCLK);
    else if (const UINT message = buttonMessage(event->button(), true))
      sendMouse(this, event, message);
  }

  void wheelEvent(QWheelEvent *event) override { sendWheel(this, event); }
  void keyPressEvent(QKeyEvent *event) override { sendKey(this, event); }
  void keyReleaseEvent(QKeyEvent *event) override { sendKey(this, event); }

  // Windows moves the focus with the Tab key only in dialogs.
  bool focusNextPrevChild(bool) override { return false; }

  void leaveEvent(QEvent *event) override {
    QWidget::leaveEvent(event);
    const std::shared_ptr<Window> window = windowOf(this);
    if (!window || GetCapture() != window->handle)
      return;
    const QPoint position = mapFromGlobal(QCursor::pos());
    meos_qt::callWindowProc(
        window, WM_MOUSEMOVE,
        meos_qt::mouseKeyFlags(QGuiApplication::mouseButtons(), QGuiApplication::keyboardModifiers()),
        pointParam(position));
  }
};

class CanvasFrame : public QWidget {
public:
  explicit CanvasFrame(QWidget *parent) : QWidget(parent) {}

protected:
  // The border of a child window (the non-client area), drawn as the classic
  // sunken edge. Appearance with visual styles follows in step 1.2.7.
  void paintEvent(QPaintEvent *) override {
    const std::shared_ptr<Window> window = windowOf(this);
    if (!window || !window->isChild())
      return;
    QRect r = rect();
    QPainter painter(this);
    const auto edge = [&](int topLeftColor, int bottomRightColor) {
      painter.fillRect(r.left(), r.top(), r.width(), 1, meos_qt::toQColor(GetSysColor(topLeftColor)));
      painter.fillRect(r.left(), r.top(), 1, r.height(), meos_qt::toQColor(GetSysColor(topLeftColor)));
      painter.fillRect(r.left(), r.bottom(), r.width(), 1, meos_qt::toQColor(GetSysColor(bottomRightColor)));
      painter.fillRect(r.right(), r.top(), 1, r.height(), meos_qt::toQColor(GetSysColor(bottomRightColor)));
      r.adjust(1, 1, -1, -1);
    };
    if (window->exStyle & WS_EX_CLIENTEDGE) {
      edge(COLOR_3DSHADOW, COLOR_3DHIGHLIGHT);
      edge(COLOR_3DDKSHADOW, COLOR_3DLIGHT);
    }
    if (window->style & WS_BORDER)
      edge(COLOR_WINDOWFRAME, COLOR_WINDOWFRAME);
  }

  bool focusNextPrevChild(bool) override { return false; }

  void resizeEvent(QResizeEvent *event) override {
    QWidget::resizeEvent(event);
    if (const std::shared_ptr<Window> window = windowOf(this)) {
      meos_qt::layoutCanvas(*window);
      meos_qt::reportGeometry(window);
    }
  }

  void moveEvent(QMoveEvent *event) override {
    QWidget::moveEvent(event);
    if (const std::shared_ptr<Window> window = windowOf(this))
      meos_qt::reportGeometry(window);
  }

  // Closing a top-level window asks the window procedure; DefWindowProc destroys it.
  void closeEvent(QCloseEvent *event) override {
    event->ignore();
    const std::shared_ptr<Window> window = windowOf(this);
    if (window && !window->destroying)
      meos_qt::callWindowProc(window, WM_CLOSE, 0, 0);
  }

  void changeEvent(QEvent *event) override {
    QWidget::changeEvent(event);
    if (event->type() != QEvent::ActivationChange || !isWindow())
      return;
    if (const std::shared_ptr<Window> window = windowOf(this))
      meos_qt::callWindowProc(window, WM_ACTIVATE, isActiveWindow() ? WA_ACTIVE : WA_INACTIVE, 0);
  }

  // Keys reach the frame of a top-level window that has no focus widget.
  void keyPressEvent(QKeyEvent *event) override { sendKey(this, event); }
  void keyReleaseEvent(QKeyEvent *event) override { sendKey(this, event); }
};

// A scroll bar of a window. As on Windows, user actions only send WM_VSCROLL or
// WM_HSCROLL; the window procedure moves the thumb with SetScrollInfo.
class CanvasScrollBar : public QScrollBar {
public:
  CanvasScrollBar(int bar, QWidget *parent)
      : QScrollBar(bar == SB_VERT ? Qt::Vertical : Qt::Horizontal, parent), bar(bar) {
    setFocusPolicy(Qt::NoFocus);
    setContextMenuPolicy(Qt::NoContextMenu);
    connect(this, &QAbstractSlider::actionTriggered, this, [this](int action) { onAction(action); });
  }

protected:
  void mouseReleaseEvent(QMouseEvent *event) override {
    const bool dragging = isSliderDown();
    QScrollBar::mouseReleaseEvent(event);
    std::shared_ptr<Window> window = windowOf(this);
    if (!window)
      return;
    if (dragging) {
      send(window, SB_THUMBPOSITION, window->scroll[bar].trackPos);
      if (!(window = windowOf(this)))
        return;
    }
    send(window, SB_ENDSCROLL, 0);
    showPosition();
  }

  void wheelEvent(QWheelEvent *event) override { sendWheel(this, event); }

private:
  void onAction(int action) {
    std::shared_ptr<Window> window = windowOf(this);
    if (!window)
      return;
    int code;
    switch (action) {
    case SliderSingleStepAdd: code = SB_LINEDOWN; break;
    case SliderSingleStepSub: code = SB_LINEUP; break;
    case SliderPageStepAdd: code = SB_PAGEDOWN; break;
    case SliderPageStepSub: code = SB_PAGEUP; break;
    case SliderToMinimum: code = SB_TOP; break;
    case SliderToMaximum: code = SB_BOTTOM; break;
    case SliderMove:
      code = SB_THUMBTRACK;
      window->scroll[bar].trackPos = sliderPosition();
      break;
    default:
      return;
    }
    send(window, code, code == SB_THUMBTRACK ? window->scroll[bar].trackPos : 0);
    if (code != SB_THUMBTRACK)
      showPosition();
  }

  void send(const std::shared_ptr<Window> &window, int code, int position) {
    meos_qt::callWindowProc(window, bar == SB_VERT ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(code, position), 0);
  }

  // Moves the thumb back to the position the application has set. setValue also
  // sets the slider position that QAbstractSlider takes as value after
  // actionTriggered, but unlike setSliderPosition triggers no action.
  void showPosition() {
    if (const std::shared_ptr<Window> window = windowOf(this))
      setValue(window->scroll[bar].pos);
  }

  const int bar;
};

void showScrollPosition(Window &window, int bar) {
  QScrollBar *scrollBar = window.scrollBars[bar];
  if (!scrollBar)
    return;
  const meos_qt::ScrollBarState &state = window.scroll[bar];
  const int page = int(state.page);
  scrollBar->setRange(state.min, std::max(state.min, state.max - std::max(page - 1, 0)));
  scrollBar->setPageStep(std::max(page, 1));
  if (!scrollBar->isSliderDown())
    scrollBar->setValue(state.pos);
}

} // namespace

/* ---------------------------------------------------------------------
   Internal interface
   --------------------------------------------------------------------- */

QWidget *meos_qt::createFrame(Window &window, QWidget *parentWidget) {
  auto *frame = new CanvasFrame(parentWidget);
  attachWidget(frame, window.handle);
  window.frame = frame;
  return frame;
}

void meos_qt::createCanvas(Window &window, QWidget *parentWidget) {
  QWidget *frame = createFrame(window, parentWidget);
  window.client = new CanvasClient(frame);
  window.surface = createWindowSurface(window.handle);
  for (int bar : {SB_HORZ, SB_VERT}) {
    window.scrollBars[bar] = new CanvasScrollBar(bar, frame);
    window.scrollBars[bar]->hide();
    showScrollPosition(window, bar);
  }
  applyWindowFlags(window);
}

void meos_qt::layoutCanvas(Window &window) {
  QWidget *frame = window.frame;
  QWidget *client = window.client;
  if (!frame || !client)
    return;

  const int border = window.isChild() ? borderWidth(window.style, window.exStyle) : 0;
  const QRect inner = frame->rect().adjusted(border, border, -border, -border);
  if (!window.surface) {
    // Controls draw their scroll bars themselves.
    client->setGeometry(inner);
    return;
  }
  const bool vertical = (window.style & WS_VSCROLL) != 0;
  const bool horizontal = (window.style & WS_HSCROLL) != 0;
  const int width = std::max(inner.width() - (vertical ? scrollBarExtent : 0), 0);
  const int height = std::max(inner.height() - (horizontal ? scrollBarExtent : 0), 0);

  client->setGeometry(inner.x(), inner.y(), width, height);
  if (QScrollBar *bar = window.scrollBars[SB_VERT]) {
    bar->setGeometry(inner.x() + width, inner.y(), scrollBarExtent, height);
    bar->setVisible(vertical);
  }
  if (QScrollBar *bar = window.scrollBars[SB_HORZ]) {
    bar->setGeometry(inner.x(), inner.y() + height, width, scrollBarExtent);
    bar->setVisible(horizontal);
  }
}

void meos_qt::applyWindowFlags(Window &window) {
  QWidget *frame = window.frame;
  if (!frame || window.isChild())
    return;

  Qt::WindowFlags flags = (window.exStyle & WS_EX_TOOLWINDOW) ? Qt::Tool : Qt::Window;
  if ((window.style & WS_CAPTION) != WS_CAPTION)
    flags |= Qt::FramelessWindowHint;
  if (window.exStyle & WS_EX_TOPMOST)
    flags |= Qt::WindowStaysOnTopHint;
  if (frame->windowFlags() == flags)
    return;

  // Changing the flags hides the window.
  const bool visible = frame->isVisible();
  frame->setWindowFlags(flags);
  if (visible)
    frame->show();
}

void meos_qt::dispatchPaint(const std::shared_ptr<Window> &window) {
  if (window->updateRegion.isEmpty() || window->destroying)
    return;
  // Invalidations while WM_PAINT is handled go into a new update region.
  window->paintRegion = window->updateRegion;
  window->paintErase = window->eraseRequested;
  window->updateRegion = QRegion();
  window->eraseRequested = false;
  callWindowProc(window, WM_PAINT, 0, 0);
  window->paintRegion = QRegion();
  window->paintErase = false;
}

void meos_qt::invalidate(Window &window, const QRegion &region, bool erase) {
  QWidget *client = window.client;
  if (!client)
    return;
  if (!window.surface) {
    // Controls paint themselves; their parts are child widgets (viewports, line edits).
    client->update();
    for (QWidget *part : client->findChildren<QWidget *>())
      part->update();
    return;
  }
  const QRegion clipped = region.intersected(client->rect());
  if (clipped.isEmpty())
    return;
  window.updateRegion += clipped;
  window.eraseRequested = window.eraseRequested || erase;
  client->update(clipped);
}

int meos_qt::borderWidth(DWORD style, DWORD exStyle) {
  return ((style & WS_BORDER) ? 1 : 0) + ((exStyle & WS_EX_CLIENTEDGE) ? 2 : 0);
}

/* ---------------------------------------------------------------------
   Painting and scroll bars
   --------------------------------------------------------------------- */

HDC BeginPaint(HWND window, PAINTSTRUCT *paint) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !paint)
    return nullptr;
  // Drawing is limited to the region of the WM_PAINT being handled.
  const QRegion region = target->paintRegion;
  const bool erase = std::exchange(target->paintErase, false);
  const HDC dc = meos_qt::createDc(meos_qt::DeviceContext::Kind::Paint, window, target->surface, &region);
  *paint = PAINTSTRUCT{};
  paint->hdc = dc;
  paint->rcPaint = meos_qt::toRECT(region.boundingRect());
  if (erase)
    paint->fErase = meos_qt::callWindowProc(target, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0) == 0;
  return dc;
}

BOOL EndPaint(HWND /*window*/, const PAINTSTRUCT *paint) {
  if (paint)
    meos_qt::releaseDc(paint->hdc, meos_qt::DeviceContext::Kind::Paint);
  return TRUE;
}

// Any thread may draw through GetDC; the backing store's screen copy is made in
// the GUI thread.
HDC GetDC(HWND window) {
  if (!window || window == GetDesktopWindow())
    return meos_qt::createDc(meos_qt::DeviceContext::Kind::Screen, nullptr, nullptr, nullptr);
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target)
    return nullptr;
  return meos_qt::createDc(meos_qt::DeviceContext::Kind::Window, window, target->surface, nullptr);
}

int ReleaseDC(HWND /*window*/, HDC dc) {
  if (meos_qt::releaseDc(dc, meos_qt::DeviceContext::Kind::Window) ||
      meos_qt::releaseDc(dc, meos_qt::DeviceContext::Kind::Screen))
    return 1;
  return 0;
}

// Moves the pixels of scrollRect (default: the client area) by (dx, dy) within
// clipRect. The uncovered area is invalidated with SW_INVALIDATE; a pending update
// region moves along, and SW_SCROLLCHILDREN moves the child windows.
int ScrollWindowEx(HWND window, int dx, int dy, const RECT *scrollRect, const RECT *clipRect,
                   HRGN /*updateRegion*/, LPRECT updateRect, UINT flags) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !target->client)
    return RGN_ERROR;

  const QRect client(QPoint(0, 0), meos_qt::clientSize(*target));
  const QRect scroll = scrollRect ? meos_qt::toQRect(*scrollRect) : client;
  const QRect clip = clipRect ? meos_qt::toQRect(*clipRect) & client : client;
  const QRect source = scroll & client;
  const QRect dest = source.translated(dx, dy) & clip;

  if (dx || dy) {
    if (target->surface && !dest.isEmpty())
      meos_qt::scrollSurface(*target->surface, dest, dx, dy);
    const QRegion movedUpdate = (target->updateRegion & source).translated(dx, dy) & clip;
    if (!movedUpdate.isEmpty())
      meos_qt::invalidate(*target, movedUpdate, target->eraseRequested);
  }

  const QRegion uncovered = QRegion(scroll & clip) - QRegion(dest);
  if (flags & SW_INVALIDATE)
    meos_qt::invalidate(*target, uncovered, (flags & SW_ERASE) != 0);

  if ((flags & SW_SCROLLCHILDREN) && (dx || dy)) {
    const std::vector<HWND> children = target->children;
    for (HWND handle : children) {
      const std::shared_ptr<Window> child = meos_qt::findWindow(handle);
      if (!child || !child->frame || (scrollRect && !meos_qt::windowRect(*child).intersects(scroll)))
        continue;
      child->frame->move(child->frame->pos() + QPoint(dx, dy));
      meos_qt::reportGeometry(child);
    }
  }

  if (updateRect)
    *updateRect = meos_qt::toRECT(uncovered.boundingRect());
  if (uncovered.isEmpty())
    return NULLREGION;
  return uncovered.rectCount() == 1 ? SIMPLEREGION : COMPLEXREGION;
}

BOOL InvalidateRect(HWND window, const RECT *rect, BOOL erase) {
  if (!window) {
    // All windows, as on Windows.
    for (const std::shared_ptr<Window> &target : meos_qt::allWindows()) {
      if (target->client)
        meos_qt::invalidate(*target, target->client->rect(), erase != FALSE);
    }
    return TRUE;
  }
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !target->client)
    return FALSE;
  const QRect area = rect ? QRect(rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top)
                          : target->client->rect();
  meos_qt::invalidate(*target, area, erase != FALSE);
  return TRUE;
}

BOOL UpdateWindow(HWND window) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target)
    return FALSE;
  if (IsWindowVisible(window))
    meos_qt::dispatchPaint(target);
  return TRUE;
}

BOOL RedrawWindow(HWND window, const RECT *updateRect, HRGN /*updateRegion*/, UINT flags) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !target->client)
    return FALSE;
  if (!(flags & RDW_INVALIDATE))
    return TRUE;

  const QRect area = updateRect ? QRect(updateRect->left, updateRect->top, updateRect->right - updateRect->left,
                                        updateRect->bottom - updateRect->top)
                                : target->client->rect();
  meos_qt::invalidate(*target, area, (flags & RDW_ERASE) != 0);
  if (flags & RDW_ALLCHILDREN) {
    for (HWND child : target->children)
      RedrawWindow(child, nullptr, nullptr, flags);
  }
  return TRUE;
}

int SetScrollInfo(HWND window, int bar, const SCROLLINFO *info, BOOL /*redraw*/) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !info || (bar != SB_HORZ && bar != SB_VERT))
    return 0;

  meos_qt::ScrollBarState &state = target->scroll[bar];
  if (info->fMask & SIF_RANGE) {
    state.min = info->nMin;
    state.max = info->nMax;
  }
  if (info->fMask & SIF_PAGE)
    state.page = info->nPage;
  if (info->fMask & SIF_POS)
    state.pos = info->nPos;

  // Clamped as by Windows: the page is at most the range, the position at most
  // the last position at which a whole page fits.
  const long long range = static_cast<long long>(state.max) - state.min + 1;
  state.page = static_cast<UINT>(std::min<long long>(state.page, std::max(range, 0LL)));
  const int lastPos = std::max(state.min, state.max - std::max(int(state.page) - 1, 0));
  state.pos = std::clamp(state.pos, state.min, lastPos);
  if (info->fMask & SIF_POS)
    state.trackPos = state.pos;

  if (info->fMask & (SIF_RANGE | SIF_PAGE)) {
    // The scroll bar is shown while there is something to scroll.
    const bool needed = state.page ? static_cast<long long>(state.page) < range : state.max > state.min;
    const DWORD styleBit = bar == SB_VERT ? WS_VSCROLL : WS_HSCROLL;
    if (needed != ((target->style & styleBit) != 0)) {
      target->style ^= styleBit;
      meos_qt::layoutCanvas(*target);
      showScrollPosition(*target, bar);
      meos_qt::reportGeometry(target);
      return target->scroll[bar].pos;
    }
  }
  showScrollPosition(*target, bar);
  return state.pos;
}

BOOL GetScrollInfo(HWND window, int bar, SCROLLINFO *info) {
  const std::shared_ptr<Window> target = meos_qt::windowOrError(window);
  if (!target || !info || (bar != SB_HORZ && bar != SB_VERT))
    return FALSE;
  const meos_qt::ScrollBarState &state = target->scroll[bar];
  if (info->fMask & SIF_RANGE) {
    info->nMin = state.min;
    info->nMax = state.max;
  }
  if (info->fMask & SIF_PAGE)
    info->nPage = state.page;
  if (info->fMask & SIF_POS)
    info->nPos = state.pos;
  if (info->fMask & SIF_TRACKPOS)
    info->nTrackPos = state.trackPos;
  return TRUE;
}
