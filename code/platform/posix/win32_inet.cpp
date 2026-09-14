/************************************************************************
    MeOS - Orienteering Software
    Linux port: the WinInet subset of download.cpp.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Until stage 2 ports HTTP to libcurl, this behaves like Windows on a computer without
// network access: InternetOpen, InternetConnect and HttpOpenRequest succeed (WinInet
// connects lazily), every attempt to reach a server fails with
// ERROR_INTERNET_CANNOT_CONNECT, and a request without a response is in the wrong
// state for reading and querying. InternetCrackUrl is complete.

#include "win32_handle.h"

#include "wininet.h"

namespace {

enum class InternetKind { Session, Connection, Request };

class InternetObject : public meos_platform::Win32Object {
public:
  explicit InternetObject(InternetKind kind) : kind(kind) {}
  const InternetKind kind;
};

HINTERNET createInternetHandle(InternetKind kind) {
  return meos_platform::registerObject(std::make_shared<InternetObject>(kind));
}

std::shared_ptr<InternetObject> internetObject(HINTERNET handle) {
  return meos_platform::fromHandle<InternetObject>(handle);
}

// Checks the handle type; sets the Windows error codes otherwise.
bool checkHandle(HINTERNET handle, InternetKind kind) {
  const std::shared_ptr<InternetObject> object = internetObject(handle);
  if (!object) {
    SetLastError(ERROR_INVALID_HANDLE);
    return false;
  }
  if (object->kind != kind) {
    SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
    return false;
  }
  return true;
}

// Copies a URL component into the caller's buffer or points into the URL, as
// InternetCrackUrl does.
bool storeComponent(LPCWSTR urlText, const std::wstring &url, std::size_t start, std::size_t length, bool unescape,
                    LPWSTR *buffer, DWORD *bufferLength) {
  if (!buffer || !bufferLength)
    return true;
  if (!*buffer) {
    if (*bufferLength != 0) {
      *buffer = const_cast<LPWSTR>(urlText + start);
      *bufferLength = static_cast<DWORD>(length);
    }
    return true;
  }
  if (*bufferLength == 0)
    return true;

  std::wstring text = url.substr(start, length);
  if (unescape) {
    std::wstring decoded;
    for (std::size_t k = 0; k < text.size(); k++) {
      if (text[k] == L'%' && k + 2 < text.size() && std::iswxdigit(text[k + 1]) && std::iswxdigit(text[k + 2])) {
        decoded.push_back(static_cast<wchar_t>(std::wcstol(text.substr(k + 1, 2).c_str(), nullptr, 16)));
        k += 2;
      }
      else
        decoded.push_back(text[k]);
    }
    text = decoded;
  }
  if (*bufferLength < text.size() + 1) {
    *bufferLength = static_cast<DWORD>(text.size() + 1);
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return false;
  }
  std::wmemcpy(*buffer, text.c_str(), text.size() + 1);
  *bufferLength = static_cast<DWORD>(text.size());
  return true;
}

} // namespace

HINTERNET InternetOpen(LPCWSTR /*agent*/, DWORD /*accessType*/, LPCWSTR /*proxy*/, LPCWSTR /*proxyBypass*/,
                       DWORD /*flags*/) {
  return createInternetHandle(InternetKind::Session);
}

BOOL InternetSetOption(HINTERNET internet, DWORD option, LPVOID buffer, DWORD bufferLength) {
  if (!internetObject(internet)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if ((option == INTERNET_OPTION_SEND_TIMEOUT || option == INTERNET_OPTION_RECEIVE_TIMEOUT) &&
      (!buffer || bufferLength < sizeof(DWORD))) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  return TRUE;
}

HINTERNET InternetOpenUrl(HINTERNET internet, LPCWSTR url, LPCWSTR /*headers*/, DWORD /*headersLength*/,
                          DWORD /*flags*/, DWORD_PTR /*context*/) {
  if (!checkHandle(internet, InternetKind::Session))
    return nullptr;
  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  if (!url || !InternetCrackUrl(url, 0, 0, &components))
    return nullptr;
  SetLastError(ERROR_INTERNET_CANNOT_CONNECT);
  return nullptr;
}

HINTERNET InternetConnect(HINTERNET internet, LPCWSTR serverName, INTERNET_PORT /*serverPort*/,
                          LPCWSTR /*userName*/, LPCWSTR /*password*/, DWORD service, DWORD /*flags*/,
                          DWORD_PTR /*context*/) {
  if (!checkHandle(internet, InternetKind::Session))
    return nullptr;
  if (!serverName || service != INTERNET_SERVICE_HTTP) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  return createInternetHandle(InternetKind::Connection);
}

HINTERNET HttpOpenRequest(HINTERNET connect, LPCWSTR /*verb*/, LPCWSTR /*objectName*/, LPCWSTR /*version*/,
                          LPCWSTR /*referrer*/, LPCWSTR * /*acceptTypes*/, DWORD /*flags*/, DWORD_PTR /*context*/) {
  if (!checkHandle(connect, InternetKind::Connection))
    return nullptr;
  return createInternetHandle(InternetKind::Request);
}

BOOL HttpSendRequestEx(HINTERNET request, LPINTERNET_BUFFERS /*buffersIn*/, LPINTERNET_BUFFERS /*buffersOut*/,
                       DWORD /*flags*/, DWORD_PTR /*context*/) {
  if (checkHandle(request, InternetKind::Request))
    SetLastError(ERROR_INTERNET_CANNOT_CONNECT);
  return FALSE;
}

// A request that has not been sent cannot be written, ended, read or queried.
BOOL HttpEndRequest(HINTERNET request, LPINTERNET_BUFFERS /*buffersOut*/, DWORD /*flags*/, DWORD_PTR /*context*/) {
  if (checkHandle(request, InternetKind::Request))
    SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
  return FALSE;
}

BOOL HttpQueryInfo(HINTERNET request, DWORD /*infoLevel*/, LPVOID /*buffer*/, LPDWORD /*bufferLength*/,
                   LPDWORD /*index*/) {
  if (checkHandle(request, InternetKind::Request))
    SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
  return FALSE;
}

BOOL InternetReadFile(HINTERNET file, LPVOID /*buffer*/, DWORD /*bytesToRead*/, LPDWORD bytesRead) {
  if (bytesRead)
    *bytesRead = 0;
  if (checkHandle(file, InternetKind::Request))
    SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
  return FALSE;
}

BOOL InternetWriteFile(HINTERNET file, LPCVOID /*buffer*/, DWORD /*bytesToWrite*/, LPDWORD bytesWritten) {
  if (bytesWritten)
    *bytesWritten = 0;
  if (checkHandle(file, InternetKind::Request))
    SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
  return FALSE;
}

BOOL InternetCloseHandle(HINTERNET internet) {
  if (!internetObject(internet) || !meos_platform::unregisterObject(internet)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return TRUE;
}

// No server has answered, so there is no response text.
BOOL InternetGetLastResponseInfo(LPDWORD error, LPWSTR buffer, LPDWORD bufferLength) {
  if (!error || !bufferLength) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *error = 0;
  if (buffer && *bufferLength > 0)
    buffer[0] = 0;
  *bufferLength = 0;
  return TRUE;
}

BOOL InternetCrackUrl(LPCWSTR urlText, DWORD urlLength, DWORD flags, LPURL_COMPONENTS components) {
  if (!urlText || !components || components->dwStructSize != sizeof(URL_COMPONENTS)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const std::wstring url = urlLength == 0 ? std::wstring(urlText) : std::wstring(urlText, urlLength);

  const std::size_t schemeEnd = url.find(L"://");
  if (schemeEnd == std::wstring::npos || schemeEnd == 0) {
    SetLastError(ERROR_INTERNET_UNRECOGNIZED_SCHEME);
    return FALSE;
  }
  std::wstring scheme = url.substr(0, schemeEnd);
  for (wchar_t &ch : scheme)
    ch = static_cast<wchar_t>(std::towlower(ch));
  INTERNET_PORT defaultPort = 0;
  if (scheme == L"http") {
    components->nScheme = INTERNET_SCHEME_HTTP;
    defaultPort = INTERNET_DEFAULT_HTTP_PORT;
  }
  else if (scheme == L"https") {
    components->nScheme = INTERNET_SCHEME_HTTPS;
    defaultPort = INTERNET_DEFAULT_HTTPS_PORT;
  }
  else if (scheme == L"ftp") {
    components->nScheme = INTERNET_SCHEME_FTP;
    defaultPort = 21;
  }
  else if (scheme == L"file")
    components->nScheme = INTERNET_SCHEME_FILE;
  else {
    components->nScheme = INTERNET_SCHEME_UNKNOWN;
    SetLastError(ERROR_INTERNET_UNRECOGNIZED_SCHEME);
    return FALSE;
  }

  const std::size_t authorityStart = schemeEnd + 3;
  std::size_t authorityEnd = url.find_first_of(L"/?#", authorityStart);
  if (authorityEnd == std::wstring::npos)
    authorityEnd = url.size();

  std::size_t hostStart = authorityStart;
  std::size_t userStart = 0, userLength = 0, passwordStart = 0, passwordLength = 0;
  const std::size_t at = url.rfind(L'@', authorityEnd == 0 ? 0 : authorityEnd - 1);
  if (at != std::wstring::npos && at >= authorityStart) {
    const std::size_t colon = url.find(L':', authorityStart);
    userStart = authorityStart;
    if (colon != std::wstring::npos && colon < at) {
      userLength = colon - authorityStart;
      passwordStart = colon + 1;
      passwordLength = at - colon - 1;
    }
    else
      userLength = at - authorityStart;
    hostStart = at + 1;
  }

  std::size_t hostLength = authorityEnd - hostStart;
  components->nPort = defaultPort;
  const std::size_t portColon = url.find(L':', hostStart);
  if (portColon != std::wstring::npos && portColon < authorityEnd) {
    hostLength = portColon - hostStart;
    const std::wstring port = url.substr(portColon + 1, authorityEnd - portColon - 1);
    if (port.empty() || port.find_first_not_of(L"0123456789") != std::wstring::npos || port.size() > 5 ||
        std::wcstol(port.c_str(), nullptr, 10) > 65535) {
      SetLastError(ERROR_INTERNET_INVALID_URL);
      return FALSE;
    }
    components->nPort = static_cast<INTERNET_PORT>(std::wcstol(port.c_str(), nullptr, 10));
  }

  std::size_t extraStart = url.find_first_of(L"?#", authorityEnd);
  if (extraStart == std::wstring::npos)
    extraStart = url.size();

  const bool unescape = (flags & ICU_ESCAPE) != 0;
  return storeComponent(urlText, url, 0, schemeEnd, false, &components->lpszScheme, &components->dwSchemeLength) &&
         storeComponent(urlText, url, hostStart, hostLength, false, &components->lpszHostName,
                        &components->dwHostNameLength) &&
         storeComponent(urlText, url, userStart, userLength, false, &components->lpszUserName,
                        &components->dwUserNameLength) &&
         storeComponent(urlText, url, passwordStart, passwordLength, false, &components->lpszPassword,
                        &components->dwPasswordLength) &&
         storeComponent(urlText, url, authorityEnd, extraStart - authorityEnd, unescape, &components->lpszUrlPath,
                        &components->dwUrlPathLength) &&
         storeComponent(urlText, url, extraStart, url.size() - extraStart, unescape, &components->lpszExtraInfo,
                        &components->dwExtraInfoLength)
             ? TRUE
             : FALSE;
}
