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
// Functions in the sections that name an implementation file in code/platform/posix
// are implemented there. Sections that name a file in code/platform/qt belong to the
// Qt backend (stage 1.2, see plans/linux-port-1.2-qt-backend.md); until that file
// exists, its functions are only declared so that the MeOS sources compile.
//
// As in the Windows SDK, the window manager and GDI parts are in winuser.h and
// wingdi.h, which are included at the end of this header.

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
MEOS_DECLARE_HANDLE(HACCEL);
MEOS_DECLARE_HANDLE(HHOOK);
MEOS_DECLARE_HANDLE(HMONITOR);

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
typedef WORD          *LPWORD;
typedef BOOL          *LPBOOL;
typedef int            INT;
typedef std::uint16_t  USHORT;
typedef unsigned char  UCHAR;
typedef std::size_t    SIZE_T;
typedef void          *PVOID;
typedef ULONG         *PULONG;

// From rpcndr.h, which windows.h includes.
typedef unsigned char  byte;

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

#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xFF))
#define HIBYTE(w) ((BYTE)(((DWORD_PTR)(w) >> 8) & 0xFF))
#define MAKEWORD(low, high) ((WORD)(((BYTE)((DWORD_PTR)(low) & 0xFF)) | ((WORD)((BYTE)((DWORD_PTR)(high) & 0xFF))) << 8))
#define MAKELONG(low, high) ((LONG)(((WORD)((DWORD_PTR)(low) & 0xFFFF)) | ((DWORD)((WORD)((DWORD_PTR)(high) & 0xFFFF))) << 16))
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xFFFF))
#define HIWORD(l) ((WORD)(((DWORD_PTR)(l) >> 16) & 0xFFFF))

#define MAXUINT ((UINT)~((UINT)0))
#define MAXINT  ((INT)(MAXUINT >> 1))

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

typedef struct _POINTL {
  LONG x;
  LONG y;
} POINTL;

typedef struct tagSIZE {
  LONG cx;
  LONG cy;
} SIZE, *LPSIZE;

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

/* Only used through pointers in MeOS headers. */
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
LPWSTR CharLower(LPWSTR text);
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
BOOL LocalFileTimeToFileTime(const FILETIME *localFileTime, LPFILETIME fileTime);
BOOL DosDateTimeToFileTime(WORD fatDate, WORD fatTime, LPFILETIME fileTime);
BOOL FileTimeToDosDateTime(const FILETIME *fileTime, LPWORD fatDate, LPWORD fatTime);
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
#define NOERROR                   0
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
#define ERROR_DIR_NOT_EMPTY       145
#define ERROR_ALREADY_EXISTS      183
#define ERROR_DIRECTORY           267
#define ERROR_CANCELLED           1223

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
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
DWORD GetFileSize(HANDLE file, LPDWORD fileSizeHigh);
BOOL CreateDirectory(LPCWSTR pathName, LPSECURITY_ATTRIBUTES security);
// The folder for temporary files ($TMPDIR or /tmp), with a trailing separator.
DWORD GetTempPath(DWORD bufferLength, LPWSTR buffer);
// With unique == 0 creates an empty file <path>/<prefix, 3 characters><hex>.TMP.
UINT GetTempFileName(LPCWSTR pathName, LPCWSTR prefixString, UINT unique, LPWSTR tempFileName);
// Only the executable of the process (module == NULL).
DWORD GetModuleFileName(HMODULE module, LPWSTR fileName, DWORD size);
BOOL RemoveDirectory(LPCWSTR pathName);
// The creation time is the status change time; Linux file systems keep no portable birth time.
BOOL GetFileTime(HANDLE file, LPFILETIME creationTime, LPFILETIME lastAccessTime, LPFILETIME lastWriteTime);
// The creation time cannot be set and is ignored.
BOOL SetFileTime(HANDLE file, const FILETIME *creationTime, const FILETIME *lastAccessTime,
                 const FILETIME *lastWriteTime);
DWORD GetFileAttributes(LPCWSTR fileName);
DWORD GetCurrentDirectory(DWORD bufferLength, LPWSTR buffer);
BOOL SetCurrentDirectory(LPCWSTR path);
HANDLE FindFirstFile(LPCWSTR fileName, LPWIN32_FIND_DATA findData);
BOOL FindNextFile(HANDLE findFile, LPWIN32_FIND_DATA findData);
BOOL FindClose(HANDLE findFile);
DWORD FormatMessage(DWORD flags, LPCVOID source, DWORD messageId, DWORD languageId, LPWSTR buffer, DWORD size,
                    va_list *arguments);
HLOCAL LocalFree(HLOCAL memory);
BOOL GetComputerName(LPWSTR buffer, LPDWORD size);
DWORD GetCurrentThreadId();

/* ---------------------------------------------------------------------
   Serial ports: code/platform/posix/win32_serial.cpp
   --------------------------------------------------------------------- */
#define ERROR_OPERATION_ABORTED 995

#define CBR_4800   4800
#define CBR_9600   9600
#define CBR_19200  19200
#define CBR_38400  38400
#define CBR_57600  57600
#define CBR_115200 115200

#define NOPARITY    0
#define ODDPARITY   1
#define EVENPARITY  2
#define MARKPARITY  3
#define SPACEPARITY 4

#define ONESTOPBIT   0
#define ONE5STOPBITS 1
#define TWOSTOPBITS  2

#define DTR_CONTROL_DISABLE   0x00
#define DTR_CONTROL_ENABLE    0x01
#define DTR_CONTROL_HANDSHAKE 0x02

#define RTS_CONTROL_DISABLE   0x00
#define RTS_CONTROL_ENABLE    0x01
#define RTS_CONTROL_HANDSHAKE 0x02
#define RTS_CONTROL_TOGGLE    0x03

#define EV_RXCHAR 0x0001

// Bit field layout of the Windows structure: MeOS sets DCBlength to sizeof(DCB).
typedef struct _DCB {
  DWORD DCBlength;
  DWORD BaudRate;
  DWORD fBinary           : 1;
  DWORD fParity           : 1;
  DWORD fOutxCtsFlow      : 1;
  DWORD fOutxDsrFlow      : 1;
  DWORD fDtrControl       : 2;
  DWORD fDsrSensitivity   : 1;
  DWORD fTXContinueOnXoff : 1;
  DWORD fOutX             : 1;
  DWORD fInX              : 1;
  DWORD fErrorChar        : 1;
  DWORD fNull             : 1;
  DWORD fRtsControl       : 2;
  DWORD fAbortOnError     : 1;
  DWORD fDummy2           : 17;
  WORD  wReserved;
  WORD  XonLim;
  WORD  XoffLim;
  BYTE  ByteSize;
  BYTE  Parity;
  BYTE  StopBits;
  char  XonChar;
  char  XoffChar;
  char  ErrorChar;
  char  EofChar;
  char  EvtChar;
  WORD  wReserved1;
} DCB, *LPDCB;

BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD count, LPDWORD read, LPVOID overlapped);
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD count, LPDWORD written, LPVOID overlapped);
BOOL GetCommState(HANDLE file, LPDCB state);
BOOL SetCommState(HANDLE file, LPDCB state);
BOOL GetCommTimeouts(HANDLE file, COMMTIMEOUTS *timeouts);
BOOL SetCommTimeouts(HANDLE file, COMMTIMEOUTS *timeouts);
BOOL SetCommMask(HANDLE file, DWORD mask);
BOOL WaitCommEvent(HANDLE file, LPDWORD mask, LPVOID overlapped);
DWORD QueryDosDevice(LPCWSTR deviceName, LPWSTR targetPath, DWORD max);

/* ---------------------------------------------------------------------
   Threads and synchronisation: code/platform/posix/win32_threads.cpp
   --------------------------------------------------------------------- */
#define STILL_ACTIVE 259

void Sleep(DWORD milliseconds);
void InitializeCriticalSection(CRITICAL_SECTION *section);
void DeleteCriticalSection(CRITICAL_SECTION *section);
void EnterCriticalSection(CRITICAL_SECTION *section);
void LeaveCriticalSection(CRITICAL_SECTION *section);
BOOL TerminateThread(HANDLE thread, DWORD exitCode);
BOOL GetExitCodeThread(HANDLE thread, LPDWORD exitCode);

/* ---------------------------------------------------------------------
   Embedded resources: code/platform/qt/win32_resources.cpp. MeOS loads
   language tables and images from resources compiled in via meos.rc and
   meoslang.rc.
   --------------------------------------------------------------------- */
#define MAKEINTRESOURCE(id) ((LPWSTR)(std::uintptr_t)((WORD)(id)))
#define IS_INTRESOURCE(p)   (((std::uintptr_t)(p) >> 16) == 0)

#define ERROR_MOD_NOT_FOUND           126
#define ERROR_RESOURCE_TYPE_NOT_FOUND 1813
#define ERROR_RESOURCE_NAME_NOT_FOUND 1814

#define RT_BITMAP     MAKEINTRESOURCE(2)
#define RT_RCDATA     MAKEINTRESOURCE(10)
#define RT_GROUP_ICON MAKEINTRESOURCE(14)
#define RT_HTML       MAKEINTRESOURCE(23)

HMODULE GetModuleHandle(LPCWSTR moduleName);
HRSRC FindResource(HMODULE module, LPCWSTR name, LPCWSTR type);
HGLOBAL LoadResource(HMODULE module, HRSRC resource);
LPVOID LockResource(HGLOBAL data);
DWORD SizeofResource(HMODULE module, HRSRC resource);

/* ---------------------------------------------------------------------
   Global memory blocks (clipboard and printer settings):
   code/platform/qt/win32_clipboard.cpp
   --------------------------------------------------------------------- */
#define GMEM_FIXED    0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GMEM_DDESHARE 0x2000

HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes);
LPVOID GlobalLock(HGLOBAL memory);
BOOL GlobalUnlock(HGLOBAL memory);
SIZE_T GlobalSize(HGLOBAL memory);
HGLOBAL GlobalFree(HGLOBAL memory);

#include "winuser.h"
#include "wingdi.h"
