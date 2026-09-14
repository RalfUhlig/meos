// Linux port: the part of the MySQL C API that MeOS uses (mysqlwrapper.cpp), described
// once for mysql_api_check.cpp. Included after a mysql.h, inside a namespace.

inline MysqlApi describeApi() {
  MysqlApi api;
  api.signatures = {
    typeid(&mysql_affected_rows).name(),   typeid(&mysql_close).name(),
    typeid(&mysql_data_seek).name(),       typeid(&mysql_error).name(),
    typeid(&mysql_fetch_fields).name(),    typeid(&mysql_fetch_lengths).name(),
    typeid(&mysql_fetch_row).name(),       typeid(&mysql_field_count).name(),
    typeid(&mysql_free_result).name(),     typeid(&mysql_get_server_info).name(),
    typeid(&mysql_init).name(),            typeid(&mysql_insert_id).name(),
    typeid(&mysql_num_fields).name(),      typeid(&mysql_num_rows).name(),
    typeid(&mysql_options).name(),         typeid(&mysql_query).name(),
    typeid(&mysql_real_connect).name(),    typeid(&mysql_real_escape_string).name(),
    typeid(&mysql_real_query).name(),      typeid(&mysql_select_db).name(),
    typeid(&mysql_store_result).name(),    typeid(&mysql_use_result).name(),
  };
  api.optReconnect = MYSQL_OPT_RECONNECT;
  api.fieldSize = sizeof(MYSQL_FIELD);
  api.fieldNameOffset = offsetof(MYSQL_FIELD, name);
  api.rowSize = sizeof(MYSQL_ROW);
  return api;
}
