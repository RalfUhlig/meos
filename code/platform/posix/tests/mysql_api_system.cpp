// Linux port: the MySQL API of the client library that is linked (MariaDB or MySQL).
#include "mysql_api.h"

#include <mysql.h>

namespace {
#include "mysql_api.inc"
}

MysqlApi systemMysqlApi() {
  return describeApi();
}
