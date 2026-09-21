/************************************************************************
    MeOS - Orienteering Software
    Linux port: the wide string functions of the C library, for unaligned text.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// MeOS keeps fixed-length text fields of its objects in byte arrays (oData in
// oClass, oRunner, ...), so a wchar_t there can lie at any address. On Windows
// wchar_t has two bytes and the CRT reads it one character at a time. glibc
// selects vectorised versions of its wide string functions (IFUNC) that assume
// wchar_t aligned to four bytes; on an unaligned string they return wrong
// results (a 20-character result module tag of a class measured 19). The
// program defines these functions itself, one character at a time: the
// executable exports them, so the dynamic linker binds the calls from
// libstdc++ (std::wstring) to them as well.
//
// Replaced are the functions glibc 2.39 dispatches through IFUNC on x86-64
// (readelf -Ws libc.so.6). The others read one character at a time already;
// calls inside glibc to its own hidden aliases cannot be replaced.
//
// <cwchar> is not included: its C++ overloads of wcschr and wcsrchr would clash
// with the C definitions. The file is compiled with -fno-builtin and
// -fno-tree-loop-distribute-patterns (code/CMakeLists.txt), so that GCC does not
// turn these loops back into calls of the functions they define.

using size_t = decltype(sizeof(0));

extern "C" {

size_t wcslen(const wchar_t *s) {
  const wchar_t *p = s;
  while (*p)
    p++;
  return size_t(p - s);
}

size_t wcsnlen(const wchar_t *s, size_t maxlen) {
  size_t n = 0;
  while (n < maxlen && s[n])
    n++;
  return n;
}

int wcscmp(const wchar_t *a, const wchar_t *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  // wchar_t is signed on Linux; glibc compares the values as signed as well.
  return *a < *b ? -1 : (*a > *b ? 1 : 0);
}

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n) {
  for (; n > 0; n--, a++, b++) {
    if (*a != *b)
      return *a < *b ? -1 : 1;
    if (!*a)
      return 0;
  }
  return 0;
}

wchar_t *wcschr(const wchar_t *s, wchar_t c) {
  for (;; s++) {
    if (*s == c)
      return const_cast<wchar_t *>(s);
    if (!*s)
      return nullptr;
  }
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c) {
  const wchar_t *last = nullptr;
  for (;; s++) {
    if (*s == c)
      last = s;
    if (!*s)
      return const_cast<wchar_t *>(last);
  }
}

wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n) {
  for (; n > 0; n--, s++) {
    if (*s == c)
      return const_cast<wchar_t *>(s);
  }
  return nullptr;
}

int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n) {
  for (; n > 0; n--, a++, b++) {
    if (*a != *b)
      return *a < *b ? -1 : 1;
  }
  return 0;
}

wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n) {
  for (size_t k = 0; k < n; k++)
    s[k] = c;
  return s;
}

wchar_t *__wmemset_chk(wchar_t *s, wchar_t c, size_t n, size_t destlen) {
  if (n > destlen)
    __builtin_trap();
  return wmemset(s, c, n);
}

wchar_t *wcpcpy(wchar_t *dst, const wchar_t *src) {
  while ((*dst = *src) != 0) {
    dst++;
    src++;
  }
  return dst;
}

wchar_t *wcscpy(wchar_t *dst, const wchar_t *src) {
  wcpcpy(dst, src);
  return dst;
}

// Copies at most n characters and fills the rest of the n with zeros; returns the
// end of the copied text (wcpncpy) or dst (wcsncpy).
wchar_t *wcpncpy(wchar_t *dst, const wchar_t *src, size_t n) {
  size_t k = 0;
  for (; k < n && src[k]; k++)
    dst[k] = src[k];
  wchar_t *end = dst + k;
  for (; k < n; k++)
    dst[k] = 0;
  return end;
}

wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n) {
  wcpncpy(dst, src, n);
  return dst;
}

wchar_t *wcscat(wchar_t *dst, const wchar_t *src) {
  wcscpy(dst + wcslen(dst), src);
  return dst;
}

wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n) {
  wchar_t *p = dst + wcslen(dst);
  size_t k = 0;
  for (; k < n && src[k]; k++)
    p[k] = src[k];
  p[k] = 0;
  return dst;
}

} // extern "C"
