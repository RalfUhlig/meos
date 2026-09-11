// Linux port: stand-in for the Windows SDK header <wininet.h>.
// Only the error codes evaluated by portable code are declared. HTTP access in
// download.cpp is ported to libcurl in stage 2.
#pragma once

#include "windows.h"

#define INTERNET_ERROR_BASE    12000
#define ERROR_INTERNET_TIMEOUT (INTERNET_ERROR_BASE + 2)
