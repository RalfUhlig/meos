/************************************************************************
    MeOS - Orienteering Software
    Linux port: embedded resources (FindResource, LoadResource, LoadBitmap,
    LoadIcon).

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// The resources of meos.rc and meoslang.rc are compiled in as Qt resources by
// cmake/Win32Resources.cmake (target meos_resources), under
// ":/meos/resources/<type>/<name>". A program without that target finds no
// resources, as a Windows program without a resource section.

#include "win32_gdi.h"

#include <QFileInfo>
#include <QIcon>
#include <QResource>

#include <map>

namespace {

// A found resource. HRSRC points to it; entries live until the program ends, as
// the resource section of a module does.
struct ResourceEntry {
  // The resource data: the embedded file, without the BITMAPFILEHEADER of a bitmap.
  const unsigned char *data = nullptr;
  DWORD size = 0;
  // The whole embedded file.
  const unsigned char *fileData = nullptr;
  qint64 fileSize = 0;
  // Owns the data of a compressed Qt resource.
  QByteArray uncompressed;
};

std::mutex resourceMutex;
std::map<QString, std::unique_ptr<ResourceEntry>> resourceEntries;

constexpr std::uintptr_t bitmapType = 2; // RT_BITMAP
constexpr std::uintptr_t iconType = 14;  // RT_GROUP_ICON: the whole .ico file here
constexpr int bitmapFileHeaderSize = 14;

// Icons of LoadIcon. As on Windows they live until the program ends; MeOS loads
// them once for its window classes.
std::map<HICON, QIcon> icons;
std::uintptr_t lastIcon = 0;

// A resource name or type as the resource compiler stores it: a number, "#<number>"
// or a name in upper case. Empty for an invalid name.
QString resourceKey(LPCWSTR name) {
  if (!name)
    return QString();
  if (IS_INTRESOURCE(name))
    return QString::number(reinterpret_cast<std::uintptr_t>(name));
  if (name[0] == L'#') {
    std::uintptr_t number = 0;
    const wchar_t *digit = name + 1;
    for (; *digit >= L'0' && *digit <= L'9'; digit++)
      number = number * 10 + std::uintptr_t(*digit - L'0');
    return *digit == 0 && digit != name + 1 && number <= 0xFFFF ? QString::number(number) : QString();
  }
  return QString::fromWCharArray(name).toUpper();
}

bool isApplicationModule(HMODULE module) {
  return !module || module == meos_qt::applicationInstance();
}

// Finds a resource, or returns nullptr with the error Windows sets.
ResourceEntry *findEntry(HMODULE module, LPCWSTR name, LPCWSTR type) {
  if (!isApplicationModule(module)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  const QString typeKey = resourceKey(type);
  const QString nameKey = resourceKey(name);
  if (typeKey.isEmpty()) {
    SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
    return nullptr;
  }
  if (nameKey.isEmpty()) {
    SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return nullptr;
  }

  const QString path = QStringLiteral(":/meos/resources/") + typeKey + QLatin1Char('/') + nameKey;
  std::lock_guard<std::mutex> lock(resourceMutex);
  const auto cached = resourceEntries.find(path);
  if (cached != resourceEntries.end())
    return cached->second.get();

  const QResource resource(path);
  if (!resource.isValid() || QFileInfo(path).isDir()) {
    const bool typeExists = QFileInfo(QStringLiteral(":/meos/resources/") + typeKey).isDir();
    SetLastError(typeExists ? ERROR_RESOURCE_NAME_NOT_FOUND : ERROR_RESOURCE_TYPE_NOT_FOUND);
    return nullptr;
  }

  auto entry = std::make_unique<ResourceEntry>();
  if (resource.compressionAlgorithm() == QResource::NoCompression) {
    entry->fileData = resource.data();
    entry->fileSize = resource.size();
  }
  else {
    entry->uncompressed = resource.uncompressedData();
    entry->fileData = reinterpret_cast<const unsigned char *>(entry->uncompressed.constData());
    entry->fileSize = entry->uncompressed.size();
  }
  entry->data = entry->fileData;
  entry->size = DWORD(entry->fileSize);
  // The resource compiler stores a bitmap without its file header.
  if (typeKey == QString::number(bitmapType) && entry->fileSize >= bitmapFileHeaderSize &&
      entry->fileData[0] == 'B' && entry->fileData[1] == 'M') {
    entry->data += bitmapFileHeaderSize;
    entry->size -= bitmapFileHeaderSize;
  }
  ResourceEntry *result = entry.get();
  resourceEntries[path] = std::move(entry);
  return result;
}

} // namespace

QImage meos_qt::bitmapResource(HINSTANCE instance, LPCWSTR name) {
  const ResourceEntry *entry = findEntry(instance, name, MAKEINTRESOURCE(bitmapType));
  if (!entry)
    return QImage();
  return QImage::fromData(entry->fileData, int(entry->fileSize), "BMP");
}

QIcon meos_qt::iconImage(HICON icon) {
  const auto entry = icons.find(icon);
  return entry == icons.end() ? QIcon() : entry->second;
}

HMODULE GetModuleHandle(LPCWSTR moduleName) {
  if (!moduleName)
    return meos_qt::applicationInstance();
  SetLastError(ERROR_MOD_NOT_FOUND);
  return nullptr;
}

HRSRC FindResource(HMODULE module, LPCWSTR name, LPCWSTR type) {
  return reinterpret_cast<HRSRC>(findEntry(module, name, type));
}

// The handle of a loaded resource is the address of its data, as on 32- and 64-bit
// Windows; LockResource returns it.
HGLOBAL LoadResource(HMODULE module, HRSRC resource) {
  if (!resource || !isApplicationModule(module)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  return const_cast<void *>(static_cast<const void *>(reinterpret_cast<const ResourceEntry *>(resource)->data));
}

LPVOID LockResource(HGLOBAL data) {
  return data;
}

DWORD SizeofResource(HMODULE module, HRSRC resource) {
  if (!resource || !isApplicationModule(module)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
  }
  return reinterpret_cast<const ResourceEntry *>(resource)->size;
}

// The icon of a window class. cmake/Win32Resources.cmake embeds the .ico file
// itself under the type of a group icon, so Qt reads the file; Windows reads the
// group and its images. MeOS asks for the icon of its resources only.
HICON LoadIcon(HINSTANCE instance, LPCWSTR iconName) {
  const ResourceEntry *entry = findEntry(instance, iconName, MAKEINTRESOURCE(iconType));
  if (!entry)
    return nullptr;
  QPixmap pixmap;
  if (!pixmap.loadFromData(entry->fileData, uint(entry->fileSize), "ICO")) {
    SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return nullptr;
  }
  const auto handle = reinterpret_cast<HICON>(++lastIcon);
  icons[handle] = QIcon(pixmap);
  return handle;
}

// A device-dependent bitmap in the colour format of the screen, as on Windows.
HBITMAP LoadBitmap(HINSTANCE instance, LPCWSTR bitmapName) {
  const QImage image = meos_qt::bitmapResource(instance, bitmapName);
  if (image.isNull())
    return nullptr;
  return meos_qt::createBitmap(image.convertToFormat(QImage::Format_RGB32));
}
