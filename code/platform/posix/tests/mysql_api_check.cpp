/************************************************************************
    MeOS - Orienteering Software
    Linux port: MySQL headers and client library match.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// mysqlwrapper.cpp is compiled against the MySQL 5.7 headers shipped in code/mysql,
// but linked with the client library of the system (MariaDB Connector/C or
// libmysqlclient). This checks that the functions MeOS calls have the same
// signatures in both headers and that the structures and constants it uses have the
// same values, so that the combination is ABI compatible.

#include "mysql_api.h"

#include <algorithm>
#include <cstdio>

int main() {
  const MysqlApi vendored = vendoredMysqlApi();
  const MysqlApi system = systemMysqlApi();
  int failures = 0;

  // Qualifiers of pointed-to types do not change the C calling convention: MySQL 5.7
  // declares const char *mysql_get_server_info(), MariaDB char *. In the mangled type
  // names (Itanium ABI) such a qualifier is a 'K'; the struct names contain none.
  const auto withoutConst = [](std::string name) {
    name.erase(std::remove(name.begin(), name.end(), 'K'), name.end());
    return name;
  };
  for (std::size_t k = 0; k < vendored.signatures.size(); k++) {
    if (withoutConst(vendored.signatures[k]) != withoutConst(system.signatures[k])) {
      std::fprintf(stderr, "signature %zu differs: %s (code/mysql) vs %s (system)\n", k,
                   vendored.signatures[k].c_str(), system.signatures[k].c_str());
      failures++;
    }
  }
  const auto compare = [&failures](const char *what, std::size_t a, std::size_t b) {
    if (a != b) {
      std::fprintf(stderr, "%s differs: %zu (code/mysql) vs %zu (system)\n", what, a, b);
      failures++;
    }
  };
  compare("MYSQL_OPT_RECONNECT", std::size_t(vendored.optReconnect), std::size_t(system.optReconnect));
  compare("sizeof(MYSQL_FIELD)", vendored.fieldSize, system.fieldSize);
  compare("offsetof(MYSQL_FIELD, name)", vendored.fieldNameOffset, system.fieldNameOffset);
  compare("sizeof(MYSQL_ROW)", vendored.rowSize, system.rowSize);

  if (failures) {
    std::fprintf(stderr, "%d difference(s)\n", failures);
    return 1;
  }
  std::printf("MySQL API check passed\n");
  return 0;
}
