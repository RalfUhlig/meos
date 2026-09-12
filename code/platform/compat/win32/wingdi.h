/************************************************************************
    MeOS - Orienteering Software
    Linux port: stand-in for the Windows SDK header <wingdi.h>.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// The part of GDI that MeOS uses: device contexts, pens and brushes, lines and filled
// shapes, gradients, bitmaps and DIB sections, fonts and text, and the printer device
// context. Included by windows.h. Constants have their Windows values.
//
// Implemented by the Qt backend in code/platform/qt (stage 1.2); the file named in
// each section does not exist yet.

#pragma once

#include "windows.h"

/* ---------------------------------------------------------------------
   Structures
   --------------------------------------------------------------------- */
typedef USHORT COLOR16;

typedef struct _TRIVERTEX {
  LONG    x;
  LONG    y;
  COLOR16 Red;
  COLOR16 Green;
  COLOR16 Blue;
  COLOR16 Alpha;
} TRIVERTEX, *PTRIVERTEX;

typedef struct _GRADIENT_RECT {
  ULONG UpperLeft;
  ULONG LowerRight;
} GRADIENT_RECT;

typedef struct _BLENDFUNCTION {
  BYTE BlendOp;
  BYTE BlendFlags;
  BYTE SourceConstantAlpha;
  BYTE AlphaFormat;
} BLENDFUNCTION;

typedef struct tagRGBQUAD {
  BYTE rgbBlue;
  BYTE rgbGreen;
  BYTE rgbRed;
  BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  LONG  biWidth;
  LONG  biHeight;
  WORD  biPlanes;
  WORD  biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG  biXPelsPerMeter;
  LONG  biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD          bmiColors[1];
} BITMAPINFO;

#define LF_FACESIZE 32

typedef struct tagLOGFONTW {
  LONG  lfHeight;
  LONG  lfWidth;
  LONG  lfEscapement;
  LONG  lfOrientation;
  LONG  lfWeight;
  BYTE  lfItalic;
  BYTE  lfUnderline;
  BYTE  lfStrikeOut;
  BYTE  lfCharSet;
  BYTE  lfOutPrecision;
  BYTE  lfClipPrecision;
  BYTE  lfQuality;
  BYTE  lfPitchAndFamily;
  WCHAR lfFaceName[LF_FACESIZE];
} LOGFONT, *LPLOGFONT;

typedef struct tagTEXTMETRICW {
  LONG  tmHeight;
  LONG  tmAscent;
  LONG  tmDescent;
  LONG  tmInternalLeading;
  LONG  tmExternalLeading;
  LONG  tmAveCharWidth;
  LONG  tmMaxCharWidth;
  LONG  tmWeight;
  LONG  tmOverhang;
  LONG  tmDigitizedAspectX;
  LONG  tmDigitizedAspectY;
  WCHAR tmFirstChar;
  WCHAR tmLastChar;
  WCHAR tmDefaultChar;
  WCHAR tmBreakChar;
  BYTE  tmItalic;
  BYTE  tmUnderlined;
  BYTE  tmStruckOut;
  BYTE  tmPitchAndFamily;
  BYTE  tmCharSet;
} TEXTMETRIC;

typedef int (CALLBACK *FONTENUMPROC)(const LOGFONT *logFont, const TEXTMETRIC *metric, DWORD fontType,
                                     LPARAM data);

#define CCHDEVICENAME 32
#define CCHFORMNAME   32

// The field layout of DEVMODEW. MeOS copies it as a whole and sets dmSize; it is not
// stored in files, so the different size of WCHAR on Linux does not matter.
typedef struct _devicemodeW {
  WCHAR dmDeviceName[CCHDEVICENAME];
  WORD  dmSpecVersion;
  WORD  dmDriverVersion;
  WORD  dmSize;
  WORD  dmDriverExtra;
  DWORD dmFields;
  union {
    struct {
      short dmOrientation;
      short dmPaperSize;
      short dmPaperLength;
      short dmPaperWidth;
      short dmScale;
      short dmCopies;
      short dmDefaultSource;
      short dmPrintQuality;
    };
    struct {
      POINTL dmPosition;
      DWORD  dmDisplayOrientation;
      DWORD  dmDisplayFixedOutput;
    };
  };
  short dmColor;
  short dmDuplex;
  short dmYResolution;
  short dmTTOption;
  short dmCollate;
  WCHAR dmFormName[CCHFORMNAME];
  WORD  dmLogPixels;
  DWORD dmBitsPerPel;
  DWORD dmPelsWidth;
  DWORD dmPelsHeight;
  union {
    DWORD dmDisplayFlags;
    DWORD dmNup;
  };
  DWORD dmDisplayFrequency;
  DWORD dmICMMethod;
  DWORD dmICMIntent;
  DWORD dmMediaType;
  DWORD dmDitherType;
  DWORD dmReserved1;
  DWORD dmReserved2;
  DWORD dmPanningWidth;
  DWORD dmPanningHeight;
} DEVMODE, *LPDEVMODE;

typedef struct _DOCINFOW {
  int     cbSize;
  LPCWSTR lpszDocName;
  LPCWSTR lpszOutput;
  LPCWSTR lpszDatatype;
  DWORD   fwType;
} DOCINFO;

/* ---------------------------------------------------------------------
   Device contexts, GDI objects, drawing and bitmaps:
   code/platform/qt/win32_gdi.cpp
   --------------------------------------------------------------------- */
#define GDI_ERROR 0xFFFFFFFF

#define WHITE_BRUSH  0
#define LTGRAY_BRUSH 1
#define NULL_BRUSH   5
#define BLACK_PEN    7
#define NULL_PEN     8
#define DC_BRUSH     18
#define DC_PEN       19

#define PS_SOLID 0

#define TRANSPARENT 1
#define OPAQUE      2

#define SRCCOPY 0x00CC0020

#define AC_SRC_OVER  0x00
#define AC_SRC_ALPHA 0x01

#define GRADIENT_FILL_RECT_H 0x00000000
#define GRADIENT_FILL_RECT_V 0x00000001

#define BI_RGB         0
#define DIB_RGB_COLORS 0

HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HGDIOBJ GetStockObject(int index);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object);
BOOL DeleteObject(HGDIOBJ object);
HPEN CreatePen(int style, int width, COLORREF color);
HBRUSH CreateSolidBrush(COLORREF color);
COLORREF SetDCPenColor(HDC dc, COLORREF color);
COLORREF GetDCPenColor(HDC dc);
COLORREF SetDCBrushColor(HDC dc, COLORREF color);
COLORREF SetTextColor(HDC dc, COLORREF color);
COLORREF SetBkColor(HDC dc, COLORREF color);
int SetBkMode(HDC dc, int mode);
int IntersectClipRect(HDC dc, int left, int top, int right, int bottom);
BOOL MoveToEx(HDC dc, int x, int y, LPPOINT previous);
BOOL LineTo(HDC dc, int x, int y);
BOOL Rectangle(HDC dc, int left, int top, int right, int bottom);
BOOL Ellipse(HDC dc, int left, int top, int right, int bottom);
BOOL Polygon(HDC dc, const POINT *points, int count);
BOOL GradientFill(HDC dc, PTRIVERTEX vertices, ULONG vertexCount, PVOID mesh, ULONG meshCount, ULONG mode);
HBITMAP CreateCompatibleBitmap(HDC dc, int width, int height);
HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *info, UINT usage, void **bits, HANDLE section,
                         DWORD offset);
BOOL BitBlt(HDC dst, int x, int y, int width, int height, HDC src, int srcX, int srcY, DWORD rop);
BOOL AlphaBlend(HDC dst, int x, int y, int width, int height, HDC src, int srcX, int srcY, int srcWidth,
                int srcHeight, BLENDFUNCTION blend);

/* ---------------------------------------------------------------------
   Fonts and text: code/platform/qt/win32_text.cpp
   --------------------------------------------------------------------- */
#define FW_LIGHT  300
#define FW_NORMAL 400
#define FW_BOLD   700

#define DEFAULT_CHARSET 1

#define OUT_TT_ONLY_PRECIS  7
#define CLIP_DEFAULT_PRECIS 0

#define PROOF_QUALITY     2
#define CLEARTYPE_QUALITY 5

#define DEFAULT_PITCH 0
#define FF_ROMAN      0x10
#define FF_MODERN     0x30

#define ANSI_FIXED_FONT  11
#define DEFAULT_GUI_FONT 17

HFONT CreateFont(int height, int width, int escapement, int orientation, int weight, DWORD italic,
                 DWORD underline, DWORD strikeOut, DWORD charSet, DWORD outPrecision, DWORD clipPrecision,
                 DWORD quality, DWORD pitchAndFamily, LPCWSTR faceName);
int EnumFontFamiliesEx(HDC dc, LPLOGFONT logFont, FONTENUMPROC enumProc, LPARAM data, DWORD flags);
BOOL TextOut(HDC dc, int x, int y, LPCWSTR text, int length);
BOOL GetTextExtentPoint32(HDC dc, LPCWSTR text, int length, LPSIZE size);
BOOL GetTextExtentPoint32A(HDC dc, LPCSTR text, int length, LPSIZE size);
DWORD GetFontData(HDC dc, DWORD table, DWORD offset, LPVOID buffer, DWORD size);

/* ---------------------------------------------------------------------
   Printer device context: code/platform/qt/win32_gdi.cpp
   Until stage 3 there is no printer; CreateDC fails.
   --------------------------------------------------------------------- */
#define HORZSIZE        4
#define VERTSIZE        6
#define HORZRES         8
#define VERTRES         10
#define PHYSICALWIDTH   110
#define PHYSICALHEIGHT  111
#define PHYSICALOFFSETX 112
#define PHYSICALOFFSETY 113

#define MM_ISOTROPIC 7

HDC CreateDC(LPCWSTR driver, LPCWSTR device, LPCWSTR port, const DEVMODE *initData);
int GetDeviceCaps(HDC dc, int index);
int SetMapMode(HDC dc, int mode);
BOOL SetWindowExtEx(HDC dc, int x, int y, LPSIZE previous);
BOOL SetViewportExtEx(HDC dc, int x, int y, LPSIZE previous);
int StartDoc(HDC dc, const DOCINFO *docInfo);
int EndDoc(HDC dc);
int StartPage(HDC dc);
int EndPage(HDC dc);
