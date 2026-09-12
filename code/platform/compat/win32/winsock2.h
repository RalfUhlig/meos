// Linux port: stand-in for the Windows SDK header <winsock2.h>.
// Winsock is BSD sockets with a few renamed pieces, so the declarations below map
// onto the system headers. Only what MeOS uses is covered. The porting of the
// network code itself (socket.cpp, the online services) follows in stage 2.
#pragma once

#include "windows.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef UINT_PTR SOCKET;

#define INVALID_SOCKET (SOCKET)(~0)
#define SOCKET_ERROR   (-1)

#define SD_RECEIVE 0
#define SD_SEND    1
#define SD_BOTH    2

// WSAGetLastError reports errno, so the codes MeOS compares against are the errno ones.
#define WSAESHUTDOWN    ESHUTDOWN
#define WSAECONNABORTED ECONNABORTED
#define WSAECONNRESET   ECONNRESET
#define WSAEWOULDBLOCK  EWOULDBLOCK
#define WSAETIMEDOUT    ETIMEDOUT

typedef struct WSAData {
  WORD           wVersion;
  WORD           wHighVersion;
  unsigned short iMaxSockets;
  unsigned short iMaxUdpDg;
  char          *lpVendorInfo;
  char           szDescription[257];
  char           szSystemStatus[129];
} WSADATA, *LPWSADATA;

// Sockets need no initialization on Linux.
inline int WSAStartup(WORD version, LPWSADATA data) {
  if (!data)
    return -1;
  std::memset(data, 0, sizeof(*data));
  data->wVersion = version;
  data->wHighVersion = version;
  return 0;
}

inline int WSACleanup() {
  return 0;
}

inline int WSAGetLastError() {
  return errno;
}

inline int closesocket(SOCKET socket) {
  return ::close(static_cast<int>(socket));
}

// The only call that differs in shape: Windows measures the address with an int.
inline SOCKET accept(SOCKET socket, sockaddr *address, int *addressLength) {
  socklen_t length = addressLength ? static_cast<socklen_t>(*addressLength) : 0;
  const int client = ::accept(static_cast<int>(socket), address, addressLength ? &length : nullptr);
  if (addressLength)
    *addressLength = static_cast<int>(length);
  return client < 0 ? INVALID_SOCKET : static_cast<SOCKET>(client);
}
