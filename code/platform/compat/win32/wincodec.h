// Linux port: stand-in for the Windows SDK header <wincodec.h> (Windows Imaging
// Component). image.cpp includes it but decodes PNG images with libpng; nothing from
// it is used.

#pragma once

#include "windows.h"
