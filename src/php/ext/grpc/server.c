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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include <ext/spl/spl_exceptions.h>
#include "php_grpc.h"

#include <zend_exceptions.h>

#include <stdbool.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/grpc_security.h>

#include "completion_queue.h"
#include "server.h"
#include "channel.h"
#include "server_credentials.h"
#include "timeval.h"

zend_class_entry *grpc_ce_server;

/* Frees and destroys an instance of wrapped_grpc_server */
void free_wrapped_grpc_server(void *object TSRMLS_DC) {
  wrapped_grpc_server *server = (wrapped_grpc_server *)object;
  if (server->wrapped != NULL) {
    grpc_server_shutdown_and_notify(server->wrapped, completion_queue, NULL);
    grpc_server_cancel_all_calls(server->wrapped);
    grpc_completion_queue_pluck(completion_queue, NULL,
                                gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    grpc_server_destroy(server->wrapped);
  }
  efree(server);
}

/* Initializes an instance of wrapped_grpc_call to be associated with an object
 * of a class specified by class_type */
zend_object_value create_wrapped_grpc_server(zend_class_entry *class_type
                                                 TSRMLS_DC) {
  zend_object_value retval;
  wrapped_grpc_server *intern;

  intern = (wrapped_grpc_server *)emalloc(sizeof(wrapped_grpc_server));
  memset(intern, 0, sizeof(wrapped_grpc_server));

  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  retval.handle = zend_objects_store_put(
      intern, (zend_objects_store_dtor_t)zend_objects_destroy_object,
      free_wrapped_grpc_server, NULL TSRMLS_CC);
  retval.handlers = zend_get_std_object_handlers();
  return retval;
}

/**
 * Constructs a new instance of the Server class
 * @param array $args The arguments to pass to the server (optional)
 */
PHP_METHOD(Server, __construct) {
  wrapped_grpc_server *server =
      (wrapped_grpc_server *)zend_object_store_get_object(getThis() TSRMLS_CC);
  zval *args_array = NULL;
  grpc_channel_args args;
  /* "|a" == 1 optional array */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|a", &args_array) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Server expects an array",
                         1 TSRMLS_CC);
    return;
  }
  if (args_array == NULL) {
    server->wrapped = grpc_server_create(NULL, NULL);
  } else {
    php_grpc_read_args_array(args_array, &args);
    server->wrapped = grpc_server_create(&args, NULL);
    efree(args.args);
  }
  grpc_server_register_completion_queue(server->wrapped, completion_queue,
                                        NULL);
}

/**
 * Request a call on a server. Creates a single GRPC_SERVER_RPC_NEW event.
 * @param long $tag_new The tag to associate with the new request
 * @param long $tag_cancel The tag to use if the call is cancelled
 * @return Void
 */
PHP_METHOD(Server, requestCall) {
  grpc_call_error error_code;
  wrapped_grpc_server *server =
      (wrapped_grpc_server *)zend_object_store_get_object(getThis() TSRMLS_CC);
  grpc_call *call;
  grpc_call_details details;
  grpc_metadata_array metadata;
  zval *result;
  grpc_event event;
  MAKE_STD_ZVAL(result);
  object_init(result);
  grpc_call_details_init(&details);
  grpc_metadata_array_init(&metadata);
  error_code =
      grpc_server_request_call(server->wrapped, &call, &details, &metadata,
                               completion_queue, completion_queue, NULL);
  if (error_code != GRPC_CALL_OK) {
    zend_throw_exception(spl_ce_LogicException, "request_call failed",
                         (long)error_code TSRMLS_CC);
    goto cleanup;
  }
  event = grpc_completion_queue_pluck(completion_queue, NULL,
                                      gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  if (!event.success) {
    zend_throw_exception(spl_ce_LogicException,
                         "Failed to request a call for some reason",
                         1 TSRMLS_CC);
    goto cleanup;
  }
  add_property_zval(result, "call", grpc_php_wrap_call(call, true));
  add_property_string(result, "method", details.method, true);
  add_property_string(result, "host", details.host, true);
  add_property_zval(result, "absolute_deadline",
                    grpc_php_wrap_timeval(details.deadline));
  add_property_zval(result, "metadata", grpc_parse_metadata_array(&metadata));
cleanup:
  grpc_call_details_destroy(&details);
  grpc_metadata_array_destroy(&metadata);
  RETURN_DESTROY_ZVAL(result);
}

/**
 * Add a http2 over tcp listener.
 * @param string $addr The address to add
 * @return true on success, false on failure
 */
PHP_METHOD(Server, addHttp2Port) {
  wrapped_grpc_server *server =
      (wrapped_grpc_server *)zend_object_store_get_object(getThis() TSRMLS_CC);
  const char *addr;
  int addr_len;
  /* "s" == 1 string */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &addr, &addr_len) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "add_http2_port expects a string", 1 TSRMLS_CC);
    return;
  }
  RETURN_LONG(grpc_server_add_insecure_http2_port(server->wrapped, addr));
}

PHP_METHOD(Server, addSecureHttp2Port) {
  wrapped_grpc_server *server =
      (wrapped_grpc_server *)zend_object_store_get_object(getThis() TSRMLS_CC);
  const char *addr;
  int addr_len;
  zval *creds_obj;
  /* "sO" == 1 string, 1 object */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sO", &addr, &addr_len,
                            &creds_obj, grpc_ce_server_credentials) ==
      FAILURE) {
    zend_throw_exception(
        spl_ce_InvalidArgumentException,
        "add_http2_port expects a string and a ServerCredentials", 1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_server_credentials *creds =
      (wrapped_grpc_server_credentials *)zend_object_store_get_object(
          creds_obj TSRMLS_CC);
  RETURN_LONG(grpc_server_add_secure_http2_port(server->wrapped, addr,
                                                creds->wrapped));
}

/**
 * Start a server - tells all listeners to start listening
 * @return Void
 */
PHP_METHOD(Server, start) {
  wrapped_grpc_server *server =
      (wrapped_grpc_server *)zend_object_store_get_object(getThis() TSRMLS_CC);
  grpc_server_start(server->wrapped);
}

static zend_function_entry server_methods[] = {
    PHP_ME(Server, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    PHP_ME(Server, requestCall, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Server, addHttp2Port, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Server, addSecureHttp2Port, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Server, start, NULL, ZEND_ACC_PUBLIC) PHP_FE_END};

void grpc_init_server(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Server", server_methods);
  ce.create_object = create_wrapped_grpc_server;
  grpc_ce_server = zend_register_internal_class(&ce TSRMLS_CC);
}
