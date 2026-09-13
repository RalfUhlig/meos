/************************************************************************
    MeOS - Orienteering Software
    Linux port: the common controls MeOS uses: tooltips, the toolbar and image
    lists.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_gdi.h"

#include <QHelpEvent>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>

#include <map>

#include "commctrl.h"

namespace {

using meos_qt::Control;
using meos_qt::Window;

/* ---------------------------------------------------------------------
   Image lists
   --------------------------------------------------------------------- */

struct ImageList {
  int width = 0;
  int height = 0;
  std::vector<QPixmap> images;
};

std::map<HIMAGELIST, std::shared_ptr<ImageList>> imageLists;
std::uintptr_t lastImageList = 0;

std::shared_ptr<ImageList> findImageList(HIMAGELIST handle) {
  const auto entry = imageLists.find(handle);
  return entry == imageLists.end() ? nullptr : entry->second;
}

HIMAGELIST registerImageList(std::shared_ptr<ImageList> list) {
  const auto handle = reinterpret_cast<HIMAGELIST>(++lastImageList);
  imageLists[handle] = std::move(list);
  return handle;
}

// Splits a bitmap strip into images of the given width. The mask colour becomes
// transparent; CLR_DEFAULT takes the colour of the top left pixel.
void addStrip(ImageList &list, QImage strip, COLORREF mask) {
  strip = strip.convertToFormat(QImage::Format_ARGB32);
  if (mask != CLR_NONE && !strip.isNull()) {
    const QRgb transparent = mask == CLR_DEFAULT ? strip.pixel(0, 0)
                                                 : qRgb(GetRValue(mask), GetGValue(mask), GetBValue(mask));
    for (int y = 0; y < strip.height(); y++) {
      auto *line = reinterpret_cast<QRgb *>(strip.scanLine(y));
      for (int x = 0; x < strip.width(); x++) {
        if ((line[x] & 0xFFFFFF) == (transparent & 0xFFFFFF))
          line[x] = 0;
      }
    }
  }
  for (int x = 0; list.width > 0 && x + list.width <= strip.width(); x += list.width)
    list.images.push_back(QPixmap::fromImage(strip.copy(x, 0, list.width, strip.height())));
}

// The standard toolbar images (IDB_STD_LARGE_COLOR), from the icon theme. Without
// a theme icon the image shows the first letter of its name.
QPixmap standardImage(int index, int width, int height) {
  static const char *const names[][2] = {
      {"edit-cut", "X"},        {"edit-copy", "C"},      {"edit-paste", "P"},
      {"edit-undo", "U"},       {"edit-redo", "R"},      {"edit-delete", "D"},
      {"document-new", "N"},    {"document-open", "O"},  {"document-save", "S"},
      {"document-print-preview", "V"}, {"document-properties", "I"}, {"help-contents", "?"},
      {"edit-find", "F"},       {"edit-find-replace", "H"}, {"document-print", "P"},
  };
  const QIcon icon = QIcon::fromTheme(QString::fromLatin1(names[index][0]));
  if (!icon.isNull())
    return icon.pixmap(width, height);
  QPixmap pixmap(width, height);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setPen(QColor(80, 80, 80));
  painter.drawRect(2, 2, width - 5, height - 5);
  QFont font = painter.font();
  font.setPixelSize(height / 2);
  painter.setFont(font);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, QString::fromLatin1(names[index][1]));
  return pixmap;
}

constexpr int standardImageCount = 15;

/* ---------------------------------------------------------------------
   Tooltips. Qt shows a tool's text on its own hover event; TTM_RELAYEVENT is
   not needed.
   --------------------------------------------------------------------- */

struct Tool {
  UINT flags;
  HWND window;
  UINT_PTR id;
  RECT rect;
  QString text;
};

class ToolTipControl : public Control {
public:
  std::vector<Tool> tools;
  int maxWidth = -1;
};

std::vector<std::weak_ptr<ToolTipControl>> toolTipControls;

std::vector<Tool>::iterator findTool(ToolTipControl &control, const TOOLINFOW &info) {
  return std::find_if(control.tools.begin(), control.tools.end(),
                      [&info](const Tool &tool) { return tool.window == info.hwnd && tool.id == info.uId; });
}

QString toolText(const TOOLINFOW &info) {
  // LPSTR_TEXTCALLBACK and resource ids are not used by MeOS.
  if (!info.lpszText || IS_INTRESOURCE(info.lpszText) || info.lpszText == reinterpret_cast<LPWSTR>(-1))
    return QString();
  return QString::fromWCharArray(info.lpszText);
}

// The tool's window gets tooltips also while the application is inactive (TTS_ALWAYSTIP).
void allowToolTips(HWND window) {
  if (const std::shared_ptr<Window> target = meos_qt::findWindow(window)) {
    if (target->frame)
      target->frame->window()->setAttribute(Qt::WA_AlwaysShowToolTips);
  }
}

LRESULT CALLBACK toolTipProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const std::shared_ptr<Window> window = meos_qt::findWindow(hwnd);
  const auto control = window ? std::dynamic_pointer_cast<ToolTipControl>(window->control) : nullptr;
  if (!control)
    return DefWindowProc(hwnd, message, wParam, lParam);
  const auto *info = reinterpret_cast<const TOOLINFOW *>(lParam);

  switch (message) {
  case TTM_ADDTOOLW: {
    if (!info)
      return FALSE;
    const HWND toolWindow = (info->uFlags & TTF_IDISHWND) ? reinterpret_cast<HWND>(info->uId) : info->hwnd;
    control->tools.push_back(Tool{info->uFlags, info->hwnd, info->uId, info->rect, toolText(*info)});
    allowToolTips(toolWindow);
    return TRUE;
  }
  case TTM_DELTOOLW:
    if (info) {
      const auto tool = findTool(*control, *info);
      if (tool != control->tools.end())
        control->tools.erase(tool);
    }
    return 0;
  case TTM_NEWTOOLRECTW:
    if (info) {
      const auto tool = findTool(*control, *info);
      if (tool != control->tools.end())
        tool->rect = info->rect;
    }
    return 0;
  case TTM_UPDATETIPTEXTW:
    if (info) {
      const auto tool = findTool(*control, *info);
      if (tool != control->tools.end())
        tool->text = toolText(*info);
    }
    return 0;
  case TTM_SETMAXTIPWIDTH: {
    const int previous = control->maxWidth;
    control->maxWidth = int(lParam);
    return previous;
  }
  case TTM_RELAYEVENT:
    return 0;
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}

void createToolTip(Window &window, QWidget * /*parentWidget*/) {
  // The tooltip window itself is never shown; QToolTip displays the texts.
  auto *frame = new QWidget(nullptr, Qt::ToolTip);
  meos_qt::attachWidget(frame, window.handle);
  window.frame = frame;
  auto control = std::make_shared<ToolTipControl>();
  window.control = control;
  toolTipControls.push_back(control);
}

// Line breaks only take effect with a maximum width, as with TTM_SETMAXTIPWIDTH.
QString formatToolTip(const QString &text, int maxWidth, const QFont &font) {
  if (maxWidth < 0)
    return QStringLiteral("<qt style='white-space:pre'>") + QString(text).replace(QLatin1Char('\n'), QLatin1Char(' ')).toHtmlEscaped() +
           QStringLiteral("</qt>");
  QString html = text.toHtmlEscaped();
  html.replace(QLatin1Char('\n'), QLatin1String("<br>"));
  const QFontMetrics metrics(font);
  int widest = 0;
  for (const QString &line : text.split(QLatin1Char('\n')))
    widest = std::max(widest, metrics.horizontalAdvance(line));
  if (widest > maxWidth)
    return QStringLiteral("<qt><table width='%1'><tr><td>").arg(maxWidth) + html + QStringLiteral("</td></tr></table></qt>");
  return QStringLiteral("<qt style='white-space:pre'>") + html + QStringLiteral("</qt>");
}

/* ---------------------------------------------------------------------
   Toolbar
   --------------------------------------------------------------------- */

class ToolbarControl : public Control {
public:
  QPointer<QToolBar> bar;
  std::map<int, HIMAGELIST> imageLists;
  int buttonStructSize = sizeof(TBBUTTON);
  int version = 0;

  // Button size: the image size plus the padding of the toolbar (7 x 6).
  QSize buttonSize() const {
    const auto entry = imageLists.find(0);
    const std::shared_ptr<ImageList> list = entry == imageLists.end() ? nullptr : findImageList(entry->second);
    if (!list)
      return QSize(23, 22);
    return QSize(list->width + 7, list->height + 6);
  }
};

LRESULT CALLBACK toolbarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const std::shared_ptr<Window> window = meos_qt::findWindow(hwnd);
  const auto control = window ? std::dynamic_pointer_cast<ToolbarControl>(window->control) : nullptr;
  QToolBar *bar = control ? control->bar.data() : nullptr;
  if (!bar)
    return DefWindowProc(hwnd, message, wParam, lParam);

  switch (message) {
  case CCM_SETVERSION: {
    const int previous = control->version;
    control->version = int(wParam);
    return previous;
  }
  case TB_BUTTONSTRUCTSIZE:
    control->buttonStructSize = int(wParam);
    return 0;
  case TB_SETIMAGELIST: {
    const HIMAGELIST previous = control->imageLists[int(wParam)];
    control->imageLists[int(wParam)] = reinterpret_cast<HIMAGELIST>(lParam);
    return reinterpret_cast<LRESULT>(previous);
  }
  case TB_LOADIMAGES: {
    // The standard images go into image list 0.
    if (lParam != reinterpret_cast<LPARAM>(HINST_COMMCTRL) || wParam != IDB_STD_LARGE_COLOR)
      return 0;
    const std::shared_ptr<ImageList> list = findImageList(control->imageLists[0]);
    if (!list)
      return 0;
    for (int i = 0; i < standardImageCount; i++)
      list->images.push_back(standardImage(i, list->width, list->height));
    return standardImageCount;
  }
  case TB_ADDBUTTONS: {
    const auto *buttons = reinterpret_cast<const TBBUTTON *>(lParam);
    const QSize size = control->buttonSize();
    bar->setIconSize(size - QSize(7, 6));
    for (WPARAM i = 0; buttons && i < wParam; i++) {
      const TBBUTTON &button = buttons[i];
      if (button.fsStyle & 0x01) { // BTNS_SEP
        bar->addSeparator();
        continue;
      }
      QAction *action = bar->addAction(QString());
      const std::shared_ptr<ImageList> list = findImageList(control->imageLists[HIWORD(button.iBitmap)]);
      const int image = LOWORD(button.iBitmap);
      if (list && image < int(list->images.size()))
        action->setIcon(QIcon(list->images[std::size_t(image)]));
      // iString is a string pointer or an index into the toolbar strings.
      if (button.iString && !IS_INTRESOURCE(button.iString)) {
        const QString text = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(button.iString));
        if (window->style & TBSTYLE_TOOLTIPS)
          action->setToolTip(text);
      }
      action->setEnabled((button.fsState & TBSTATE_ENABLED) != 0);
      if (QWidget *widget = bar->widgetForAction(action)) {
        widget->setFixedSize(size);
        widget->setFocusPolicy(Qt::NoFocus);
      }
      const int command = button.idCommand;
      QObject::connect(action, &QAction::triggered, bar, [bar, command] {
        const std::shared_ptr<Window> target = meos_qt::findWindow(meos_qt::windowFromWidget(bar));
        if (!target)
          return;
        if (const std::shared_ptr<Window> parent = meos_qt::findWindow(target->parent))
          meos_qt::callWindowProc(parent, WM_COMMAND, MAKEWPARAM(command, BN_CLICKED),
                                  reinterpret_cast<LPARAM>(target->handle));
      });
    }
    return TRUE;
  }
  case TB_SETMAXTEXTROWS:
    return TRUE;
  case TB_GETBUTTONSIZE: {
    const QSize size = control->buttonSize();
    return MAKELRESULT(size.width(), size.height());
  }
  case TB_AUTOSIZE: {
    // Along the top of the parent's client area.
    RECT parentRect = {0, 0, 0, 0};
    GetClientRect(window->parent, &parentRect);
    const int height = control->buttonSize().height() + 4;
    SetWindowPos(hwnd, nullptr, 0, 0, parentRect.right, height, SWP_NOZORDER);
    return 0;
  }
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}

void createToolbar(Window &window, QWidget *parentWidget) {
  QWidget *frame = meos_qt::createFrame(window, parentWidget);
  auto control = std::make_shared<ToolbarControl>();
  auto *bar = new QToolBar(frame);
  bar->setMovable(false);
  bar->setFloatable(false);
  bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  bar->setContentsMargins(0, 0, 0, 0);
  bar->setFocusPolicy(Qt::NoFocus);
  control->bar = bar;
  window.control = control;
  window.client = bar;
}

meos_qt::WindowClass commonClass(const wchar_t *name, WNDPROC proc, void (*create)(Window &, QWidget *)) {
  meos_qt::WindowClass windowClass;
  windowClass.name = name;
  windowClass.proc = proc;
  windowClass.cursor = LoadCursor(nullptr, IDC_ARROW);
  windowClass.createWidgets = create;
  return windowClass;
}

} // namespace

void meos_qt::registerCommonControlClasses(const std::function<void(const WindowClass &)> &add) {
  add(commonClass(TOOLTIPS_CLASS, toolTipProc, createToolTip));
  add(commonClass(TOOLBARCLASSNAME, toolbarProc, createToolbar));
}

bool meos_qt::showToolTip(QWidget *receiver, QHelpEvent *event) {
  const HWND window = windowFromWidget(receiver);
  if (!window)
    return false;
  const std::shared_ptr<Window> target = findWindow(window);

  toolTipControls.erase(std::remove_if(toolTipControls.begin(), toolTipControls.end(),
                                       [](const std::weak_ptr<ToolTipControl> &c) { return c.expired(); }),
                        toolTipControls.end());
  for (const std::weak_ptr<ToolTipControl> &weak : toolTipControls) {
    const std::shared_ptr<ToolTipControl> control = weak.lock();
    for (const Tool &tool : control->tools) {
      QRect area;
      if (tool.flags & TTF_IDISHWND) {
        if (reinterpret_cast<HWND>(tool.id) != window)
          continue;
        area = receiver->rect();
      }
      else {
        if (tool.window != window || !target || !target->client)
          continue;
        const QRect rect = toQRect(tool.rect);
        const QPoint position = target->client->mapFromGlobal(event->globalPos());
        if (!rect.contains(position))
          continue;
        area = QRect(receiver->mapFromGlobal(target->client->mapToGlobal(rect.topLeft())), rect.size());
      }
      if (tool.text.isEmpty())
        break;
      QToolTip::showText(event->globalPos(), formatToolTip(tool.text, control->maxWidth, QToolTip::font()),
                         receiver, area);
      return true;
    }
  }
  // Toolbar buttons carry their tooltip themselves.
  if (!receiver->toolTip().isEmpty())
    return false;
  // Windows shows no tooltip for a place without a tool, also not the parent's.
  QToolTip::hideText();
  event->ignore();
  return true;
}

void InitCommonControls() {}

HIMAGELIST ImageList_Create(int cx, int cy, UINT /*flags*/, int /*initial*/, int /*grow*/) {
  if (cx <= 0 || cy <= 0)
    return nullptr;
  auto list = std::make_shared<ImageList>();
  list->width = cx;
  list->height = cy;
  return registerImageList(std::move(list));
}

HIMAGELIST ImageList_LoadImage(HINSTANCE instance, LPCWSTR bitmap, int cx, int /*grow*/, COLORREF mask, UINT type,
                               UINT /*flags*/) {
  if (type != IMAGE_BITMAP || cx <= 0)
    return nullptr;
  const QImage strip = meos_qt::bitmapResource(instance, bitmap);
  if (strip.isNull())
    return nullptr;
  auto list = std::make_shared<ImageList>();
  list->width = cx;
  list->height = strip.height();
  addStrip(*list, strip, mask);
  return registerImageList(std::move(list));
}

BOOL ImageList_Destroy(HIMAGELIST imageList) {
  return imageLists.erase(imageList) ? TRUE : FALSE;
}
