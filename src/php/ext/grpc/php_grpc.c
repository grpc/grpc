#include "call.h"
#include "channel.h"
#include "server.h"
#include "completion_queue.h"
#include "event.h"
#include "timeval.h"
#include "credentials.h"
#include "server_credentials.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_grpc.h"

//ZEND_DECLARE_MODULE_GLOBALS(grpc)

/* {{{ grpc_functions[]
 *
 * Every user visible function must have an entry in grpc_functions[].
 */
const zend_function_entry grpc_functions[] = {
    PHP_FE_END  /* Must be the last line in grpc_functions[] */
};
/* }}} */

/* {{{ grpc_module_entry
 */
zend_module_entry grpc_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
    STANDARD_MODULE_HEADER,
#endif
    "grpc",
    grpc_functions,
    PHP_MINIT(grpc),
    PHP_MSHUTDOWN(grpc),
    NULL,
    NULL,
    PHP_MINFO(grpc),
#if ZEND_MODULE_API_NO >= 20010901
    PHP_GRPC_VERSION,
#endif
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_GRPC
ZEND_GET_MODULE(grpc)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("grpc.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_grpc_globals, grpc_globals)
    STD_PHP_INI_ENTRY("grpc.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_grpc_globals, grpc_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_grpc_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_grpc_init_globals(zend_grpc_globals *grpc_globals)
{
    grpc_globals->global_value = 0;
    grpc_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(grpc)
{
    /* If you have INI entries, uncomment these lines
    REGISTER_INI_ENTRIES();
    */
    /* Register call error constants */
    grpc_init();
    REGISTER_LONG_CONSTANT("Grpc\\CALL_OK", GRPC_CALL_OK, CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR", GRPC_CALL_ERROR, CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_NOT_ON_SERVER",
                           GRPC_CALL_ERROR_NOT_ON_SERVER,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_NOT_ON_CLIENT",
                           GRPC_CALL_ERROR_NOT_ON_CLIENT,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_ALREADY_INVOKED",
                           GRPC_CALL_ERROR_ALREADY_INVOKED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_NOT_INVOKED",
                           GRPC_CALL_ERROR_NOT_INVOKED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_ALREADY_FINISHED",
                           GRPC_CALL_ERROR_ALREADY_FINISHED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_TOO_MANY_OPERATIONS",
                           GRPC_CALL_ERROR_TOO_MANY_OPERATIONS,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_INVALID_FLAGS",
                           GRPC_CALL_ERROR_INVALID_FLAGS,
                           CONST_CS);

    /* Register op error constants */
    REGISTER_LONG_CONSTANT("Grpc\\OP_OK", GRPC_OP_OK, CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\OP_ERROR", GRPC_OP_ERROR, CONST_CS);

    /* Register flag constants */
    REGISTER_LONG_CONSTANT("Grpc\\WRITE_BUFFER_HINT",
                           GRPC_WRITE_BUFFER_HINT,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\WRITE_NO_COMPRESS",
                           GRPC_WRITE_NO_COMPRESS,
                           CONST_CS);

    /* Register completion type constants */
    REGISTER_LONG_CONSTANT("Grpc\\QUEUE_SHUTDOWN",
                           GRPC_QUEUE_SHUTDOWN,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\READ", GRPC_READ, CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\INVOKE_ACCEPTED",
                           GRPC_INVOKE_ACCEPTED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\WRITE_ACCEPTED",
                           GRPC_WRITE_ACCEPTED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\FINISH_ACCEPTED",
                           GRPC_FINISH_ACCEPTED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\CLIENT_METADATA_READ",
                           GRPC_CLIENT_METADATA_READ,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\FINISHED", GRPC_FINISHED, CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\SERVER_RPC_NEW",
                           GRPC_SERVER_RPC_NEW,
                           CONST_CS);

    /* Register status constants */
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_OK",
                           GRPC_STATUS_OK,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_CANCELLED",
                           GRPC_STATUS_CANCELLED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNKNOWN",
                           GRPC_STATUS_UNKNOWN,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_INVALID_ARGUMENT",
                           GRPC_STATUS_INVALID_ARGUMENT,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_DEADLINE_EXCEEDED",
                           GRPC_STATUS_DEADLINE_EXCEEDED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_NOT_FOUND",
                           GRPC_STATUS_NOT_FOUND,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_ALREADY_EXISTS",
                           GRPC_STATUS_ALREADY_EXISTS,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_PERMISSION_DENIED",
                           GRPC_STATUS_PERMISSION_DENIED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNAUTHENTICATED",
                           GRPC_STATUS_UNAUTHENTICATED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_RESOURCE_EXHAUSTED",
                           GRPC_STATUS_RESOURCE_EXHAUSTED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_FAILED_PRECONDITION",
                           GRPC_STATUS_FAILED_PRECONDITION,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_ABORTED",
                           GRPC_STATUS_ABORTED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_OUT_OF_RANGE",
                           GRPC_STATUS_OUT_OF_RANGE,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNIMPLEMENTED",
                           GRPC_STATUS_UNIMPLEMENTED,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_INTERNAL",
                           GRPC_STATUS_INTERNAL,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNAVAILABLE",
                           GRPC_STATUS_UNAVAILABLE,
                           CONST_CS);
    REGISTER_LONG_CONSTANT("Grpc\\STATUS_DATA_LOSS",
                           GRPC_STATUS_DATA_LOSS,
                           CONST_CS);

    grpc_init_call(TSRMLS_C);
    grpc_init_channel(TSRMLS_C);
    grpc_init_server(TSRMLS_C);
    grpc_init_completion_queue(TSRMLS_C);
    grpc_init_timeval(TSRMLS_C);
    grpc_init_credentials(TSRMLS_C);
    grpc_init_server_credentials(TSRMLS_C);
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(grpc)
{
    /* uncomment this line if you have INI entries
    UNREGISTER_INI_ENTRIES();
    */
    grpc_shutdown_timeval(TSRMLS_C);
    grpc_shutdown();
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(grpc)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "grpc support", "enabled");
    php_info_print_table_end();

    /* Remove comments if you have entries in php.ini
    DISPLAY_INI_ENTRIES();
    */
}
/* }}} */
/* The previous line is meant for vim and emacs, so it can correctly fold and
   unfold functions in source code. See the corresponding marks just before
   function definition, where the functions purpose is also documented. Please
   follow this convention for the convenience of others editing your code.
*/


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
