// Linux port: stand-in for the Windows SDK header <wininet.h>.
// The WinInet subset used by download.cpp is implemented in
// code/platform/posix/win32_inet.cpp. Until stage 2 (libcurl) it behaves like a
// computer without network access: sessions, connections and requests can be
// created, but connecting fails with ERROR_INTERNET_CANNOT_CONNECT.
#pragma once

#include "windows.h"

typedef LPVOID HINTERNET;
typedef WORD   INTERNET_PORT;

#define INTERNET_ERROR_BASE                    12000
#define ERROR_INTERNET_TIMEOUT                 (INTERNET_ERROR_BASE + 2)
#define ERROR_INTERNET_INVALID_URL             (INTERNET_ERROR_BASE + 5)
#define ERROR_INTERNET_UNRECOGNIZED_SCHEME     (INTERNET_ERROR_BASE + 6)
#define ERROR_INTERNET_INCORRECT_HANDLE_TYPE   (INTERNET_ERROR_BASE + 18)
#define ERROR_INTERNET_INCORRECT_HANDLE_STATE  (INTERNET_ERROR_BASE + 19)
#define ERROR_INTERNET_CANNOT_CONNECT          (INTERNET_ERROR_BASE + 29)
#define ERROR_INTERNET_CONNECTION_RESET        (INTERNET_ERROR_BASE + 31)
#define ERROR_INTERNET_FORCE_RETRY             (INTERNET_ERROR_BASE + 32)

#define INTERNET_OPEN_TYPE_PRECONFIG 0
#define INTERNET_OPEN_TYPE_DIRECT    1

#define INTERNET_DEFAULT_HTTP_PORT  80
#define INTERNET_DEFAULT_HTTPS_PORT 443

#define INTERNET_SERVICE_HTTP 3

#define INTERNET_OPTION_SEND_TIMEOUT    5
#define INTERNET_OPTION_RECEIVE_TIMEOUT 6

#define INTERNET_FLAG_RELOAD                   0x80000000
#define INTERNET_FLAG_NO_CACHE_WRITE           0x04000000
#define INTERNET_FLAG_DONT_CACHE               INTERNET_FLAG_NO_CACHE_WRITE
#define INTERNET_FLAG_SECURE                   0x00800000
#define INTERNET_FLAG_KEEP_CONNECTION          0x00400000
#define INTERNET_FLAG_IGNORE_CERT_DATE_INVALID 0x00002000
#define INTERNET_FLAG_IGNORE_CERT_CN_INVALID   0x00001000

#define ICU_ESCAPE 0x80000000

#define HTTP_QUERY_CONTENT_LENGTH 5
#define HTTP_QUERY_STATUS_CODE    19
#define HTTP_QUERY_FLAG_NUMBER    0x20000000

#define HTTP_STATUS_OK            200
#define HTTP_STATUS_BAD_REQUEST   400
#define HTTP_STATUS_DENIED        401
#define HTTP_STATUS_FORBIDDEN     403
#define HTTP_STATUS_NOT_FOUND     404
#define HTTP_STATUS_SERVER_ERROR  500
#define HTTP_STATUS_NOT_SUPPORTED 501

#define HTTP_VERSION L"HTTP/1.0"

typedef enum {
  INTERNET_SCHEME_PARTIAL = -2,
  INTERNET_SCHEME_UNKNOWN = -1,
  INTERNET_SCHEME_DEFAULT = 0,
  INTERNET_SCHEME_FTP,
  INTERNET_SCHEME_GOPHER,
  INTERNET_SCHEME_HTTP,
  INTERNET_SCHEME_HTTPS,
  INTERNET_SCHEME_FILE
} INTERNET_SCHEME;

typedef struct {
  DWORD           dwStructSize;
  LPWSTR          lpszScheme;
  DWORD           dwSchemeLength;
  INTERNET_SCHEME nScheme;
  LPWSTR          lpszHostName;
  DWORD           dwHostNameLength;
  INTERNET_PORT   nPort;
  LPWSTR          lpszUserName;
  DWORD           dwUserNameLength;
  LPWSTR          lpszPassword;
  DWORD           dwPasswordLength;
  LPWSTR          lpszUrlPath;
  DWORD           dwUrlPathLength;
  LPWSTR          lpszExtraInfo;
  DWORD           dwExtraInfoLength;
} URL_COMPONENTS, *LPURL_COMPONENTS;

typedef struct _INTERNET_BUFFERS {
  DWORD                     dwStructSize;
  struct _INTERNET_BUFFERS *Next;
  LPCWSTR                   lpcszHeader;
  DWORD                     dwHeadersLength;
  DWORD                     dwHeadersTotal;
  LPVOID                    lpvBuffer;
  DWORD                     dwBufferLength;
  DWORD                     dwBufferTotal;
  DWORD                     dwOffsetLow;
  DWORD                     dwOffsetHigh;
} INTERNET_BUFFERS, *LPINTERNET_BUFFERS;

HINTERNET InternetOpen(LPCWSTR agent, DWORD accessType, LPCWSTR proxy, LPCWSTR proxyBypass, DWORD flags);
BOOL InternetSetOption(HINTERNET internet, DWORD option, LPVOID buffer, DWORD bufferLength);
HINTERNET InternetOpenUrl(HINTERNET internet, LPCWSTR url, LPCWSTR headers, DWORD headersLength, DWORD flags,
                          DWORD_PTR context);
HINTERNET InternetConnect(HINTERNET internet, LPCWSTR serverName, INTERNET_PORT serverPort, LPCWSTR userName,
                          LPCWSTR password, DWORD service, DWORD flags, DWORD_PTR context);
BOOL InternetCloseHandle(HINTERNET internet);
BOOL InternetReadFile(HINTERNET file, LPVOID buffer, DWORD bytesToRead, LPDWORD bytesRead);
BOOL InternetWriteFile(HINTERNET file, LPCVOID buffer, DWORD bytesToWrite, LPDWORD bytesWritten);
BOOL InternetGetLastResponseInfo(LPDWORD error, LPWSTR buffer, LPDWORD bufferLength);
// Splits a URL (scheme://[user[:password]@]host[:port][/path][?extra]). Components
// with a buffer are copied (length in characters, including the terminating null, on
// input); components with a null buffer and a non-zero length get a pointer into url.
BOOL InternetCrackUrl(LPCWSTR url, DWORD urlLength, DWORD flags, LPURL_COMPONENTS components);
HINTERNET HttpOpenRequest(HINTERNET connect, LPCWSTR verb, LPCWSTR objectName, LPCWSTR version, LPCWSTR referrer,
                          LPCWSTR *acceptTypes, DWORD flags, DWORD_PTR context);
BOOL HttpSendRequestEx(HINTERNET request, LPINTERNET_BUFFERS buffersIn, LPINTERNET_BUFFERS buffersOut, DWORD flags,
                       DWORD_PTR context);
BOOL HttpEndRequest(HINTERNET request, LPINTERNET_BUFFERS buffersOut, DWORD flags, DWORD_PTR context);
BOOL HttpQueryInfo(HINTERNET request, DWORD infoLevel, LPVOID buffer, LPDWORD bufferLength, LPDWORD index);
