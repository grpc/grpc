/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "php_grpc.h"

#include "call.h"
#include "channel.h"
#include "server.h"
#include "timeval.h"
#include "version.h"
#include "channel_credentials.h"
#include "call_credentials.h"
#include "server_credentials.h"
#include "completion_queue.h"
#include <inttypes.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <ext/spl/spl_exceptions.h>
#include <zend_exceptions.h>

#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
#include <pthread.h>
#endif

ZEND_DECLARE_MODULE_GLOBALS(grpc)
static PHP_GINIT_FUNCTION(grpc);
HashTable grpc_persistent_list;
HashTable grpc_target_upper_bound_map;
/* {{{ grpc_functions[]
 *
 * Every user visible function must have an entry in grpc_functions[].
 */
const zend_function_entry grpc_functions[] = {
    PHP_FE_END /* Must be the last line in grpc_functions[] */
};
/* }}} */

ZEND_DECLARE_MODULE_GLOBALS(grpc);

/* {{{ grpc_module_entry
 */
zend_module_entry grpc_module_entry = {
  STANDARD_MODULE_HEADER,
  "grpc",
  grpc_functions,
  PHP_MINIT(grpc),
  PHP_MSHUTDOWN(grpc),
  PHP_RINIT(grpc),
  NULL,
  PHP_MINFO(grpc),
  PHP_GRPC_VERSION,
  PHP_MODULE_GLOBALS(grpc),
  PHP_GINIT(grpc),
  NULL,
  NULL,
  STANDARD_MODULE_PROPERTIES_EX};
/* }}} */

#ifdef COMPILE_DL_GRPC
ZEND_GET_MODULE(grpc)
#endif

/* {{{ PHP_INI
 */
   PHP_INI_BEGIN()
   STD_PHP_INI_ENTRY("grpc.enable_fork_support", "0", PHP_INI_SYSTEM, OnUpdateBool,
                     enable_fork_support, zend_grpc_globals, grpc_globals)
   STD_PHP_INI_ENTRY("grpc.poll_strategy", NULL, PHP_INI_SYSTEM, OnUpdateString,
                     poll_strategy, zend_grpc_globals, grpc_globals)
   STD_PHP_INI_ENTRY("grpc.grpc_verbosity", NULL, PHP_INI_SYSTEM, OnUpdateString,
                     grpc_verbosity, zend_grpc_globals, grpc_globals)
   STD_PHP_INI_ENTRY("grpc.grpc_trace", NULL, PHP_INI_SYSTEM, OnUpdateString,
                     grpc_trace, zend_grpc_globals, grpc_globals)
   STD_PHP_INI_ENTRY("grpc.log_filename", NULL, PHP_INI_SYSTEM, OnUpdateString,
                     log_filename, zend_grpc_globals, grpc_globals)
   PHP_INI_END()
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

void create_new_channel(
    wrapped_grpc_channel *channel,
    char *target,
    grpc_channel_args args,
    wrapped_grpc_channel_credentials *creds) {
  if (creds == NULL) {
    grpc_channel_credentials *insecure_creds = grpc_insecure_credentials_create();
    channel->wrapper->wrapped = grpc_channel_create(target, insecure_creds, &args);
    grpc_channel_credentials_release(insecure_creds);
  } else {
    channel->wrapper->wrapped =
        grpc_channel_create(target, creds->wrapped, &args);
  }
}

void acquire_persistent_locks() {
  zval *data;
  PHP_GRPC_HASH_FOREACH_VAL_START(&grpc_persistent_list, data)
    php_grpc_zend_resource *rsrc  =
                (php_grpc_zend_resource*) PHP_GRPC_HASH_VALPTR_TO_VAL(data)
    if (rsrc == NULL) {
      break;
    }
    channel_persistent_le_t* le = rsrc->ptr;

    gpr_mu_lock(&le->channel->mu);
  PHP_GRPC_HASH_FOREACH_END()
}

void release_persistent_locks() {
  zval *data;
  PHP_GRPC_HASH_FOREACH_VAL_START(&grpc_persistent_list, data)
    php_grpc_zend_resource *rsrc  =
                (php_grpc_zend_resource*) PHP_GRPC_HASH_VALPTR_TO_VAL(data)
    if (rsrc == NULL) {
      break;
    }
    channel_persistent_le_t* le = rsrc->ptr;

    gpr_mu_unlock(&le->channel->mu);
  PHP_GRPC_HASH_FOREACH_END()
}

void destroy_grpc_channels() {
  zval *data;
  PHP_GRPC_HASH_FOREACH_VAL_START(&grpc_persistent_list, data)
    php_grpc_zend_resource *rsrc  =
                (php_grpc_zend_resource*) PHP_GRPC_HASH_VALPTR_TO_VAL(data)
    if (rsrc == NULL) {
      break;
    }
    channel_persistent_le_t* le = rsrc->ptr;

    wrapped_grpc_channel wrapped_channel;
    wrapped_channel.wrapper = le->channel;
    grpc_channel_wrapper *channel = wrapped_channel.wrapper;
    grpc_channel_destroy(channel->wrapped);
    channel->wrapped = NULL;
  PHP_GRPC_HASH_FOREACH_END()
}

void prefork() {
  acquire_persistent_locks();
}

// Clean all channels in the persistent list
// Called at post fork
void php_grpc_clean_persistent_list(TSRMLS_D) {
    zend_hash_clean(&grpc_persistent_list);
    zend_hash_clean(&grpc_target_upper_bound_map);
}

void postfork_child() {
  TSRMLS_FETCH();

  // loop through persistent list and destroy all underlying grpc_channel objs
  destroy_grpc_channels();

  release_persistent_locks();
  
  // clean all channels in the persistent list
  php_grpc_clean_persistent_list(TSRMLS_C);

  // clear completion queue
  grpc_php_shutdown_completion_queue(TSRMLS_C);

  // clean-up grpc_core
  grpc_shutdown();
  if (grpc_is_initialized() > 0) {
    zend_throw_exception(spl_ce_UnexpectedValueException,
                         "Oops, failed to shutdown gRPC Core after fork()",
                         1 TSRMLS_CC);
  }

  // restart grpc_core
  grpc_init();
  grpc_php_init_completion_queue(TSRMLS_C);
}

void postfork_parent() {
  release_persistent_locks();
}

void register_fork_handlers() {
  if (getenv("GRPC_ENABLE_FORK_SUPPORT")) {
#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
    pthread_atfork(&prefork, &postfork_parent, &postfork_child);
#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  }
}

void apply_ini_settings(TSRMLS_D) {
  if (GRPC_G(enable_fork_support)) {
    char *enable_str = malloc(sizeof("GRPC_ENABLE_FORK_SUPPORT=1"));
    strcpy(enable_str, "GRPC_ENABLE_FORK_SUPPORT=1");
    putenv(enable_str);
  }

  if (GRPC_G(poll_strategy)) {
    char *poll_str = malloc(sizeof("GRPC_POLL_STRATEGY=") +
                            strlen(GRPC_G(poll_strategy)));
    strcpy(poll_str, "GRPC_POLL_STRATEGY=");
    strcat(poll_str, GRPC_G(poll_strategy));
    putenv(poll_str);
  }

  if (GRPC_G(grpc_verbosity)) {
    char *verbosity_str = malloc(sizeof("GRPC_VERBOSITY=") +
                                 strlen(GRPC_G(grpc_verbosity)));
    strcpy(verbosity_str, "GRPC_VERBOSITY=");
    strcat(verbosity_str, GRPC_G(grpc_verbosity));
    putenv(verbosity_str);
  }

  if (GRPC_G(grpc_trace)) {
    char *trace_str = malloc(sizeof("GRPC_TRACE=") +
                             strlen(GRPC_G(grpc_trace)));
    strcpy(trace_str, "GRPC_TRACE=");
    strcat(trace_str, GRPC_G(grpc_trace));
    putenv(trace_str);
  }
}

static void custom_logger(gpr_log_func_args* args) {
  TSRMLS_FETCH();

  const char* final_slash;
  const char* display_file;
  char* prefix;
  char* final;
  gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);

  final_slash = strrchr(args->file, '/');
  if (final_slash) {
    display_file = final_slash + 1;
  } else {
    display_file = args->file;
  }

  FILE *fp = fopen(GRPC_G(log_filename), "ab");
  if (!fp) {
    return;
  }

  gpr_asprintf(&prefix, "%s%" PRId64 ".%09" PRId32 " %s:%d]",
               gpr_log_severity_string(args->severity), now.tv_sec,
               now.tv_nsec, display_file, args->line);

  gpr_asprintf(&final, "%-60s %s\n", prefix, args->message);

  fprintf(fp, "%s", final);
  fclose(fp);
  gpr_free(prefix);
  gpr_free(final);
}

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(grpc) {
  REGISTER_INI_ENTRIES();

  /* Register call error constants */
  /** everything went ok */
  REGISTER_LONG_CONSTANT("Grpc\\CALL_OK", GRPC_CALL_OK,
                         CONST_CS | CONST_PERSISTENT);
  /** something failed, we don't know what */
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR", GRPC_CALL_ERROR,
                         CONST_CS | CONST_PERSISTENT);
  /** this method is not available on the server */
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_NOT_ON_SERVER",
                         GRPC_CALL_ERROR_NOT_ON_SERVER,
                         CONST_CS | CONST_PERSISTENT);
  /** this method is not available on the client */
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_NOT_ON_CLIENT",
                         GRPC_CALL_ERROR_NOT_ON_CLIENT,
                         CONST_CS | CONST_PERSISTENT);
  /** this method must be called before invoke */
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_ALREADY_INVOKED",
                         GRPC_CALL_ERROR_ALREADY_INVOKED,
                         CONST_CS | CONST_PERSISTENT);
  /** this method must be called after invoke */
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_NOT_INVOKED",
                         GRPC_CALL_ERROR_NOT_INVOKED,
                         CONST_CS | CONST_PERSISTENT);
  /** this call is already finished
      (writes_done or write_status has already been called) */
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_ALREADY_FINISHED",
                         GRPC_CALL_ERROR_ALREADY_FINISHED,
                         CONST_CS | CONST_PERSISTENT);
  /** there is already an outstanding read/write operation on the call */
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_TOO_MANY_OPERATIONS",
                         GRPC_CALL_ERROR_TOO_MANY_OPERATIONS,
                         CONST_CS | CONST_PERSISTENT);
  /** the flags value was illegal for this call */
  REGISTER_LONG_CONSTANT("Grpc\\CALL_ERROR_INVALID_FLAGS",
                         GRPC_CALL_ERROR_INVALID_FLAGS,
                         CONST_CS | CONST_PERSISTENT);

  /* Register flag constants */
  /** Hint that the write may be buffered and need not go out on the wire
      immediately. GRPC is free to buffer the message until the next non-buffered
      write, or until writes_done, but it need not buffer completely or at all. */
  REGISTER_LONG_CONSTANT("Grpc\\WRITE_BUFFER_HINT", GRPC_WRITE_BUFFER_HINT,
                         CONST_CS | CONST_PERSISTENT);
  /** Force compression to be disabled for a particular write
      (start_write/add_metadata). Illegal on invoke/accept. */
  REGISTER_LONG_CONSTANT("Grpc\\WRITE_NO_COMPRESS", GRPC_WRITE_NO_COMPRESS,
                         CONST_CS | CONST_PERSISTENT);

  /* Register status constants */
  /** Not an error; returned on success */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_OK", GRPC_STATUS_OK,
                         CONST_CS | CONST_PERSISTENT);
  /** The operation was cancelled (typically by the caller). */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_CANCELLED", GRPC_STATUS_CANCELLED,
                         CONST_CS | CONST_PERSISTENT);
  /** Unknown error.  An example of where this error may be returned is
      if a Status value received from another address space belongs to
      an error-space that is not known in this address space.  Also
      errors raised by APIs that do not return enough error information
      may be converted to this error. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNKNOWN", GRPC_STATUS_UNKNOWN,
                         CONST_CS | CONST_PERSISTENT);
  /** Client specified an invalid argument.  Note that this differs
      from FAILED_PRECONDITION.  INVALID_ARGUMENT indicates arguments
      that are problematic regardless of the state of the system
      (e.g., a malformed file name). */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_INVALID_ARGUMENT",
                         GRPC_STATUS_INVALID_ARGUMENT,
                         CONST_CS | CONST_PERSISTENT);
  /** Deadline expired before operation could complete.  For operations
      that change the state of the system, this error may be returned
      even if the operation has completed successfully.  For example, a
      successful response from a server could have been delayed long
      enough for the deadline to expire. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_DEADLINE_EXCEEDED",
                         GRPC_STATUS_DEADLINE_EXCEEDED,
                         CONST_CS | CONST_PERSISTENT);
  /** Some requested entity (e.g., file or directory) was not found. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_NOT_FOUND", GRPC_STATUS_NOT_FOUND,
                         CONST_CS | CONST_PERSISTENT);
  /** Some entity that we attempted to create (e.g., file or directory)
      already exists. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_ALREADY_EXISTS",
                         GRPC_STATUS_ALREADY_EXISTS,
                         CONST_CS | CONST_PERSISTENT);
  /** The caller does not have permission to execute the specified
      operation.  PERMISSION_DENIED must not be used for rejections
      caused by exhausting some resource (use RESOURCE_EXHAUSTED
      instead for those errors).  PERMISSION_DENIED must not be
      used if the caller can not be identified (use UNAUTHENTICATED
      instead for those errors). */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_PERMISSION_DENIED",
                         GRPC_STATUS_PERMISSION_DENIED,
                         CONST_CS | CONST_PERSISTENT);
  /** The request does not have valid authentication credentials for the
      operation. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNAUTHENTICATED",
                         GRPC_STATUS_UNAUTHENTICATED,
                         CONST_CS | CONST_PERSISTENT);
  /** Some resource has been exhausted, perhaps a per-user quota, or
      perhaps the entire file system is out of space. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_RESOURCE_EXHAUSTED",
                         GRPC_STATUS_RESOURCE_EXHAUSTED,
                         CONST_CS | CONST_PERSISTENT);
  /** Operation was rejected because the system is not in a state
      required for the operation's execution.  For example, directory
      to be deleted may be non-empty, an rmdir operation is applied to
      a non-directory, etc.

      A litmus test that may help a service implementor in deciding
      between FAILED_PRECONDITION, ABORTED, and UNAVAILABLE:
       (a) Use UNAVAILABLE if the client can retry just the failing call.
       (b) Use ABORTED if the client should retry at a higher-level
           (e.g., restarting a read-modify-write sequence).
       (c) Use FAILED_PRECONDITION if the client should not retry until
           the system state has been explicitly fixed.  E.g., if an "rmdir"
           fails because the directory is non-empty, FAILED_PRECONDITION
           should be returned since the client should not retry unless
           they have first fixed up the directory by deleting files from it.
       (d) Use FAILED_PRECONDITION if the client performs conditional
           REST Get/Update/Delete on a resource and the resource on the
           server does not match the condition. E.g., conflicting
           read-modify-write on the same resource. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_FAILED_PRECONDITION",
                         GRPC_STATUS_FAILED_PRECONDITION,
                         CONST_CS | CONST_PERSISTENT);
  /** The operation was aborted, typically due to a concurrency issue
      like sequencer check failures, transaction aborts, etc.

      See litmus test above for deciding between FAILED_PRECONDITION,
      ABORTED, and UNAVAILABLE. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_ABORTED", GRPC_STATUS_ABORTED,
                         CONST_CS | CONST_PERSISTENT);
  /** Operation was attempted past the valid range.  E.g., seeking or
      reading past end of file.

      Unlike INVALID_ARGUMENT, this error indicates a problem that may
      be fixed if the system state changes. For example, a 32-bit file
      system will generate INVALID_ARGUMENT if asked to read at an
      offset that is not in the range [0,2^32-1], but it will generate
      OUT_OF_RANGE if asked to read from an offset past the current
      file size.

      There is a fair bit of overlap between FAILED_PRECONDITION and
      OUT_OF_RANGE.  We recommend using OUT_OF_RANGE (the more specific
      error) when it applies so that callers who are iterating through
      a space can easily look for an OUT_OF_RANGE error to detect when
      they are done. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_OUT_OF_RANGE",
                         GRPC_STATUS_OUT_OF_RANGE,
                         CONST_CS | CONST_PERSISTENT);
  /** Operation is not implemented or not supported/enabled in this service. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNIMPLEMENTED",
                         GRPC_STATUS_UNIMPLEMENTED,
                         CONST_CS | CONST_PERSISTENT);
  /** Internal errors.  Means some invariants expected by underlying
      system has been broken.  If you see one of these errors,
      something is very broken. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_INTERNAL", GRPC_STATUS_INTERNAL,
                         CONST_CS | CONST_PERSISTENT);
  /** The service is currently unavailable.  This is a most likely a
      transient condition and may be corrected by retrying with
      a backoff. Note that it is not always safe to retry non-idempotent
      operations.

      WARNING: Although data MIGHT not have been transmitted when this
      status occurs, there is NOT A GUARANTEE that the server has not seen
      anything. So in general it is unsafe to retry on this status code
      if the call is non-idempotent.

      See litmus test above for deciding between FAILED_PRECONDITION,
      ABORTED, and UNAVAILABLE. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_UNAVAILABLE", GRPC_STATUS_UNAVAILABLE,
                         CONST_CS | CONST_PERSISTENT);
  /** Unrecoverable data loss or corruption. */
  REGISTER_LONG_CONSTANT("Grpc\\STATUS_DATA_LOSS", GRPC_STATUS_DATA_LOSS,
                         CONST_CS | CONST_PERSISTENT);

  /* Register op type constants */
  /** Send initial metadata: one and only one instance MUST be sent for each
      call, unless the call was cancelled - in which case this can be skipped.
      This op completes after all bytes of metadata have been accepted by
      outgoing flow control. */
  REGISTER_LONG_CONSTANT("Grpc\\OP_SEND_INITIAL_METADATA",
                         GRPC_OP_SEND_INITIAL_METADATA,
                         CONST_CS | CONST_PERSISTENT);
  /** Send a message: 0 or more of these operations can occur for each call.
      This op completes after all bytes for the message have been accepted by
      outgoing flow control. */
  REGISTER_LONG_CONSTANT("Grpc\\OP_SEND_MESSAGE",
                         GRPC_OP_SEND_MESSAGE,
                         CONST_CS | CONST_PERSISTENT);
  /** Send a close from the client: one and only one instance MUST be sent from
      the client, unless the call was cancelled - in which case this can be
      skipped. This op completes after all bytes for the call
      (including the close) have passed outgoing flow control. */
  REGISTER_LONG_CONSTANT("Grpc\\OP_SEND_CLOSE_FROM_CLIENT",
                         GRPC_OP_SEND_CLOSE_FROM_CLIENT,
                         CONST_CS | CONST_PERSISTENT);
  /** Send status from the server: one and only one instance MUST be sent from
      the server unless the call was cancelled - in which case this can be
      skipped. This op completes after all bytes for the call
      (including the status) have passed outgoing flow control. */
  REGISTER_LONG_CONSTANT("Grpc\\OP_SEND_STATUS_FROM_SERVER",
                         GRPC_OP_SEND_STATUS_FROM_SERVER,
                         CONST_CS | CONST_PERSISTENT);
  /** Receive initial metadata: one and only one MUST be made on the client,
      must not be made on the server.
      This op completes after all initial metadata has been read from the
      peer. */
  REGISTER_LONG_CONSTANT("Grpc\\OP_RECV_INITIAL_METADATA",
                         GRPC_OP_RECV_INITIAL_METADATA,
                         CONST_CS | CONST_PERSISTENT);
  /** Receive a message: 0 or more of these operations can occur for each call.
      This op completes after all bytes of the received message have been
      read, or after a half-close has been received on this call. */
  REGISTER_LONG_CONSTANT("Grpc\\OP_RECV_MESSAGE",
                         GRPC_OP_RECV_MESSAGE,
                         CONST_CS | CONST_PERSISTENT);
  /** Receive status on the client: one and only one must be made on the client.
      This operation always succeeds, meaning ops paired with this operation
      will also appear to succeed, even though they may not have. In that case
      the status will indicate some failure.
      This op completes after all activity on the call has completed. */
  REGISTER_LONG_CONSTANT("Grpc\\OP_RECV_STATUS_ON_CLIENT",
                         GRPC_OP_RECV_STATUS_ON_CLIENT,
                         CONST_CS | CONST_PERSISTENT);
  /** Receive close on the server: one and only one must be made on the
      server. This op completes after the close has been received by the
      server. This operation always succeeds, meaning ops paired with
      this operation will also appear to succeed, even though they may not
      have. */
  REGISTER_LONG_CONSTANT("Grpc\\OP_RECV_CLOSE_ON_SERVER",
                         GRPC_OP_RECV_CLOSE_ON_SERVER,
                         CONST_CS | CONST_PERSISTENT);

  /* Register connectivity state constants */
  /** channel is idle */
  REGISTER_LONG_CONSTANT("Grpc\\CHANNEL_IDLE",
                         GRPC_CHANNEL_IDLE,
                         CONST_CS | CONST_PERSISTENT);
  /** channel is connecting */
  REGISTER_LONG_CONSTANT("Grpc\\CHANNEL_CONNECTING",
                         GRPC_CHANNEL_CONNECTING,
                         CONST_CS | CONST_PERSISTENT);
  /** channel is ready for work */
  REGISTER_LONG_CONSTANT("Grpc\\CHANNEL_READY",
                         GRPC_CHANNEL_READY,
                         CONST_CS | CONST_PERSISTENT);
  /** channel has seen a failure but expects to recover */
  REGISTER_LONG_CONSTANT("Grpc\\CHANNEL_TRANSIENT_FAILURE",
                         GRPC_CHANNEL_TRANSIENT_FAILURE,
                         CONST_CS | CONST_PERSISTENT);
  /** channel has seen a failure that it cannot recover from */
  REGISTER_LONG_CONSTANT("Grpc\\CHANNEL_FATAL_FAILURE",
                         GRPC_CHANNEL_SHUTDOWN,
                         CONST_CS | CONST_PERSISTENT);

  /** grpc version string */
  REGISTER_STRING_CONSTANT("Grpc\\VERSION", PHP_GRPC_VERSION,
                           CONST_CS | CONST_PERSISTENT);
  
  grpc_init_call(TSRMLS_C);
  GRPC_STARTUP(channel);
  grpc_init_server(TSRMLS_C);
  grpc_init_timeval(TSRMLS_C);
  grpc_init_channel_credentials(TSRMLS_C);
  grpc_init_call_credentials(TSRMLS_C);
  grpc_init_server_credentials(TSRMLS_C);
  return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(grpc) {
  UNREGISTER_INI_ENTRIES();
  // WARNING: This function IS being called by PHP when the extension
  // is unloaded but the logs were somehow suppressed.
  if (GRPC_G(initialized)) {
    zend_hash_clean(&grpc_persistent_list);
    zend_hash_destroy(&grpc_persistent_list);
    zend_hash_clean(&grpc_target_upper_bound_map);
    zend_hash_destroy(&grpc_target_upper_bound_map);
    grpc_shutdown_timeval(TSRMLS_C);
    grpc_php_shutdown_completion_queue(TSRMLS_C);
    grpc_shutdown();
    GRPC_G(initialized) = 0;
  }
  return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(grpc) {
  php_info_print_table_start();
  php_info_print_table_row(2, "grpc support", "enabled");
  php_info_print_table_row(2, "grpc module version", PHP_GRPC_VERSION);
  php_info_print_table_end();
  DISPLAY_INI_ENTRIES();
}
/* }}} */

/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(grpc) {
  if (!GRPC_G(initialized)) {
    apply_ini_settings(TSRMLS_C);
    if (GRPC_G(log_filename)) {
      gpr_set_log_function(custom_logger);
    }
    grpc_init();
    register_fork_handlers();
    grpc_php_init_completion_queue(TSRMLS_C);
    GRPC_G(initialized) = 1;
  }
  return SUCCESS;
}
/* }}} */

/* {{{ PHP_GINIT_FUNCTION
 */
static PHP_GINIT_FUNCTION(grpc) {
  grpc_globals->initialized = 0;
  grpc_globals->enable_fork_support = 0;
  grpc_globals->poll_strategy = NULL;
  grpc_globals->grpc_verbosity = NULL;
  grpc_globals->grpc_trace = NULL;
  grpc_globals->log_filename = NULL;
}
/* }}} */

/* The previous line is meant for vim and emacs, so it can correctly fold and
   unfold functions in source code. See the corresponding marks just before
   function definition, where the functions purpose is also documented. Please
   follow this convention for the convenience of others editing your code.
*/
