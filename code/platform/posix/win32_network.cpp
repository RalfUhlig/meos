/************************************************************************
    MeOS - Orienteering Software
    Linux port: network interface enumeration (IP Helper API).

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_handle.h"

#include "iphlpapi.h"

#include <ifaddrs.h>
#include <net/if.h>

#include <map>

namespace {

struct Interface {
  std::string name;
  unsigned index = 0;
  bool loopback = false;
  std::vector<std::vector<char>> addresses; // sockaddr_in or sockaddr_in6
};

std::size_t alignedSize(std::size_t size) {
  const std::size_t alignment = alignof(std::max_align_t);
  return (size + alignment - 1) / alignment * alignment;
}

} // namespace

ULONG GetAdaptersAddresses(ULONG family, ULONG flags, PVOID /*reserved*/, PIP_ADAPTER_ADDRESSES addresses,
                           PULONG sizePointer) {
  if (!sizePointer || (family != AF_UNSPEC && family != AF_INET && family != AF_INET6))
    return ERROR_INVALID_PARAMETER;

  ifaddrs *list = nullptr;
  if (::getifaddrs(&list) != 0)
    return ERROR_NOT_ENOUGH_MEMORY;

  // Interfaces in the order the system lists them; an interface without an address of
  // the family is still an adapter, as on Windows.
  std::vector<Interface> interfaces;
  std::map<std::string, std::size_t> byName;
  for (ifaddrs *entry = list; entry; entry = entry->ifa_next) {
    if (!entry->ifa_name)
      continue;
    auto found = byName.find(entry->ifa_name);
    if (found == byName.end()) {
      found = byName.emplace(entry->ifa_name, interfaces.size()).first;
      Interface added;
      added.name = entry->ifa_name;
      added.index = ::if_nametoindex(entry->ifa_name);
      added.loopback = (entry->ifa_flags & IFF_LOOPBACK) != 0;
      interfaces.push_back(std::move(added));
    }
    const sockaddr *address = entry->ifa_addr;
    if (!address || (flags & GAA_FLAG_SKIP_UNICAST))
      continue;
    std::size_t length = 0;
    if (address->sa_family == AF_INET && family != AF_INET6)
      length = sizeof(sockaddr_in);
    else if (address->sa_family == AF_INET6 && family != AF_INET)
      length = sizeof(sockaddr_in6);
    if (length > 0) {
      const char *bytes = reinterpret_cast<const char *>(address);
      interfaces[found->second].addresses.emplace_back(bytes, bytes + length);
    }
  }
  ::freeifaddrs(list);

  if (interfaces.empty())
    return ERROR_NO_DATA;

  std::size_t required = 0;
  for (const Interface &adapter : interfaces) {
    required += alignedSize(sizeof(IP_ADAPTER_ADDRESSES)) + alignedSize(adapter.name.size() + 1);
    for (const std::vector<char> &address : adapter.addresses)
      required += alignedSize(sizeof(IP_ADAPTER_UNICAST_ADDRESS)) + alignedSize(address.size());
  }
  if (!addresses || *sizePointer < required) {
    *sizePointer = static_cast<ULONG>(required);
    return ERROR_BUFFER_OVERFLOW;
  }

  std::memset(addresses, 0, required);
  char *next = reinterpret_cast<char *>(addresses);
  const auto allocate = [&next](std::size_t size) {
    char *block = next;
    next += alignedSize(size);
    return block;
  };

  IP_ADAPTER_ADDRESSES *previous = nullptr;
  for (const Interface &adapter : interfaces) {
    auto *entry = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(allocate(sizeof(IP_ADAPTER_ADDRESSES)));
    entry->Length = sizeof(IP_ADAPTER_ADDRESSES);
    entry->IfIndex = adapter.index;
    entry->IfType = adapter.loopback ? IF_TYPE_SOFTWARE_LOOPBACK : IF_TYPE_ETHERNET_CSMACD;
    entry->AdapterName = allocate(adapter.name.size() + 1);
    std::memcpy(entry->AdapterName, adapter.name.c_str(), adapter.name.size() + 1);
    if (previous)
      previous->Next = entry;
    previous = entry;

    IP_ADAPTER_UNICAST_ADDRESS *previousAddress = nullptr;
    for (const std::vector<char> &address : adapter.addresses) {
      auto *unicast = reinterpret_cast<IP_ADAPTER_UNICAST_ADDRESS *>(allocate(sizeof(IP_ADAPTER_UNICAST_ADDRESS)));
      unicast->Length = sizeof(IP_ADAPTER_UNICAST_ADDRESS);
      char *stored = allocate(address.size());
      std::memcpy(stored, address.data(), address.size());
      unicast->Address.lpSockaddr = reinterpret_cast<LPSOCKADDR>(stored);
      unicast->Address.iSockaddrLength = static_cast<INT>(address.size());
      if (previousAddress)
        previousAddress->Next = unicast;
      else
        entry->FirstUnicastAddress = unicast;
      previousAddress = unicast;
    }
  }
  return ERROR_SUCCESS;
}
