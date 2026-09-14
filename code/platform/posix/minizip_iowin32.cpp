/************************************************************************
    MeOS - Orienteering Software
    Linux port: the wide-character file functions of minizip's iowin32.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// zip.cpp opens archives with fill_win32_filefunc64W, which passes the file name as a
// wchar_t string through minizip's const void * parameter. The system minizip only has
// the stdio functions for narrow names; these callbacks accept MeOS file names ('\' or
// '/' as separator) and work on stdio streams.

#include "windows.h"

#include "minizip/ioapi.h"
#include "minizip/iowin32.h"

namespace {

voidpf ZCALLBACK openWide(voidpf /*opaque*/, const void *fileName, int mode) {
  const char *fopenMode = nullptr;
  if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) == ZLIB_FILEFUNC_MODE_READ)
    fopenMode = "rb";
  else if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
    fopenMode = "r+b";
  else if (mode & ZLIB_FILEFUNC_MODE_CREATE)
    fopenMode = "wb";
  if (!fileName || !fopenMode)
    return nullptr;
  return std::fopen(meos_compat::nativePath(static_cast<const wchar_t *>(fileName)).c_str(), fopenMode);
}

uLong ZCALLBACK readFile(voidpf /*opaque*/, voidpf stream, void *buffer, uLong size) {
  return static_cast<uLong>(std::fread(buffer, 1, size, static_cast<FILE *>(stream)));
}

uLong ZCALLBACK writeFile(voidpf /*opaque*/, voidpf stream, const void *buffer, uLong size) {
  return static_cast<uLong>(std::fwrite(buffer, 1, size, static_cast<FILE *>(stream)));
}

ZPOS64_T ZCALLBACK tellFile(voidpf /*opaque*/, voidpf stream) {
  const off_t position = ::ftello(static_cast<FILE *>(stream));
  return position < 0 ? static_cast<ZPOS64_T>(-1) : static_cast<ZPOS64_T>(position);
}

long ZCALLBACK seekFile(voidpf /*opaque*/, voidpf stream, ZPOS64_T offset, int origin) {
  int whence;
  switch (origin) {
    case ZLIB_FILEFUNC_SEEK_CUR:
      whence = SEEK_CUR;
      break;
    case ZLIB_FILEFUNC_SEEK_END:
      whence = SEEK_END;
      break;
    case ZLIB_FILEFUNC_SEEK_SET:
      whence = SEEK_SET;
      break;
    default:
      return -1;
  }
  return ::fseeko(static_cast<FILE *>(stream), static_cast<off_t>(offset), whence) == 0 ? 0 : -1;
}

int ZCALLBACK closeFile(voidpf /*opaque*/, voidpf stream) {
  return std::fclose(static_cast<FILE *>(stream));
}

int ZCALLBACK testErrorFile(voidpf /*opaque*/, voidpf stream) {
  return std::ferror(static_cast<FILE *>(stream));
}

} // namespace

void fill_win32_filefunc64W(zlib_filefunc64_def *functions) {
  functions->zopen64_file = openWide;
  functions->zread_file = readFile;
  functions->zwrite_file = writeFile;
  functions->ztell64_file = tellFile;
  functions->zseek64_file = seekFile;
  functions->zclose_file = closeFile;
  functions->zerror_file = testErrorFile;
  functions->opaque = nullptr;
}
