/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "call.h"
#include "channel.h"
#include "server.h"
#include "timeval.h"
#include "channel_credentials.h"
#include "call_credentials.h"
#include "server_credentials.h"
#include "completion_queue.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include "php_grpc.h"

// ZEND_DECLARE_MODULE_GLOBALS(grpc)

/* {{{ grpc_functions[]
 *
 * Every user visible function must have an entry in grpc_functions[].
 */
const zend_function_entry grpc_functions[] = {
    PHP_FE_END /* Must be the last line in grpc_functions[] */
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
  STANDARD_MODULE_PROPERTIES};
/* }}} */

#ifdef COMPILE_DL_GRPC
ZEND_GET_MODULE(grpc)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
   PHP_INI_BEGIN()
   STD_PHP_INI_ENTRY("grpc.global_value", "42", PHP_INI_ALL, OnUpdateLong,
                     global_value, zend_grpc_globals, grpc_globals)
   STD_PHP_INI_ENTRY("grpc.global_string", "foobar", PHP_INI_ALL,
                     OnUpdateString, global_string, zend_grpc_globals,
                     grpc_globals)
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
PHP_MINIT_FUNCTION(grpc) {
  /* If you have INI entries, uncomment these lines
     REGISTER_INI_ENTRIES();
  */
  /* Register call error constants */
  grpc_init();
  REGISTER_LONG_CONSTANT("Grpc\\CALL_OK", GRPC_CALL_OK,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR", GRPC_CALL_ERROR,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_NOT_ON_SERVER",
                         GRPC_CALL_ERROR_NOT_ON_SERVER,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_NOT_ON_CLIENT",
                         GRPC_CALL_ERROR_NOT_ON_CLIENT,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_ALREADY_INVOKED",
                         GRPC_CALL_ERROR_ALREADY_INVOKED,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_NOT_INVOKED",
                         GRPC_CALL_ERROR_NOT_INVOKED,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_ALREADY_FINISHED",
                         GRPC_CALL_ERROR_ALREADY_FINISHED,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_TOO_MANY_OPERATIONS",
                         GRPC_CALL_ERROR_TOO_MANY_OPERATIONS,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_INVALID_FLAGS",
                         GRPC_CALL_ERROR_INVALID_FLAGS,
                         CONST_CS | CONST_PERSISTENT);

  /* Register flag constants */
  REGISTER_LONG_CONSTANT("Grpc\\WRITE_BUFFER_HINT", GRPC_WRITE_BUFFER_HINT,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\WRITE_NO_COMPRESS", GRPC_WRITE_NO_COMPRESS,
                         CONST_CS | CONST_PERSISTENT);

  /* Register status constants */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_OK", GRPC_STATUS_OK,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_CANCELLED", GRPC_STATUS_CANCELLED,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNKNOWN", GRPC_STATUS_UNKNOWN,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_INVALID_ARGUMENT",
                         GRPC_STATUS_INVALID_ARGUMENT,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_DEADLINE_EXCEEDED",
                         GRPC_STATUS_DEADLINE_EXCEEDED,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_NOT_FOUND", GRPC_STATUS_NOT_FOUND,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_ALREADY_EXISTS",
                         GRPC_STATUS_ALREADY_EXISTS,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_PERMISSION_DENIED",
                         GRPC_STATUS_PERMISSION_DENIED,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNAUTHENTICATED",
                         GRPC_STATUS_UNAUTHENTICATED,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_RESOURCE_EXHAUSTED",
                         GRPC_STATUS_RESOURCE_EXHAUSTED,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_FAILED_PRECONDITION",
                         GRPC_STATUS_FAILED_PRECONDITION,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_ABORTED", GRPC_STATUS_ABORTED,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_OUT_OF_RANGE",
                         GRPC_STATUS_OUT_OF_RANGE,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNIMPLEMENTED",
                         GRPC_STATUS_UNIMPLEMENTED,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_INTERNAL", GRPC_STATUS_INTERNAL,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNAVAILABLE", GRPC_STATUS_UNAVAILABLE,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_DATA_LOSS", GRPC_STATUS_DATA_LOSS,
                         CONST_CS | CONST_PERSISTENT);

  /* Register op type constants */
  REGISTER_LONG_CONSTANT("Grpc\\OP_SEND_INITIAL_METADATA",
                         GRPC_OP_SEND_INITIAL_METADATA,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\OP_SEND_MESSAGE",
                         GRPC_OP_SEND_MESSAGE,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\OP_SEND_CLOSE_FROM_CLIENT",
                         GRPC_OP_SEND_CLOSE_FROM_CLIENT,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\OP_SEND_STATUS_FROM_SERVER",
                         GRPC_OP_SEND_STATUS_FROM_SERVER,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\OP_RECV_INITIAL_METADATA",
                         GRPC_OP_RECV_INITIAL_METADATA,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\OP_RECV_MESSAGE",
                         GRPC_OP_RECV_MESSAGE,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\OP_RECV_STATUS_ON_CLIENT",
                         GRPC_OP_RECV_STATUS_ON_CLIENT,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\OP_RECV_CLOSE_ON_SERVER",
                         GRPC_OP_RECV_CLOSE_ON_SERVER,
                         CONST_CS | CONST_PERSISTENT);

  /* Register connectivity state constants */
  REGISTER_LONG_CONSTANT("Grpc\\CHANNEL_IDLE",
                         GRPC_CHANNEL_IDLE,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CHANNEL_CONNECTING",
                         GRPC_CHANNEL_CONNECTING,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CHANNEL_READY",
                         GRPC_CHANNEL_READY,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CHANNEL_TRANSIENT_FAILURE",
                         GRPC_CHANNEL_TRANSIENT_FAILURE,
                         CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("Grpc\\CHANNEL_FATAL_FAILURE",
                         GRPC_CHANNEL_SHUTDOWN,
                         CONST_CS | CONST_PERSISTENT);

  grpc_init_call(TSRMLS_C);
  grpc_init_channel(TSRMLS_C);
  grpc_init_server(TSRMLS_C);
  grpc_init_timeval(TSRMLS_C);
  grpc_init_channel_credentials(TSRMLS_C);
  grpc_init_call_credentials(TSRMLS_C);
  grpc_init_server_credentials(TSRMLS_C);
  grpc_php_init_completion_queue(TSRMLS_C);
  return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(grpc) {
  /* uncomment this line if you have INI entries
     UNREGISTER_INI_ENTRIES();
  */
  // WARNING: This function IS being called by PHP when the extension
  // is unloaded but the logs were somehow suppressed.
  grpc_shutdown_timeval(TSRMLS_C);
  grpc_php_shutdown_completion_queue(TSRMLS_C);
  grpc_shutdown();
  return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(grpc) {
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
