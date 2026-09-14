// Linux port: layout and signatures of the MySQL C API, see mysql_api_check.cpp.
#pragma once

#include <cstddef>
#include <string>
#include <typeinfo>
#include <vector>

struct MysqlApi {
  std::vector<std::string> signatures;
  int optReconnect = 0;
  std::size_t fieldSize = 0;
  std::size_t fieldNameOffset = 0;
  std::size_t rowSize = 0;
};

MysqlApi vendoredMysqlApi();
MysqlApi systemMysqlApi();
