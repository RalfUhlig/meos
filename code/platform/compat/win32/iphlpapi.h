// Linux port: stand-in for the Windows SDK header <iphlpapi.h>.
// GetAdaptersAddresses is implemented with getifaddrs in
// code/platform/posix/win32_network.cpp. The structures carry the members MeOS reads
// (ListIpAddresses in download.cpp), in Windows order but not with the full Windows
// layout; the buffer is always filled by that implementation.
#pragma once

#include "winsock2.h"

#define GAA_FLAG_SKIP_UNICAST       0x0001
#define GAA_FLAG_SKIP_ANYCAST       0x0002
#define GAA_FLAG_SKIP_MULTICAST     0x0004
#define GAA_FLAG_SKIP_DNS_SERVER    0x0008
#define GAA_FLAG_SKIP_FRIENDLY_NAME 0x0020

#define IF_TYPE_OTHER             1
#define IF_TYPE_ETHERNET_CSMACD   6
#define IF_TYPE_SOFTWARE_LOOPBACK 24

#define ERROR_NO_DATA 232

typedef struct _SOCKET_ADDRESS {
  LPSOCKADDR lpSockaddr;
  INT        iSockaddrLength;
} SOCKET_ADDRESS, *PSOCKET_ADDRESS;

typedef struct _IP_ADAPTER_UNICAST_ADDRESS {
  ULONG                               Length;
  DWORD                               Flags;
  struct _IP_ADAPTER_UNICAST_ADDRESS *Next;
  SOCKET_ADDRESS                      Address;
} IP_ADAPTER_UNICAST_ADDRESS, *PIP_ADAPTER_UNICAST_ADDRESS;

typedef struct _IP_ADAPTER_ADDRESSES {
  ULONG                         Length;
  DWORD                         IfIndex;
  struct _IP_ADAPTER_ADDRESSES *Next;
  char                         *AdapterName;
  PIP_ADAPTER_UNICAST_ADDRESS   FirstUnicastAddress;
  DWORD                         IfType;
} IP_ADAPTER_ADDRESSES, *PIP_ADAPTER_ADDRESSES;

// Lists the network interfaces with their IPv4 and IPv6 addresses (family AF_INET,
// AF_INET6 or AF_UNSPEC). Returns ERROR_BUFFER_OVERFLOW and the required size if the
// buffer is too small, and ERROR_NO_DATA if there is no interface.
ULONG GetAdaptersAddresses(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES addresses,
                           PULONG sizePointer);
