/************************************************************************
    MeOS - Orienteering Software
    Linux port: global memory blocks and the clipboard on Qt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// The formats set between OpenClipboard and CloseClipboard are collected and put on
// the Qt clipboard as one QMimeData when the clipboard is closed:
//   CF_UNICODETEXT, CF_TEXT    -> text/plain
//   "HTML Format" (CF_HTML)    -> text/html (the CF_HTML header removed)
//   other registered formats   -> application/x-qt-windows-mime;value="<name>"
// GetClipboardData converts back and synthesizes CF_TEXT from CF_UNICODETEXT and
// vice versa, as Windows does. The clipboard functions run in the GUI thread.

#include "win32_ui.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include <map>
#include <unordered_map>

namespace {

/* ---------------------------------------------------------------------
   Global memory blocks
   --------------------------------------------------------------------- */

constexpr DWORD errorNotLocked = 158; // ERROR_NOT_LOCKED

struct GlobalBlock {
  std::unique_ptr<char[]> memory;
  SIZE_T size = 0;
  bool moveable = false;
  int locks = 0;
};

// A fixed block is identified by its address, a moveable block by the address of
// its bookkeeping, which is never the address of block memory.
std::mutex globalMutex;
std::unordered_map<HGLOBAL, std::unique_ptr<GlobalBlock>> globalBlocks;

/* ---------------------------------------------------------------------
   Clipboard
   --------------------------------------------------------------------- */

constexpr UINT firstRegisteredFormat = 0xC000;

struct RegisteredFormat {
  UINT id;
  std::wstring name;
};
// By lower-case name; format names are case insensitive.
std::map<std::wstring, RegisteredFormat> registeredFormats;

struct ClipboardState {
  bool open = false;
  bool emptied = false;
  // Data set since OpenClipboard, by format.
  std::map<UINT, QByteArray> pending;
  // Blocks returned by GetClipboardData. The clipboard owns them; they stay valid
  // until the clipboard is opened again.
  std::vector<HGLOBAL> returned;
};
ClipboardState clipboard;

std::wstring lowerCase(const std::wstring &text) {
  std::wstring lower(text);
  for (wchar_t &ch : lower)
    ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));
  return lower;
}

std::wstring formatName(UINT format) {
  for (const auto &entry : registeredFormats) {
    if (entry.second.id == format)
      return entry.second.name;
  }
  return std::wstring();
}

bool isHtmlFormat(UINT format) {
  const auto entry = registeredFormats.find(L"html format");
  return entry != registeredFormats.end() && entry->second.id == format;
}

QString windowsMimeType(const std::wstring &name) {
  return QStringLiteral("application/x-qt-windows-mime;value=\"%1\"").arg(QString::fromStdWString(name));
}

bool checkOpen() {
  if (!meos_qt::isGuiThread()) {
    SetLastError(ERROR_ACCESS_DENIED);
    return false;
  }
  if (!clipboard.open) {
    SetLastError(ERROR_CLIPBOARD_NOT_OPEN);
    return false;
  }
  return true;
}

void freeReturnedBlocks() {
  for (HGLOBAL block : clipboard.returned)
    GlobalFree(block);
  clipboard.returned.clear();
}

// Text of a CF_UNICODETEXT block: wchar_t units up to a null or the end of the block
// (MeOS copies text without the terminating null).
QString unicodeText(const QByteArray &data) {
  const std::size_t units = std::size_t(data.size()) / sizeof(wchar_t);
  std::wstring text(units, L'\0');
  if (units)
    std::memcpy(&text[0], data.constData(), units * sizeof(wchar_t));
  const std::size_t end = text.find(L'\0');
  if (end != std::wstring::npos)
    text.resize(end);
  return QString::fromStdWString(text);
}

// Text of a CF_TEXT block in the ANSI code page.
QString ansiText(const QByteArray &data) {
  const int length = int(qstrnlen(data.constData(), uint(data.size())));
  if (length == 0)
    return QString();
  const int size = MultiByteToWideChar(CP_ACP, 0, data.constData(), length, nullptr, 0);
  std::wstring text(std::size_t(std::max(size, 0)), L'\0');
  if (size > 0)
    MultiByteToWideChar(CP_ACP, 0, data.constData(), length, &text[0], size);
  return QString::fromStdWString(text);
}

// The value of a "Name:number" line of a CF_HTML header, or -1.
int htmlHeaderOffset(const QByteArray &data, const char *name) {
  const QByteArray key = QByteArray(name) + ':';
  const int start = data.indexOf(key);
  if (start < 0)
    return -1;
  int position = start + key.size();
  int value = 0;
  bool digits = false;
  while (position < data.size() && data[position] >= '0' && data[position] <= '9') {
    value = value * 10 + (data[position] - '0');
    digits = true;
    position++;
  }
  return digits ? value : -1;
}

// The HTML of a CF_HTML block: from StartHTML to EndHTML, or the fragment if those
// are missing (as Qt's Windows clipboard does). The offsets count bytes of the block.
QString htmlFromCfHtml(const QByteArray &data) {
  int start = htmlHeaderOffset(data, "StartHTML");
  int end = htmlHeaderOffset(data, "EndHTML");
  if (start < 0 || end < 0) {
    start = htmlHeaderOffset(data, "StartFragment");
    end = htmlHeaderOffset(data, "EndFragment");
  }
  if (start < 0 || start > data.size())
    return QString();
  if (end < start || end > data.size())
    end = data.size();
  QByteArray html = data.mid(start, end - start);
  while (html.endsWith('\0'))
    html.chop(1);
  return QString::fromUtf8(html);
}

// A CF_HTML block for HTML: the header, the HTML in UTF-8 and a terminating null.
// The fragment lies between the StartFragment/EndFragment comments, if present.
QByteArray cfHtmlFromHtml(const QString &html) {
  const QByteArray body = html.toUtf8();
  int fragmentStart = 0;
  int fragmentEnd = body.size();
  const QByteArray startMarker("<!--StartFragment-->");
  const QByteArray endMarker("<!--EndFragment-->");
  const int startComment = body.indexOf(startMarker);
  const int endComment = body.lastIndexOf(endMarker);
  if (startComment >= 0 && endComment >= startComment + startMarker.size()) {
    fragmentStart = startComment + startMarker.size();
    fragmentEnd = endComment;
  }
  const auto header = [&](int offset) {
    return QByteArray("Version:0.9\r\n") + "StartHTML:" + QByteArray::number(offset).rightJustified(10, '0') +
           "\r\nEndHTML:" + QByteArray::number(offset + body.size()).rightJustified(10, '0') +
           "\r\nStartFragment:" + QByteArray::number(offset + fragmentStart).rightJustified(10, '0') +
           "\r\nEndFragment:" + QByteArray::number(offset + fragmentEnd).rightJustified(10, '0') + "\r\n";
  };
  const int offset = int(header(0).size());
  return header(offset) + body + '\0';
}

// Puts one Windows format into mime data.
void addToMime(QMimeData &mime, UINT format, const QByteArray &data) {
  if (format == CF_UNICODETEXT)
    mime.setText(unicodeText(data));
  else if (format == CF_TEXT)
    mime.setText(ansiText(data));
  else if (isHtmlFormat(format))
    mime.setHtml(htmlFromCfHtml(data));
  else if (format >= firstRegisteredFormat && !formatName(format).empty())
    mime.setData(windowsMimeType(formatName(format)), data);
}

// The pending formats as mime data. CF_UNICODETEXT wins over CF_TEXT.
void addPending(QMimeData &mime) {
  for (const auto &entry : clipboard.pending) {
    if (entry.first == CF_TEXT && clipboard.pending.count(CF_UNICODETEXT))
      continue;
    addToMime(mime, entry.first, entry.second);
  }
}

// The data of a Windows format from mime data; empty if it is not available.
QByteArray formatFromMime(const QMimeData &mime, UINT format, bool &available) {
  available = false;
  if (format == CF_UNICODETEXT || format == CF_TEXT) {
    if (!mime.hasText())
      return QByteArray();
    available = true;
    const std::wstring text = mime.text().toStdWString();
    if (format == CF_UNICODETEXT)
      return QByteArray(reinterpret_cast<const char *>(text.c_str()), int((text.size() + 1) * sizeof(wchar_t)));
    const int size = WideCharToMultiByte(CP_ACP, 0, text.c_str(), int(text.size()) + 1, nullptr, 0, nullptr, nullptr);
    QByteArray ansi(std::max(size, 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), int(text.size()) + 1, ansi.data(), size, nullptr, nullptr);
    return ansi;
  }
  if (isHtmlFormat(format)) {
    if (!mime.hasHtml())
      return QByteArray();
    available = true;
    return cfHtmlFromHtml(mime.html());
  }
  const std::wstring name = formatName(format);
  if (!name.empty() && mime.hasFormat(windowsMimeType(name))) {
    available = true;
    return mime.data(windowsMimeType(name));
  }
  return QByteArray();
}

HGLOBAL allocateBlock(const QByteArray &data) {
  const HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, SIZE_T(data.size()));
  if (!block)
    return nullptr;
  if (const auto memory = static_cast<char *>(GlobalLock(block))) {
    std::memcpy(memory, data.constData(), std::size_t(data.size()));
    GlobalUnlock(block);
  }
  return block;
}

} // namespace

/* ---------------------------------------------------------------------
   Global memory blocks
   --------------------------------------------------------------------- */

HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes) {
  auto block = std::make_unique<GlobalBlock>();
  // Always zeroed, as with GMEM_ZEROINIT; the memory is never empty so that fixed
  // blocks have distinct addresses.
  block->memory.reset(new (std::nothrow) char[std::max<SIZE_T>(bytes, 1)]());
  if (!block->memory) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  block->size = bytes;
  block->moveable = (flags & GMEM_MOVEABLE) != 0;
  const HGLOBAL handle = block->moveable ? static_cast<HGLOBAL>(block.get()) : static_cast<HGLOBAL>(block->memory.get());
  std::lock_guard<std::mutex> lock(globalMutex);
  globalBlocks[handle] = std::move(block);
  return handle;
}

LPVOID GlobalLock(HGLOBAL memory) {
  std::lock_guard<std::mutex> lock(globalMutex);
  const auto entry = globalBlocks.find(memory);
  if (entry == globalBlocks.end()) {
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  GlobalBlock &block = *entry->second;
  if (block.moveable)
    block.locks++;
  return block.memory.get();
}

// Returns TRUE while the block stays locked; FALSE with ERROR_SUCCESS when the last
// lock is released.
BOOL GlobalUnlock(HGLOBAL memory) {
  std::lock_guard<std::mutex> lock(globalMutex);
  const auto entry = globalBlocks.find(memory);
  if (entry == globalBlocks.end()) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  GlobalBlock &block = *entry->second;
  if (!block.moveable)
    return TRUE;
  if (block.locks == 0) {
    SetLastError(errorNotLocked);
    return FALSE;
  }
  if (--block.locks > 0)
    return TRUE;
  SetLastError(ERROR_SUCCESS);
  return FALSE;
}

SIZE_T GlobalSize(HGLOBAL memory) {
  std::lock_guard<std::mutex> lock(globalMutex);
  const auto entry = globalBlocks.find(memory);
  if (entry == globalBlocks.end()) {
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
  }
  return entry->second->size;
}

// Returns nullptr on success and the handle on failure.
HGLOBAL GlobalFree(HGLOBAL memory) {
  std::lock_guard<std::mutex> lock(globalMutex);
  if (!globalBlocks.erase(memory)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return memory;
  }
  return nullptr;
}

/* ---------------------------------------------------------------------
   Clipboard
   --------------------------------------------------------------------- */

BOOL OpenClipboard(HWND /*owner*/) {
  if (!meos_qt::isGuiThread()) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  if (clipboard.open)
    return TRUE; // Already open in this thread.
  freeReturnedBlocks();
  clipboard.open = true;
  clipboard.emptied = false;
  clipboard.pending.clear();
  return TRUE;
}

BOOL CloseClipboard() {
  if (!checkOpen())
    return FALSE;
  clipboard.open = false;
  if (!clipboard.emptied && clipboard.pending.empty())
    return TRUE;

  auto mime = std::make_unique<QMimeData>();
  // Without EmptyClipboard the other formats stay.
  if (!clipboard.emptied) {
    if (const QMimeData *current = QGuiApplication::clipboard()->mimeData()) {
      for (const QString &type : current->formats())
        mime->setData(type, current->data(type));
    }
  }
  addPending(*mime);
  clipboard.pending.clear();
  clipboard.emptied = false;
  QGuiApplication::clipboard()->setMimeData(mime.release());
  return TRUE;
}

BOOL EmptyClipboard() {
  if (!checkOpen())
    return FALSE;
  clipboard.emptied = true;
  clipboard.pending.clear();
  return TRUE;
}

// The clipboard takes the data at once and frees the block, which the application
// must not use after a successful call.
HANDLE SetClipboardData(UINT format, HANDLE memory) {
  if (!checkOpen())
    return nullptr;
  if (!memory) {
    // Delayed rendering (WM_RENDERFORMAT) is not supported.
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  const SIZE_T size = GlobalSize(memory);
  const auto data = static_cast<const char *>(GlobalLock(memory));
  if (!data)
    return nullptr;
  clipboard.pending[format] = QByteArray(data, int(size));
  GlobalUnlock(memory);
  GlobalFree(memory);
  return memory;
}

HANDLE GetClipboardData(UINT format) {
  if (!checkOpen())
    return nullptr;

  QMimeData pendingMime;
  addPending(pendingMime);
  bool available = false;
  QByteArray data;
  // Exactly the data that was set, then conversions of it, then the clipboard.
  const auto exact = clipboard.pending.find(format);
  if (exact != clipboard.pending.end()) {
    data = exact->second;
    available = true;
  }
  if (!available)
    data = formatFromMime(pendingMime, format, available);
  if (!available && !clipboard.emptied) {
    if (const QMimeData *current = QGuiApplication::clipboard()->mimeData())
      data = formatFromMime(*current, format, available);
  }
  if (!available)
    return nullptr;

  const HGLOBAL block = allocateBlock(data);
  if (block)
    clipboard.returned.push_back(block);
  return block;
}

UINT RegisterClipboardFormat(LPCWSTR formatName) {
  if (!formatName || !*formatName) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  const std::wstring key = lowerCase(formatName);
  const auto entry = registeredFormats.find(key);
  if (entry != registeredFormats.end())
    return entry->second.id;
  const UINT id = firstRegisteredFormat + UINT(registeredFormats.size());
  registeredFormats[key] = RegisteredFormat{id, formatName};
  return id;
}
