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

/**
 * class Server
 * @see https://github.com/grpc/grpc/tree/master/src/php/ext/grpc/server.c
 */

#include "server.h"

#include <ext/spl/spl_exceptions.h>
#include <zend_exceptions.h>

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>

#include "call.h"
#include "completion_queue.h"
#include "channel.h"
#include "server_credentials.h"
#include "timeval.h"

zend_class_entry *grpc_ce_server;
PHP_GRPC_DECLARE_OBJECT_HANDLER(server_ce_handlers)

/* Frees and destroys an instance of wrapped_grpc_server */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_server)
  if (p->wrapped != NULL) {
    grpc_server_shutdown_and_notify(p->wrapped, completion_queue, NULL);
    grpc_server_cancel_all_calls(p->wrapped);
    grpc_completion_queue_pluck(completion_queue, NULL,
                                gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    grpc_server_destroy(p->wrapped);
  }
PHP_GRPC_FREE_WRAPPED_FUNC_END()

/* Initializes an instance of wrapped_grpc_call to be associated with an
 * object of a class specified by class_type */
php_grpc_zend_object create_wrapped_grpc_server(zend_class_entry *class_type
                                                TSRMLS_DC) {
  PHP_GRPC_ALLOC_CLASS_OBJECT(wrapped_grpc_server);
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  PHP_GRPC_FREE_CLASS_OBJECT(wrapped_grpc_server, server_ce_handlers);
}

/**
 * Constructs a new instance of the Server class
 * @param array $args_array The arguments to pass to the server (optional)
 */
PHP_METHOD(Server, __construct) {
  wrapped_grpc_server *server =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_server, getThis());
  zval *args_array = NULL;
  grpc_channel_args args;

  /* "|a" == 1 optional array */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|a", &args_array) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Server expects an array", 1 TSRMLS_CC);
    return;
  }
  if (args_array == NULL) {
    server->wrapped = grpc_server_create(NULL, NULL);
  } else {
    if (php_grpc_read_args_array(args_array, &args TSRMLS_CC) == FAILURE) {
      efree(args.args);
      return;
    }
    server->wrapped = grpc_server_create(&args, NULL);
    efree(args.args);
  }
  grpc_server_register_completion_queue(server->wrapped, completion_queue,
                                        NULL);
}

/**
 * Request a call on a server. Creates a single GRPC_SERVER_RPC_NEW event.
 * @return void
 */
PHP_METHOD(Server, requestCall) {
  grpc_call_error error_code;
  grpc_call *call;
  grpc_call_details details;
  grpc_metadata_array metadata;
  grpc_event event;

  wrapped_grpc_server *server =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_server, getThis());
  zval *result;
  PHP_GRPC_MAKE_STD_ZVAL(result);
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
                                      gpr_inf_future(GPR_CLOCK_REALTIME),
                                      NULL);
  if (!event.success) {
    zend_throw_exception(spl_ce_LogicException,
                         "Failed to request a call for some reason",
                         1 TSRMLS_CC);
    goto cleanup;
  }
  char *method_text = grpc_slice_to_c_string(details.method);
  char *host_text = grpc_slice_to_c_string(details.host);
  php_grpc_add_property_string(result, "method", method_text, true);
  php_grpc_add_property_string(result, "host", host_text, true);
  gpr_free(method_text);
  gpr_free(host_text);
  php_grpc_add_property_zval(result, "call",
                             grpc_php_wrap_call(call, true TSRMLS_CC));
  php_grpc_add_property_zval(result, "absolute_deadline",
                             grpc_php_wrap_timeval(details.deadline TSRMLS_CC));
  php_grpc_add_property_zval(result, "metadata",
                             grpc_parse_metadata_array(&metadata TSRMLS_CC));

 cleanup:
  grpc_call_details_destroy(&details);
  grpc_metadata_array_destroy(&metadata);
  RETURN_DESTROY_ZVAL(result);
}

/**
 * Add a http2 over tcp listener.
 * @param string $addr The address to add
 * @return int Port on success, 0 on failure
 */
PHP_METHOD(Server, addHttp2Port) {
  const char *addr;
  php_grpc_int addr_len;
  wrapped_grpc_server *server =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_server, getThis());

  /* "s" == 1 string */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &addr, &addr_len)
      == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "add_http2_port expects a string", 1 TSRMLS_CC);
    return;
  }
  grpc_server_credentials *creds = grpc_insecure_server_credentials_create();
  int result = grpc_server_add_http2_port(server->wrapped, addr, creds);
  grpc_server_credentials_release(creds);
  RETURN_LONG(result);
}

/**
 * Add a secure http2 over tcp listener.
 * @param string $addr The address to add
 * @param ServerCredentials The ServerCredentials object
 * @return int Port on success, 0 on failure
 */
PHP_METHOD(Server, addSecureHttp2Port) {
  const char *addr;
  php_grpc_int addr_len;
  zval *creds_obj;
  wrapped_grpc_server *server =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_server, getThis());

  /* "sO" == 1 string, 1 object */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sO", &addr, &addr_len,
                            &creds_obj, grpc_ce_server_credentials) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "add_http2_port expects a string and a "
                         "ServerCredentials", 1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_server_credentials *creds =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_server_credentials, creds_obj);
  RETURN_LONG(grpc_server_add_http2_port(server->wrapped, addr,
                                                creds->wrapped));
}

/**
 * Start a server - tells all listeners to start listening
 * @return void
 */
PHP_METHOD(Server, start) {
  wrapped_grpc_server *server =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_server, getThis());
  grpc_server_start(server->wrapped);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_construct, 0, 0, 0)
  ZEND_ARG_INFO(0, args)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_requestCall, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_addHttp2Port, 0, 0, 1)
  ZEND_ARG_INFO(0, addr)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_addSecureHttp2Port, 0, 0, 2)
  ZEND_ARG_INFO(0, addr)
  ZEND_ARG_INFO(0, server_creds)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_start, 0, 0, 0)
ZEND_END_ARG_INFO()

static zend_function_entry server_methods[] = {
  PHP_ME(Server, __construct, arginfo_construct,
         ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
  PHP_ME(Server, requestCall, arginfo_requestCall,
         ZEND_ACC_PUBLIC)
  PHP_ME(Server, addHttp2Port, arginfo_addHttp2Port,
         ZEND_ACC_PUBLIC)
  PHP_ME(Server, addSecureHttp2Port, arginfo_addSecureHttp2Port,
         ZEND_ACC_PUBLIC)
  PHP_ME(Server, start, arginfo_start,
         ZEND_ACC_PUBLIC)
  PHP_FE_END
};

void grpc_init_server(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Server", server_methods);
  ce.create_object = create_wrapped_grpc_server;
  grpc_ce_server = zend_register_internal_class(&ce TSRMLS_CC);
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_server, server_ce_handlers);
}
