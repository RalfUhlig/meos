// Linux port: stand-in for the Windows SDK header <winsock2.h>.
// Declares the socket types that MeOS headers use as data members. socket.cpp is
// ported to BSD sockets in stage 2.
#pragma once

#include "windows.h"

typedef UINT_PTR SOCKET;

#define INVALID_SOCKET (SOCKET)(~0)
#define SOCKET_ERROR   (-1)
