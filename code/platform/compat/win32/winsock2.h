// Linux port: stand-in for the Windows SDK header <winsock2.h>.
// Winsock is BSD sockets with a few renamed pieces, so the declarations below map
// onto the system headers. Only what MeOS uses is covered. The porting of the
// network code itself (socket.cpp, the online services) follows in stage 2.
#pragma once

#include "windows.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>

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

typedef struct sockaddr SOCKADDR;
typedef struct sockaddr *PSOCKADDR, *LPSOCKADDR;

// Windows declares the IPv4 address as a union with byte and word views
// (download.cpp reads S_un.S_un_b). s_addr stays usable as with struct in_addr.
typedef struct WinInAddr {
  union {
    union {
      struct { UCHAR s_b1, s_b2, s_b3, s_b4; } S_un_b;
      struct { USHORT s_w1, s_w2; } S_un_w;
      ULONG S_addr;
    } S_un;
    in_addr_t s_addr;
  };
} IN_ADDR, *PIN_ADDR;

// Same layout as struct sockaddr_in, with the Windows address type.
typedef struct WinSockAddrIn {
  sa_family_t sin_family;
  in_port_t   sin_port;
  IN_ADDR     sin_addr;
  char        sin_zero[8];
} SOCKADDR_IN, *PSOCKADDR_IN;

typedef struct sockaddr_in6 SOCKADDR_IN6;

// Winsock's select ignores nfds and leaves the timeout unchanged; socket.cpp passes
// 0 and reuses the timeout in a loop. A distinct fd_set type selects this overload
// for code that declares its sets after including this header.
typedef fd_set PosixFdSet;
struct WinFdSet : PosixFdSet {};
#define fd_set WinFdSet

inline int select(int /*nfds*/, WinFdSet *readFds, WinFdSet *writeFds, WinFdSet *exceptFds,
                  timeval *timeout) {
  int highest = -1;
  for (const WinFdSet *set : {readFds, writeFds, exceptFds}) {
    if (!set)
      continue;
    for (int fd = FD_SETSIZE - 1; fd > highest; fd--) {
      if (FD_ISSET(fd, set)) {
        highest = fd;
        break;
      }
    }
  }
  timeval remaining;
  if (timeout)
    remaining = *timeout;
  // The base class pointers select the POSIX function, not this overload.
  return ::select(highest + 1, static_cast<PosixFdSet *>(readFds), static_cast<PosixFdSet *>(writeFds),
                  static_cast<PosixFdSet *>(exceptFds), timeout ? &remaining : nullptr);
}

inline int recvfrom(SOCKET socket, char *buffer, int length, int flags, sockaddr *from, int *fromLength) {
  socklen_t size = fromLength ? static_cast<socklen_t>(*fromLength) : 0;
  const ssize_t received = ::recvfrom(static_cast<int>(socket), buffer, static_cast<size_t>(std::max(length, 0)),
                                      flags, from, fromLength ? &size : nullptr);
  if (fromLength)
    *fromLength = static_cast<int>(size);
  return static_cast<int>(received);
}

// Windows measures the address with an int.
inline SOCKET accept(SOCKET socket, sockaddr *address, int *addressLength) {
  socklen_t length = addressLength ? static_cast<socklen_t>(*addressLength) : 0;
  const int client = ::accept(static_cast<int>(socket), address, addressLength ? &length : nullptr);
  if (addressLength)
    *addressLength = static_cast<int>(length);
  return client < 0 ? INVALID_SOCKET : static_cast<SOCKET>(client);
}
