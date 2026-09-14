// Linux port: the MySQL API as seen by mysqlwrapper.cpp, through the headers in code/mysql.
#include "mysql_api.h"

#include "mysql/mysql.h"

namespace {
#include "mysql_api.inc"
}

MysqlApi vendoredMysqlApi() {
  return describeApi();
}
