/************************************************************************
    MeOS - Orienteering Software
    Linux port: stand-in for the Windows SDK header <windows.h>.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Only on the include path for non-Windows builds. Declares the Win32 types,
// macros and structures that MeOS headers use, so that they parse unchanged.
//
// Functions in the sections for strings, date and time, and the file system are
// implemented in code/platform/posix. Functions in sections marked "stage 1" are
// declared so that the portable code compiles; they are implemented together with
// the Qt backend.

#pragma once

#include "../msvc_compat.h"

/* Handles: distinct opaque pointer types, as with STRICT on Windows. */
#define MEOS_DECLARE_HANDLE(name) struct name##__; typedef struct name##__ *name
MEOS_DECLARE_HANDLE(HWND);
MEOS_DECLARE_HANDLE(HDC);
MEOS_DECLARE_HANDLE(HFONT);
MEOS_DECLARE_HANDLE(HBITMAP);
MEOS_DECLARE_HANDLE(HBRUSH);
MEOS_DECLARE_HANDLE(HPEN);
MEOS_DECLARE_HANDLE(HRGN);
MEOS_DECLARE_HANDLE(HINSTANCE);
MEOS_DECLARE_HANDLE(HMENU);
MEOS_DECLARE_HANDLE(HICON);
MEOS_DECLARE_HANDLE(HCURSOR);
MEOS_DECLARE_HANDLE(HRSRC);

typedef void     *HANDLE;
typedef HANDLE    HGLOBAL;
typedef HANDLE    HLOCAL;
typedef HANDLE    HGDIOBJ;
typedef HINSTANCE HMODULE;

#define INVALID_HANDLE_VALUE ((HANDLE)(std::intptr_t)-1)

typedef std::uintptr_t WPARAM;
typedef std::intptr_t  LPARAM;
typedef std::intptr_t  LRESULT;
typedef DWORD          COLORREF;
typedef DWORD          LCID;
typedef std::uint16_t  ATOM;
typedef BYTE          *LPBYTE;
typedef BOOL          *LPBOOL;

typedef wchar_t        TCHAR;
typedef wchar_t       *LPTSTR;
typedef const wchar_t *LPCTSTR;

/* Calling conventions have no meaning on x86-64 Linux. */
#define CALLBACK
#define WINAPI
#define APIENTRY

typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define MAX_PATH 260
#define MAX_COMPUTERNAME_LENGTH 15
#define TEXT(s) L##s

#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb) ((BYTE)(rgb))
#define GetGValue(rgb) ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) ((BYTE)((rgb) >> 16))

#define LANG_NEUTRAL    0x00
#define SUBLANG_DEFAULT 0x01
#define MAKELANGID(primary, sub) ((((WORD)(sub)) << 10) | (WORD)(primary))

#define ZeroMemory(dst, len) std::memset((dst), 0, (len))
#define CopyMemory(dst, src, len) std::memcpy((dst), (src), (len))

/* Structures with their Windows layout. */
typedef struct tagRECT {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
} RECT, *LPRECT;
typedef const RECT *LPCRECT;

typedef struct tagPOINT {
  LONG x;
  LONG y;
} POINT, *LPPOINT;

typedef struct tagSIZE {
  LONG cx;
  LONG cy;
} SIZE;

// The anonymous struct members are a Microsoft extension that GCC and Clang accept as well.
typedef union _LARGE_INTEGER {
  struct {
    DWORD LowPart;
    LONG HighPart;
  };
  struct {
    DWORD LowPart;
    LONG HighPart;
  } u;
  long long QuadPart;
} LARGE_INTEGER;

typedef union _ULARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  };
  struct {
    DWORD LowPart;
    DWORD HighPart;
  } u;
  unsigned long long QuadPart;
} ULARGE_INTEGER;

typedef struct _SYSTEMTIME {
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;

typedef struct _FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *LPFILETIME;

typedef struct _WIN32_FIND_DATAW {
  DWORD    dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD    nFileSizeHigh;
  DWORD    nFileSizeLow;
  DWORD    dwReserved0;
  DWORD    dwReserved1;
  WCHAR    cFileName[MAX_PATH];
  WCHAR    cAlternateFileName[14];
} WIN32_FIND_DATA, *LPWIN32_FIND_DATA;

typedef struct _COMMTIMEOUTS {
  DWORD ReadIntervalTimeout;
  DWORD ReadTotalTimeoutMultiplier;
  DWORD ReadTotalTimeoutConstant;
  DWORD WriteTotalTimeoutMultiplier;
  DWORD WriteTotalTimeoutConstant;
} COMMTIMEOUTS;

typedef struct _CRITICAL_SECTION {
  void          *DebugInfo;
  LONG           LockCount;
  LONG           RecursionCount;
  HANDLE         OwningThread;
  HANDLE         LockSemaphore;
  std::uintptr_t SpinCount;
} CRITICAL_SECTION;

/* Placeholder with the size of DEVMODEW; only the Windows printing code uses its fields. */
typedef struct _devicemodeW {
  BYTE data[220];
} DEVMODE;

/* Only used through pointers in MeOS headers. */
struct tagLOGFONTW;
typedef struct tagLOGFONTW LOGFONT;
struct tagTEXTMETRICW;
typedef struct tagTEXTMETRICW TEXTMETRIC;
struct _TIME_ZONE_INFORMATION;
typedef struct _TIME_ZONE_INFORMATION TIME_ZONE_INFORMATION;
struct _SECURITY_ATTRIBUTES;
typedef struct _SECURITY_ATTRIBUTES *LPSECURITY_ATTRIBUTES;

/* ---------------------------------------------------------------------
   Strings: code/platform/posix/win32_strings.cpp
   --------------------------------------------------------------------- */
#define CP_ACP        0
#define CP_OEMCP      1
#define CP_THREAD_ACP 3
#define CP_UTF8       65001

#define MB_PRECOMPOSED       0x00000001
#define MB_ERR_INVALID_CHARS 0x00000008

#define LOCALE_USER_DEFAULT   0x0400
#define LOCALE_SYSTEM_DEFAULT 0x0800
#define NORM_IGNORECASE       0x00000001

#define CSTR_LESS_THAN    1
#define CSTR_EQUAL        2
#define CSTR_GREATER_THAN 3

int MultiByteToWideChar(UINT codePage, DWORD flags, LPCSTR src, int srcLen, LPWSTR dst, int dstLen);
int WideCharToMultiByte(UINT codePage, DWORD flags, LPCWSTR src, int srcLen, LPSTR dst, int dstLen,
                        LPCSTR defaultChar, LPBOOL usedDefaultChar);
int CompareString(LCID locale, DWORD flags, LPCWSTR a, int lenA, LPCWSTR b, int lenB);
int lstrcmpi(LPCWSTR a, LPCWSTR b);
BOOL IsCharAlphaNumeric(WCHAR ch);
DWORD CharLowerBuff(LPWSTR buffer, DWORD length);
DWORD CharUpperBuff(LPWSTR buffer, DWORD length);
void OutputDebugString(LPCWSTR text);
void OutputDebugStringA(LPCSTR text);

// wsprintf writes at most 1024 characters plus the terminating null.
inline int wsprintf(LPWSTR buffer, LPCWSTR format, ...) {
  va_list args;
  va_start(args, format);
  int written = meos_compat::vswprintfMsvc(buffer, 1025, format, args);
  va_end(args);
  return written;
}

/* ---------------------------------------------------------------------
   Date and time: code/platform/posix/win32_time.cpp
   --------------------------------------------------------------------- */
void GetLocalTime(LPSYSTEMTIME st);
void GetSystemTime(LPSYSTEMTIME st);
BOOL SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft);
BOOL FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME st);
BOOL FileTimeToLocalFileTime(const FILETIME *fileTime, LPFILETIME localFileTime);
BOOL SystemTimeToTzSpecificLocalTime(const TIME_ZONE_INFORMATION *timeZone, const SYSTEMTIME *universalTime,
                                     LPSYSTEMTIME localTime);
BOOL TzSpecificLocalTimeToSystemTime(const TIME_ZONE_INFORMATION *timeZone, const SYSTEMTIME *localTime,
                                     LPSYSTEMTIME universalTime);
DWORD GetTickCount();
unsigned long long GetTickCount64();
int GetDateFormatA(LCID locale, DWORD flags, const SYSTEMTIME *date, LPCSTR format, LPSTR buffer, int size);
int GetTimeFormatA(LCID locale, DWORD flags, const SYSTEMTIME *time, LPCSTR format, LPSTR buffer, int size);

/* ---------------------------------------------------------------------
   File system, errors and system information: code/platform/posix/win32_files.cpp
   --------------------------------------------------------------------- */
#define ERROR_SUCCESS             0
#define ERROR_FILE_NOT_FOUND      2
#define ERROR_PATH_NOT_FOUND      3
#define ERROR_ACCESS_DENIED       5
#define ERROR_INVALID_HANDLE      6
#define ERROR_NOT_ENOUGH_MEMORY   8
#define ERROR_NO_MORE_FILES       18
#define ERROR_SHARING_VIOLATION   32
#define ERROR_FILE_EXISTS         80
#define ERROR_INVALID_PARAMETER   87
#define ERROR_BUFFER_OVERFLOW     111
#define ERROR_DISK_FULL           112
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_ALREADY_EXISTS      183

#define GENERIC_READ  0x80000000UL
#define GENERIC_WRITE 0x40000000UL

#define FILE_SHARE_READ   0x00000001
#define FILE_SHARE_WRITE  0x00000002
#define FILE_SHARE_DELETE 0x00000004

#define CREATE_NEW        1
#define CREATE_ALWAYS     2
#define OPEN_EXISTING     3
#define OPEN_ALWAYS       4
#define TRUNCATE_EXISTING 5

#define FILE_ATTRIBUTE_READONLY  0x00000001
#define FILE_ATTRIBUTE_HIDDEN    0x00000002
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE   0x00000020
#define FILE_ATTRIBUTE_NORMAL    0x00000080
#define INVALID_FILE_ATTRIBUTES  ((DWORD)-1)

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000

DWORD GetLastError();
void SetLastError(DWORD error);
HANDLE CreateFile(LPCWSTR fileName, DWORD access, DWORD shareMode, LPSECURITY_ATTRIBUTES security,
                  DWORD disposition, DWORD flags, HANDLE templateFile);
BOOL CloseHandle(HANDLE handle);
BOOL DeleteFile(LPCWSTR fileName);
BOOL CopyFile(LPCWSTR existingFileName, LPCWSTR newFileName, BOOL failIfExists);
DWORD GetFileAttributes(LPCWSTR fileName);
DWORD GetCurrentDirectory(DWORD bufferLength, LPWSTR buffer);
HANDLE FindFirstFile(LPCWSTR fileName, LPWIN32_FIND_DATA findData);
BOOL FindNextFile(HANDLE findFile, LPWIN32_FIND_DATA findData);
BOOL FindClose(HANDLE findFile);
DWORD FormatMessage(DWORD flags, LPCVOID source, DWORD messageId, DWORD languageId, LPWSTR buffer, DWORD size,
                    va_list *arguments);
HLOCAL LocalFree(HLOCAL memory);
BOOL GetComputerName(LPWSTR buffer, LPDWORD size);
DWORD GetCurrentThreadId();

/* ---------------------------------------------------------------------
   Embedded resources (stage 1). MeOS loads language tables and images
   from resources compiled in via meos.rc and meoslang.rc.
   --------------------------------------------------------------------- */
HRSRC FindResource(HMODULE module, LPCWSTR name, LPCWSTR type);
HGLOBAL LoadResource(HMODULE module, HRSRC resource);
LPVOID LockResource(HGLOBAL data);
DWORD SizeofResource(HMODULE module, HRSRC resource);

/* ---------------------------------------------------------------------
   Windowing and GDI (stage 1, replaced by the Qt backend).
   --------------------------------------------------------------------- */
#define WM_USER 0x0400

#define MB_OK                0x00000000
#define MB_OKCANCEL          0x00000001
#define MB_YESNOCANCEL       0x00000003
#define MB_YESNO             0x00000004
#define MB_ICONQUESTION      0x00000020
#define MB_ICONEXCLAMATION   0x00000030
#define MB_ICONWARNING       0x00000030
#define MB_ICONINFORMATION   0x00000040

#define IDOK     1
#define IDCANCEL 2
#define IDYES    6
#define IDNO     7

#define GDI_ERROR 0xFFFFFFFFL

int MessageBox(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type);
BOOL PostMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
BOOL DestroyWindow(HWND window);
BOOL InvalidateRect(HWND window, const RECT *rect, BOOL erase);
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);
HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object);
DWORD GetFontData(HDC dc, DWORD table, DWORD offset, LPVOID buffer, DWORD size);
BOOL GetTextExtentPoint32A(HDC dc, LPCSTR text, int length, SIZE *size);
