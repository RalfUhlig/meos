/************************************************************************
    MeOS - Orienteering Software
    Linux port: message boxes, common dialogs, popup menus and the shell on Qt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// A message box is a window of the dialog class with BUTTON controls, as on Windows,
// so that a WH_CBT hook can rename, measure and move the buttons (gdioutput::ask).
// The file, folder and colour dialogs and popup menus are Qt dialogs and menus. All
// of them run a nested event loop, in which posted messages are dispatched as in the
// modal loops of Windows.

#include "win32_gdi.h"

#include <QColorDialog>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QStyle>
#include <QThread>
#include <QUrl>

#include <map>
#include <set>

#include "commctrl.h"
#include "commdlg.h"
#include "shellapi.h"
#include "shlobj.h"

// The item ID list of a folder: here just its path.
struct _ITEMIDLIST {
  std::wstring path;
};

namespace {

using meos_qt::Window;

constexpr DWORD errorInvalidMessageBoxStyle = 1438; // ERROR_INVALID_MSGBOX_STYLE

const wchar_t *const dialogClassName = L"#32770";

// Last error of a common dialog, for CommDlgExtendedError.
thread_local DWORD dialogError = 0;

std::wstring toWide(const QString &text) {
  return text.toStdWString();
}

QString fromWide(LPCWSTR text) {
  return text ? QString::fromWCharArray(text) : QString();
}

// The Qt widget that dialogs of an owner window are placed on: the frame of its
// top-level window.
QWidget *ownerWidget(HWND owner) {
  std::shared_ptr<Window> window = meos_qt::findWindow(owner);
  while (window && window->isChild())
    window = meos_qt::findWindow(window->parent);
  return window ? window->frame.data() : nullptr;
}

HWND topLevelWindow(HWND owner) {
  std::shared_ptr<Window> window = meos_qt::findWindow(owner);
  while (window && window->isChild())
    window = meos_qt::findWindow(window->parent);
  return window ? window->handle : nullptr;
}

// Runs a Qt dialog or menu modally. Windows releases the mouse capture when a
// modal loop starts; with the capture kept, clicks on the dialog would go to the
// capture window.
template <typename Run>
auto runModal(Run &&run) {
  ReleaseCapture();
  meos_qt::HandlerScope scope;
  return run();
}

/* ---------------------------------------------------------------------
   Message boxes
   --------------------------------------------------------------------- */

constexpr UINT typeMask = 0x0000000F;
constexpr UINT iconMask = 0x000000F0;

// Measures of the Windows 10 message box at 96 DPI (logical pixels). To be matched
// with screenshots in step 1.2.7.
constexpr int boxMargin = 24;      // around icon and text
constexpr int iconSize = 32;
constexpr int iconTextGap = 12;
constexpr int bandPadding = 12;    // around the buttons in the lower band
constexpr int buttonWidth = 88;    // 50 dialog units with Segoe UI 9 pt
constexpr int buttonHeight = 26;   // 14 dialog units
constexpr int buttonGap = 8;
constexpr int messageFontHeight = -12; // Segoe UI 9 pt, the message font

struct MessageBoxState {
  std::vector<int> buttons;
  std::wstring text;
  HFONT font = nullptr;
  QPixmap icon;
  QRect textRect;
  QPoint iconPosition;
  int bandTop = 0;

  int result = 0;
  bool done = false;
  QEventLoop *loop = nullptr;
};
std::map<HWND, MessageBoxState *> messageBoxes;

void endMessageBox(MessageBoxState &state, int result) {
  if (state.done)
    return;
  state.done = true;
  state.result = result;
  if (state.loop)
    state.loop->quit();
}

bool hasButton(const MessageBoxState &state, int id) {
  return std::find(state.buttons.begin(), state.buttons.end(), id) != state.buttons.end();
}

// Escape, the close button and IDCANCEL: Cancel if there is one, OK if it is the
// only button, otherwise nothing.
void cancelMessageBox(MessageBoxState &state) {
  if (hasButton(state, IDCANCEL))
    endMessageBox(state, IDCANCEL);
  else if (state.buttons.size() == 1 && state.buttons[0] == IDOK)
    endMessageBox(state, IDOK);
}

LRESULT CALLBACK messageBoxProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  const auto entry = messageBoxes.find(window);
  MessageBoxState *state = entry == messageBoxes.end() ? nullptr : entry->second;
  if (!state)
    return DefWindowProc(window, message, wParam, lParam);

  switch (message) {
  case WM_COMMAND: {
    const int id = LOWORD(wParam);
    if (id == IDCANCEL)
      cancelMessageBox(*state);
    else if (hasButton(*state, id))
      endMessageBox(*state, id);
    return 0;
  }
  case WM_CLOSE:
    cancelMessageBox(*state);
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT paint;
    const HDC dc = BeginPaint(window, &paint);
    RECT client;
    GetClientRect(window, &client);
    if (const std::shared_ptr<meos_qt::DeviceContext> context = meos_qt::findDc(dc)) {
      const QRect area = meos_qt::toQRect(client);
      meos_qt::paint(*context, area, [&](QPainter &painter) {
        painter.fillRect(QRect(0, 0, area.width(), state->bandTop), meos_qt::toQColor(GetSysColor(COLOR_WINDOW)));
        painter.fillRect(QRect(0, state->bandTop, area.width(), area.height() - state->bandTop),
                         meos_qt::toQColor(GetSysColor(COLOR_3DFACE)));
        if (!state->icon.isNull())
          painter.drawPixmap(state->iconPosition, state->icon);
      });
    }
    const HGDIOBJ oldFont = SelectObject(dc, state->font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    RECT textRect = meos_qt::toRECT(state->textRect);
    DrawText(dc, state->text.c_str(), -1, &textRect, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    EndPaint(window, &paint);
    return 0;
  }
  case WM_DESTROY:
    // Destroyed from outside, for example with its owner.
    endMessageBox(*state, 0);
    messageBoxes.erase(window);
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

QString buttonText(int id) {
  switch (id) {
  case IDOK: return QCoreApplication::translate("QPlatformTheme", "OK");
  case IDCANCEL: return QCoreApplication::translate("QPlatformTheme", "Cancel");
  case IDYES: return QCoreApplication::translate("QPlatformTheme", "&Yes");
  case IDNO: return QCoreApplication::translate("QPlatformTheme", "&No");
  default: return QString();
  }
}

QPixmap messageIcon(UINT type) {
  QStyle::StandardPixmap pixmap;
  switch (type & iconMask) {
  case MB_ICONHAND: pixmap = QStyle::SP_MessageBoxCritical; break;
  case MB_ICONQUESTION: pixmap = QStyle::SP_MessageBoxQuestion; break;
  case MB_ICONEXCLAMATION: pixmap = QStyle::SP_MessageBoxWarning; break;
  case MB_ICONINFORMATION: pixmap = QStyle::SP_MessageBoxInformation; break;
  default: return QPixmap();
  }
  return QApplication::style()->standardIcon(pixmap).pixmap(iconSize, iconSize);
}

int showMessageBox(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type, bool callHooks) {
  MessageBoxState state;
  switch (type & typeMask) {
  case MB_OK: state.buttons = {IDOK}; break;
  case MB_OKCANCEL: state.buttons = {IDOK, IDCANCEL}; break;
  case MB_YESNOCANCEL: state.buttons = {IDYES, IDNO, IDCANCEL}; break;
  case MB_YESNO: state.buttons = {IDYES, IDNO}; break;
  default:
    SetLastError(errorInvalidMessageBoxStyle);
    return 0;
  }
  state.text = text ? text : L"";
  state.icon = messageIcon(type);
  state.font = CreateFont(messageFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

  const HWND ownerTop = topLevelWindow(owner);
  const HWND previousFocus = GetFocus();
  const HINSTANCE instance = meos_qt::applicationInstance();
  const HWND box = CreateWindowEx(0, dialogClassName, caption ? caption : L"Error",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                                  ownerTop, nullptr, instance, nullptr);
  if (!box) {
    DeleteObject(state.font);
    return 0;
  }
  messageBoxes[box] = &state;

  // Buttons of equal width, wide enough for every text.
  std::vector<HWND> buttons;
  int width = buttonWidth;
  for (std::size_t i = 0; i < state.buttons.size(); i++) {
    const std::wstring label = toWide(buttonText(state.buttons[i]));
    const HWND button = CreateWindowEx(0, L"Button", label.c_str(),
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                                       0, 0, buttonWidth, buttonHeight, box,
                                       reinterpret_cast<HMENU>(std::intptr_t(state.buttons[i])), instance, nullptr);
    SendMessage(button, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), 0);
    SIZE ideal;
    if (Button_GetIdealSize(button, &ideal))
      width = std::max<int>(width, ideal.cx);
    buttons.push_back(button);
  }

  // Text wrapped to at most 5/8 of the screen width, as long lines make Windows
  // message boxes wide.
  QRect available(0, 0, 1024, 768);
  if (QWidget *ownerFrame = ownerWidget(ownerTop); ownerFrame && ownerFrame->screen())
    available = ownerFrame->screen()->availableGeometry();
  else if (const QScreen *screen = QGuiApplication::primaryScreen())
    available = screen->availableGeometry();
  RECT measure = {0, 0, std::max(available.width() * 5 / 8, 200), 0};
  const HDC screenDc = GetDC(nullptr);
  const HGDIOBJ oldFont = SelectObject(screenDc, state.font);
  DrawText(screenDc, state.text.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
  SelectObject(screenDc, oldFont);
  ReleaseDC(nullptr, screenDc);
  const QSize textSize(measure.right - measure.left, measure.bottom - measure.top);

  const int textLeft = boxMargin + (state.icon.isNull() ? 0 : iconSize + iconTextGap);
  const int contentHeight = std::max(textSize.height(), state.icon.isNull() ? 0 : iconSize);
  const int textTop = boxMargin + (contentHeight - textSize.height()) / 2;
  state.textRect = QRect(QPoint(textLeft, textTop), textSize);
  state.iconPosition = QPoint(boxMargin, boxMargin + (contentHeight - iconSize) / 2);
  state.bandTop = boxMargin * 2 + contentHeight;

  const int count = int(buttons.size());
  const int buttonsWidth = count * width + (count - 1) * buttonGap + 2 * bandPadding;
  const QSize client(std::min(std::max(textLeft + textSize.width() + boxMargin, buttonsWidth), available.width()),
                     state.bandTop + buttonHeight + 2 * bandPadding);
  for (int i = 0; i < count; i++) {
    const int x = client.width() - bandPadding - (count - i) * width - (count - 1 - i) * buttonGap;
    SetWindowPos(buttons[std::size_t(i)], nullptr, x, state.bandTop + bandPadding, width, buttonHeight, SWP_NOZORDER);
  }

  // Centred on the monitor of the owner, as DS_CENTER does.
  const std::shared_ptr<Window> window = meos_qt::findWindow(box);
  if (window && window->frame) {
    QWidget *frame = window->frame;
    const bool closable = hasButton(state, IDCANCEL) || state.buttons == std::vector<int>{IDOK};
    Qt::WindowFlags flags = Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint | Qt::CustomizeWindowHint | Qt::WindowTitleHint;
    if (closable)
      flags |= Qt::WindowCloseButtonHint;
    frame->setWindowFlags(flags);
    if (ownerTop)
      frame->setWindowModality(Qt::WindowModal);
  }
  SetWindowPos(box, nullptr, available.x() + (available.width() - client.width()) / 2,
               available.y() + (available.height() - client.height()) / 2, client.width(), client.height(),
               SWP_NOZORDER);
  if (window && window->frame)
    window->frame->setFixedSize(window->frame->size());

  ShowWindow(box, SW_SHOW);
  // As on Windows, the hook runs when the shown box is activated, before the default
  // button gets the focus and before anything is painted.
  if (callHooks && meos_qt::hasHooks(WH_CBT) && meos_qt::findWindow(box)) {
    CBTACTIVATESTRUCT activate = {FALSE, GetFocus()};
    meos_qt::callHooks(WH_CBT, HCBT_ACTIVATE, reinterpret_cast<WPARAM>(box), reinterpret_cast<LPARAM>(&activate));
  }

  if (meos_qt::findWindow(box)) {
    if (!buttons.empty() && meos_qt::findWindow(buttons[0]))
      SetFocus(buttons[0]);
    QEventLoop loop;
    state.loop = &loop;
    runModal([&] { return state.done ? 0 : loop.exec(QEventLoop::DialogExec); });
    state.loop = nullptr;
  }

  if (meos_qt::findWindow(box))
    DestroyWindow(box);
  messageBoxes.erase(box);
  DeleteObject(state.font);
  // Windows gives the focus back to the window that had it in the owner.
  if (previousFocus && meos_qt::findWindow(previousFocus) && !GetFocus())
    SetFocus(previousFocus);
  return state.result;
}

} // namespace

void meos_qt::registerDialogClasses(const std::function<void(const WindowClass &)> &add) {
  WindowClass dialog;
  dialog.name = dialogClassName;
  dialog.proc = messageBoxProc;
  dialog.style = CS_DBLCLKS;
  dialog.cursor = LoadCursor(nullptr, IDC_ARROW);
  dialog.background = reinterpret_cast<HBRUSH>(std::intptr_t(COLOR_3DFACE + 1));
  add(dialog);
}

// IsDialogMessage for message boxes: Enter chooses the focused push button, Escape
// cancels, Tab and the arrow keys move the focus between the buttons.
bool meos_qt::dialogKeyEvent(QWidget *receiver, const QKeyEvent &event) {
  if (event.type() != QEvent::KeyPress)
    return false;
  const std::shared_ptr<Window> focus = findWindow(windowFromWidget(receiver));
  if (!focus)
    return false;
  const HWND box = focus->isChild() ? focus->parent : focus->handle;
  const auto entry = messageBoxes.find(box);
  if (entry == messageBoxes.end())
    return false;
  const std::shared_ptr<Window> dialog = findWindow(box);
  if (!dialog)
    return false;

  switch (event.key()) {
  case Qt::Key_Return:
  case Qt::Key_Enter: {
    int id = entry->second->buttons.empty() ? IDOK : entry->second->buttons[0];
    HWND button = GetDlgItem(box, id);
    if (focus->isChild() && hasButton(*entry->second, int(focus->id))) {
      id = int(focus->id);
      button = focus->handle;
    }
    callWindowProc(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(button));
    return true;
  }
  case Qt::Key_Escape:
    callWindowProc(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
    return true;
  case Qt::Key_Tab:
  case Qt::Key_Backtab:
  case Qt::Key_Left:
  case Qt::Key_Right:
  case Qt::Key_Up:
  case Qt::Key_Down: {
    const std::vector<HWND> &children = dialog->children;
    if (children.empty())
      return true;
    const bool back = event.key() == Qt::Key_Backtab || event.key() == Qt::Key_Left || event.key() == Qt::Key_Up ||
                      (event.key() == Qt::Key_Tab && (event.modifiers() & Qt::ShiftModifier));
    const auto current = std::find(children.begin(), children.end(), focus->handle);
    std::size_t index = current == children.end() ? 0 : std::size_t(current - children.begin());
    index = back ? (index + children.size() - 1) % children.size() : (index + 1) % children.size();
    SetFocus(children[index]);
    return true;
  }
  default:
    return false;
  }
}

int MessageBox(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type) {
  if (meos_qt::isGuiThread())
    return showMessageBox(owner, text, caption, type, true);
  // Another thread (the SI reader): the box is shown by the GUI thread, which
  // must be running its message loop. Hooks belong to their thread and are not
  // called.
  QCoreApplication *app = QCoreApplication::instance();
  if (!app)
    return 0;
  const std::wstring textCopy = text ? text : L"";
  const std::wstring captionCopy = caption ? caption : L"Error";
  int result = 0;
  QMetaObject::invokeMethod(
      app, [&] { result = showMessageBox(owner, textCopy.c_str(), captionCopy.c_str(), type, false); },
      Qt::BlockingQueuedConnection);
  return result;
}

BOOL MessageBeep(UINT /*type*/) {
  QApplication::beep();
  return TRUE;
}

/* ---------------------------------------------------------------------
   File dialogs
   --------------------------------------------------------------------- */

namespace {

// The patterns of a Windows filter ("*.txt;*.csv") for Qt. "*.*" matches every
// file on Windows, but only names with a dot in Qt.
QStringList filterPatterns(const QString &patterns) {
  QStringList result;
  for (QString pattern : patterns.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
    pattern = pattern.trimmed();
    if (pattern == QLatin1String("*.*"))
      pattern = QStringLiteral("*");
    if (!pattern.isEmpty())
      result.push_back(pattern);
  }
  return result;
}

std::vector<QString> filterPatternLists(LPCWSTR filter) {
  std::vector<QString> result;
  for (const wchar_t *entry = filter; entry && *entry;) {
    const wchar_t *patterns = entry + std::wcslen(entry) + 1;
    result.push_back(QString::fromWCharArray(patterns));
    if (!*patterns)
      break;
    entry = patterns + std::wcslen(patterns) + 1;
  }
  return result;
}

// The extension a filter implies ("txt" for "*.txt;*.csv"), or an empty string.
QString filterExtension(const QString &patterns) {
  const QStringList list = filterPatterns(patterns);
  if (list.isEmpty())
    return QString();
  const QString &first = list.front();
  if (!first.startsWith(QLatin1String("*.")))
    return QString();
  const QString extension = first.mid(2);
  return extension.contains(QLatin1Char('*')) || extension.contains(QLatin1Char('?')) ? QString() : extension;
}

BOOL runFileDialog(LPOPENFILENAME ofn, bool save) {
  dialogError = 0;
  if (!ofn || !ofn->lpstrFile || ofn->nMaxFile == 0) {
    dialogError = CDERR_INITIALIZATION;
    return FALSE;
  }

  QFileDialog dialog(ownerWidget(ofn->hwndOwner));
  dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
  dialog.setFileMode(save || !(ofn->Flags & OFN_FILEMUSTEXIST) ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
  if (save && !(ofn->Flags & OFN_OVERWRITEPROMPT))
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
  if (ofn->lpstrTitle)
    dialog.setWindowTitle(fromWide(ofn->lpstrTitle));

  const QStringList filters = meos_qt::fileDialogFilters(ofn->lpstrFilter);
  const std::vector<QString> patterns = filterPatternLists(ofn->lpstrFilter);
  if (!filters.isEmpty()) {
    dialog.setNameFilters(filters);
    const int index = ofn->nFilterIndex >= 1 && int(ofn->nFilterIndex) <= filters.size() ? int(ofn->nFilterIndex) - 1 : 0;
    dialog.selectNameFilter(filters[index]);
  }

  // The extension of the selected filter, else lpstrDefExt, is appended to a name
  // typed without one. Without lpstrDefExt nothing is appended.
  const auto updateSuffix = [&](const QString &nameFilter) {
    if (!ofn->lpstrDefExt)
      return;
    const int index = filters.indexOf(nameFilter);
    QString suffix = index >= 0 ? filterExtension(patterns[std::size_t(index)]) : QString();
    if (suffix.isEmpty())
      suffix = fromWide(ofn->lpstrDefExt);
    dialog.setDefaultSuffix(suffix);
  };
  updateSuffix(dialog.selectedNameFilter());
  QObject::connect(&dialog, &QFileDialog::filterSelected, &dialog, updateSuffix);

  const QString initialFile = QString::fromWCharArray(ofn->lpstrFile);
  if (ofn->lpstrInitialDir)
    dialog.setDirectory(QString::fromStdString(meos_compat::nativePath(ofn->lpstrInitialDir)));
  if (!initialFile.isEmpty())
    dialog.selectFile(QString::fromStdString(meos_compat::nativePath(ofn->lpstrFile)));

  if (runModal([&] { return dialog.exec(); }) != QDialog::Accepted || dialog.selectedFiles().isEmpty())
    return FALSE;

  const std::wstring path = toWide(QDir::cleanPath(dialog.selectedFiles().front()));
  if (!filters.isEmpty())
    ofn->nFilterIndex = DWORD(std::max<qsizetype>(filters.indexOf(dialog.selectedNameFilter()), 0) + 1);
  if (path.size() + 1 > ofn->nMaxFile) {
    // The required size goes into the buffer.
    ofn->lpstrFile[0] = wchar_t(std::min<std::size_t>(path.size() + 1, 0xFFFF));
    dialogError = FNERR_BUFFERTOOSMALL;
    return FALSE;
  }
  std::wmemcpy(ofn->lpstrFile, path.c_str(), path.size() + 1);

  const std::size_t slash = path.rfind(L'/');
  const std::size_t nameStart = slash == std::wstring::npos ? 0 : slash + 1;
  const std::size_t dot = path.rfind(L'.');
  ofn->nFileOffset = WORD(nameStart);
  ofn->nFileExtension = WORD(dot != std::wstring::npos && dot >= nameStart ? dot + 1 : path.size());
  if (ofn->lpstrFileTitle && ofn->nMaxFileTitle > 0) {
    const std::wstring name = path.substr(nameStart);
    const std::size_t count = std::min<std::size_t>(name.size(), ofn->nMaxFileTitle - 1);
    std::wmemcpy(ofn->lpstrFileTitle, name.c_str(), count);
    ofn->lpstrFileTitle[count] = 0;
  }
  return TRUE;
}

} // namespace

QStringList meos_qt::fileDialogFilters(LPCWSTR filter) {
  QStringList result;
  for (const wchar_t *entry = filter; entry && *entry;) {
    const wchar_t *patterns = entry + std::wcslen(entry) + 1;
    const QStringList list = filterPatterns(QString::fromWCharArray(patterns));
    result.push_back(QString::fromWCharArray(entry) + QStringLiteral(" (") + list.join(QLatin1Char(' ')) +
                     QLatin1Char(')'));
    if (!*patterns)
      break;
    entry = patterns + std::wcslen(patterns) + 1;
  }
  return result;
}

DWORD CommDlgExtendedError() {
  return dialogError;
}

BOOL GetOpenFileName(LPOPENFILENAME ofn) {
  return runFileDialog(ofn, false);
}

BOOL GetSaveFileName(LPOPENFILENAME ofn) {
  return runFileDialog(ofn, true);
}

/* ---------------------------------------------------------------------
   Folder browser and COM
   --------------------------------------------------------------------- */

namespace {

class TaskAllocator : public IMalloc {
public:
  ULONG AddRef() override { return 1; }
  ULONG Release() override { return 1; }
  void *Alloc(SIZE_T size) override { return std::malloc(size); }
  // Frees item ID lists of SHBrowseForFolder and memory from Alloc.
  void Free(void *memory) override {
    if (!memory)
      return;
    const auto list = static_cast<ITEMIDLIST *>(memory);
    if (idLists.erase(list)) {
      delete list;
      return;
    }
    std::free(memory);
  }

  std::set<ITEMIDLIST *> idLists;
};

TaskAllocator &taskAllocator() {
  static TaskAllocator allocator;
  return allocator;
}

} // namespace

HRESULT CoInitializeEx(LPVOID /*reserved*/, DWORD /*coInit*/) {
  return S_OK;
}

HRESULT SHGetMalloc(LPMALLOC *malloc) {
  if (!malloc)
    return HRESULT(0x80070057); // E_INVALIDARG
  *malloc = &taskAllocator();
  return S_OK;
}

// Starts in the folder that pszDisplayName holds, if it exists: MeOS puts the
// current folder there. (Windows uses the buffer for output only.)
LPITEMIDLIST SHBrowseForFolder(BROWSEINFO *browseInfo) {
  if (!browseInfo)
    return nullptr;
  QFileDialog dialog(ownerWidget(browseInfo->hwndOwner));
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly);
  if (browseInfo->lpszTitle && *browseInfo->lpszTitle)
    dialog.setWindowTitle(fromWide(browseInfo->lpszTitle));
  if (browseInfo->pszDisplayName && *browseInfo->pszDisplayName) {
    const QString start = QString::fromStdString(meos_compat::nativePath(browseInfo->pszDisplayName));
    if (QFileInfo(start).isDir())
      dialog.setDirectory(start);
  }
  if (runModal([&] { return dialog.exec(); }) != QDialog::Accepted || dialog.selectedFiles().isEmpty())
    return nullptr;

  const QString path = QDir::cleanPath(dialog.selectedFiles().front());
  if (browseInfo->pszDisplayName) {
    QString name = QFileInfo(path).fileName();
    if (name.isEmpty())
      name = path;
    const std::wstring display = toWide(name.left(MAX_PATH - 1));
    std::wmemcpy(browseInfo->pszDisplayName, display.c_str(), display.size() + 1);
  }
  auto *list = new ITEMIDLIST{toWide(path)};
  taskAllocator().idLists.insert(list);
  return list;
}

BOOL SHGetPathFromIDList(LPCITEMIDLIST idList, LPWSTR path) {
  if (!idList || !path)
    return FALSE;
  if (idList->path.size() + 1 > MAX_PATH) {
    path[0] = 0;
    return FALSE;
  }
  std::wmemcpy(path, idList->path.c_str(), idList->path.size() + 1);
  return TRUE;
}

BOOL SHGetSpecialFolderPath(HWND /*owner*/, LPWSTR path, int folder, BOOL create) {
  if (!path)
    return FALSE;
  path[0] = 0;
  QStandardPaths::StandardLocation location;
  switch (folder) {
    case CSIDL_APPDATA:
      location = QStandardPaths::GenericDataLocation;
      break;
    case CSIDL_PERSONAL:
      location = QStandardPaths::DocumentsLocation;
      break;
    case CSIDL_DESKTOPDIRECTORY:
      location = QStandardPaths::DesktopLocation;
      break;
    default:
      return FALSE;
  }
  const QString folderPath = QStandardPaths::writableLocation(location);
  if (folderPath.isEmpty() || (!QDir(folderPath).exists() && !(create && QDir().mkpath(folderPath))))
    return FALSE;
  const std::wstring wide = QDir::cleanPath(folderPath).toStdWString();
  if (wide.size() + 1 > MAX_PATH)
    return FALSE;
  std::wmemcpy(path, wide.c_str(), wide.size() + 1);
  return TRUE;
}

/* ---------------------------------------------------------------------
   Colour dialog
   --------------------------------------------------------------------- */

BOOL ChooseColor(LPCHOOSECOLOR cc) {
  dialogError = 0;
  if (!cc || cc->lStructSize != sizeof(CHOOSECOLOR)) {
    dialogError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  constexpr int customCount = 16;
  if (cc->lpCustColors) {
    for (int i = 0; i < customCount; i++)
      QColorDialog::setCustomColor(i, meos_qt::toQColor(cc->lpCustColors[i]));
  }
  QColorDialog dialog(ownerWidget(cc->hwndOwner));
  dialog.setCurrentColor((cc->Flags & CC_RGBINIT) ? meos_qt::toQColor(cc->rgbResult) : QColor(Qt::black));
  const bool accepted = runModal([&] { return dialog.exec(); }) == QDialog::Accepted;
  // The dialog updates the custom colours the user has defined in any case.
  if (cc->lpCustColors) {
    for (int i = 0; i < customCount; i++) {
      const QColor color = QColorDialog::customColor(i);
      cc->lpCustColors[i] = RGB(color.red(), color.green(), color.blue());
    }
  }
  if (!accepted)
    return FALSE;
  const QColor color = dialog.selectedColor();
  cc->rgbResult = RGB(color.red(), color.green(), color.blue());
  return TRUE;
}

/* ---------------------------------------------------------------------
   Printing: no printer until stage 3
   --------------------------------------------------------------------- */

BOOL PrintDlg(LPPRINTDLG pd) {
  if (pd)
    pd->hDC = nullptr;
  dialogError = PDERR_NODEFAULTPRN;
  return FALSE;
}

BOOL PageSetupDlg(LPPAGESETUPDLG /*psd*/) {
  dialogError = PDERR_NODEFAULTPRN;
  return FALSE;
}

/* ---------------------------------------------------------------------
   Popup menus
   --------------------------------------------------------------------- */

namespace {

struct MenuItem {
  UINT flags;
  UINT_PTR id;
  std::wstring text;
};

std::map<HMENU, std::vector<MenuItem>> menus;
std::uintptr_t lastMenu = 0;

} // namespace

HMENU CreatePopupMenu() {
  const auto handle = reinterpret_cast<HMENU>(++lastMenu);
  menus[handle];
  return handle;
}

BOOL AppendMenu(HMENU menu, UINT flags, UINT_PTR idNewItem, LPCWSTR newItem) {
  const auto entry = menus.find(menu);
  if (entry == menus.end()) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (flags != MF_STRING && flags != MF_SEPARATOR) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  entry->second.push_back(MenuItem{flags, idNewItem, newItem && flags == MF_STRING ? newItem : L""});
  return TRUE;
}

BOOL DestroyMenu(HMENU menu) {
  if (!menus.erase(menu)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return TRUE;
}

// With TPM_RETURNCMD the chosen command (0 for none); otherwise the command is
// posted to the window as WM_COMMAND. "&x" marks the access key and a tab separates
// the shortcut text, in Qt as on Windows.
BOOL TrackPopupMenuEx(HMENU menu, UINT flags, int x, int y, HWND window, LPTPMPARAMS /*params*/) {
  const auto entry = menus.find(menu);
  if (entry == menus.end()) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  const std::shared_ptr<Window> owner = meos_qt::windowOrError(window);
  if (!owner || !meos_qt::isGuiThread())
    return FALSE;

  QMenu popup(owner->client ? owner->client.data() : owner->frame.data());
  std::map<QAction *, UINT_PTR> commands;
  for (const MenuItem &item : entry->second) {
    if (item.flags == MF_SEPARATOR)
      popup.addSeparator();
    else
      commands[popup.addAction(QString::fromStdWString(item.text))] = item.id;
  }
  const QAction *chosen = runModal([&] { return popup.exec(QPoint(x, y)); });
  const auto command = commands.find(const_cast<QAction *>(chosen));
  const UINT_PTR id = command == commands.end() ? 0 : command->second;
  if (flags & TPM_RETURNCMD)
    return BOOL(id);
  if (id)
    PostMessage(window, WM_COMMAND, WPARAM(id), 0);
  return TRUE;
}

/* ---------------------------------------------------------------------
   Shell
   --------------------------------------------------------------------- */

namespace {

constexpr std::intptr_t shellSuccess = 42;
constexpr std::intptr_t shellAccessDenied = 5; // SE_ERR_ACCESSDENIED
constexpr std::intptr_t shellNoAssociation = 31; // SE_ERR_NOASSOC

HINSTANCE shellResult(std::intptr_t value) {
  return reinterpret_cast<HINSTANCE>(value);
}

bool isUrl(const QString &file) {
  return file.contains(QLatin1String("://")) || file.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive);
}

} // namespace

// "open" (or no operation) of a URL, a folder, a document or a program. Programs
// start with the parameters, in the given working folder; URLs, folders and
// documents open with the desktop's default application.
HINSTANCE ShellExecute(HWND /*owner*/, LPCWSTR operation, LPCWSTR file, LPCWSTR parameters, LPCWSTR directory,
                       INT /*showCommand*/) {
  const QString verb = fromWide(operation);
  if (!verb.isEmpty() && verb.compare(QLatin1String("open"), Qt::CaseInsensitive) != 0 &&
      verb.compare(QLatin1String("explore"), Qt::CaseInsensitive) != 0)
    return shellResult(shellNoAssociation);
  if (!file || !*file)
    return shellResult(ERROR_FILE_NOT_FOUND);

  const QString target = fromWide(file);
  if (isUrl(target))
    return shellResult(QDesktopServices::openUrl(QUrl(target)) ? shellSuccess : shellNoAssociation);

  const QString workingDirectory =
      directory && *directory ? QString::fromStdString(meos_compat::nativePath(directory)) : QDir::currentPath();
  QFileInfo info(QString::fromStdString(meos_compat::nativePath(file)));
  if (info.isRelative())
    info = QFileInfo(QDir(workingDirectory), info.filePath());
  if (!info.exists())
    return shellResult(info.dir().exists() ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND);

  if (info.isFile() && info.isExecutable()) {
    const QStringList arguments = QProcess::splitCommand(fromWide(parameters));
    return shellResult(QProcess::startDetached(info.absoluteFilePath(), arguments, workingDirectory) ? shellSuccess
                                                                                                    : shellAccessDenied);
  }
  return shellResult(QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath())) ? shellSuccess
                                                                                              : shellNoAssociation);
}
