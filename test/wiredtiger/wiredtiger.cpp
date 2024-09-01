#include "./wt_util.h"

int main(int argc, char const *argv[]) {
  /*! [access example connection] */
  WT_CONNECTION *conn;
  WT_CURSOR *cursor;
  WT_SESSION *session;
  const char *key, *value;
  int ret;

  cursor = wtInit(&conn, &session);

  /*! [access example cursor open] */

  /*! [access example cursor insert] */
  cursor->set_key(cursor, "key1hello"); /* Insert a record. */
  cursor->set_value(cursor, "value1world");
  error_check(cursor->insert(cursor));
  /*! [access example cursor insert] */

  /*! [access example cursor list] */
  error_check(cursor->reset(cursor)); /* Restart the scan. */
  while ((ret = cursor->next(cursor)) == 0) {
    error_check(cursor->get_key(cursor, &key));
    error_check(cursor->get_value(cursor, &value));

    printf("Got record: %s : %s\n", key, value);
  }
  //   scan_end_check(ret == WT_NOTFOUND); /* Check for end-of-table. */
  /*! [access example cursor list] */

  /*! [access example close] */
  error_check(conn->close(conn, NULL)); /* Close all handles. */
                                        /*! [access example close] */
}
