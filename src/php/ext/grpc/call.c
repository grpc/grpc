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
 * class Call
 * @see https://github.com/grpc/grpc/tree/master/src/php/ext/grpc/call.c
 */

#include "call.h"

#include <ext/spl/spl_exceptions.h>
#include <zend_exceptions.h>

#include <grpc/support/alloc.h>

#include "batch.h"
#include "call_credentials.h"
#include "channel.h"
#include "completion_queue.h"
#include "timeval.h"

zend_class_entry *grpc_ce_call;
PHP_GRPC_DECLARE_OBJECT_HANDLER(call_ce_handlers)

/* Frees and destroys an instance of wrapped_grpc_call */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_call)
  if (p->owned && p->wrapped != NULL) {
    grpc_call_unref(p->wrapped);
  }
PHP_GRPC_FREE_WRAPPED_FUNC_END()

/* Initializes an instance of wrapped_grpc_call to be associated with an
 * object of a class specified by class_type */
php_grpc_zend_object create_wrapped_grpc_call(zend_class_entry *class_type
                                              TSRMLS_DC) {
  PHP_GRPC_ALLOC_CLASS_OBJECT(wrapped_grpc_call);
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  PHP_GRPC_FREE_CLASS_OBJECT(wrapped_grpc_call, call_ce_handlers);
}

/* Wraps a grpc_call struct in a PHP object. Owned indicates whether the
   struct should be destroyed at the end of the object's lifecycle */
zval *grpc_php_wrap_call(grpc_call *wrapped, bool owned TSRMLS_DC) {
  zval *call_object;
  PHP_GRPC_MAKE_STD_ZVAL(call_object);
  object_init_ex(call_object, grpc_ce_call);
  wrapped_grpc_call *call = PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call,
                                                        call_object);
  call->wrapped = wrapped;
  call->owned = owned;
  return call_object;
}

/**
 * Constructs a new instance of the Call class.
 * @param Channel $channel_obj The channel to associate the call with.
 *                             Must not be closed.
 * @param string $method The method to call
 * @param Timeval $deadline_obj The deadline for completing the call
 * @param string $host_override = "" The host is set by user (optional)
 */
PHP_METHOD(Call, __construct) {
  zval *channel_obj;
  char *method;
  php_grpc_int method_len;
  zval *deadline_obj;
  char *host_override = NULL;
  php_grpc_int host_override_len = 0;
  wrapped_grpc_call *call = PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call,
                                                        getThis());

  /* "OsO|s" == 1 Object, 1 string, 1 Object, 1 optional string */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "OsO|s", &channel_obj,
                            grpc_ce_channel, &method, &method_len,
                            &deadline_obj, grpc_ce_timeval, &host_override,
                            &host_override_len) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Call expects a Channel, a String, a Timeval and "
                         "an optional String", 1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_channel *channel =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel, channel_obj);
  if (channel->wrapper == NULL) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Call cannot be constructed from a closed Channel",
                         1 TSRMLS_CC);
    return;
  }
  gpr_mu_lock(&channel->wrapper->mu);
  if (channel->wrapper == NULL || channel->wrapper->wrapped == NULL) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Call cannot be constructed from a closed Channel",
                         1 TSRMLS_CC);
    gpr_mu_unlock(&channel->wrapper->mu);
    return;
  }
  add_property_zval(getThis(), "channel", channel_obj);
  wrapped_grpc_timeval *deadline =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_timeval, deadline_obj);
  grpc_slice method_slice = grpc_slice_from_copied_string(method);
  grpc_slice host_slice = host_override != NULL ?
      grpc_slice_from_copied_string(host_override) : grpc_empty_slice();
  call->wrapped =
    grpc_channel_create_call(channel->wrapper->wrapped, NULL,
                             GRPC_PROPAGATE_DEFAULTS,
                             completion_queue, method_slice,
                             host_override != NULL ? &host_slice : NULL,
                             deadline->wrapped, NULL);
  grpc_slice_unref(method_slice);
  grpc_slice_unref(host_slice);
  call->owned = true;
  call->channel = channel;
  gpr_mu_unlock(&channel->wrapper->mu);
}

/**
 * Start a batch of RPC actions.
 * @param array $array Array of actions to take
 * @return object Object with results of all actions
 */
PHP_METHOD(Call, startBatch) {
  zval* result = NULL;

  wrapped_grpc_call *call = PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call,
                                                        getThis());
  if (call->channel) {
    // startBatch in gRPC PHP server doesn't have channel in it.
    if (call->channel->wrapper == NULL ||
        call->channel->wrapper->wrapped == NULL) {
      zend_throw_exception(spl_ce_RuntimeException,
                           "startBatch Error. Channel is closed",
                           1 TSRMLS_CC);
    }
  }

  zval *array;
  struct batch batch = {{{0}}};

  grpc_call_error error;

    /* "a" == 1 array */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &array) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "start_batch expects an array", 1 TSRMLS_CC);
    return;
  }

  // c-core may call rand(). If we don't call srand() here, all the
  // random numbers being returned would be the same.
  gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  srand(now.tv_nsec);

  batch_init(&batch);

  if (!batch_populate_ops(&batch, Z_ARRVAL_P(array))) {
    batch_destroy(&batch);
    return;
  }

  error = grpc_call_start_batch(call->wrapped, batch.ops, batch.op_num, call->wrapped,
                                NULL);
  if (error != GRPC_CALL_OK) {
    zend_throw_exception(spl_ce_LogicException,
                         "start_batch was called incorrectly",
                         (long)error TSRMLS_CC);
    batch_destroy(&batch);
    return;
  }

  grpc_completion_queue_pluck(completion_queue, call->wrapped,
                              gpr_inf_future(GPR_CLOCK_REALTIME), NULL);

  result = batch_process_ops(&batch);

  batch_destroy(&batch);
  RETURN_DESTROY_ZVAL(result);
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
PHP_METHOD(Call, getPeer) {
  wrapped_grpc_call *call = PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call,
                                                        getThis());
  char *peer = grpc_call_get_peer(call->wrapped);
  PHP_GRPC_RETVAL_STRING(peer, 1);
  gpr_free(peer);
}

/**
 * Cancel the call. This will cause the call to end with STATUS_CANCELLED
 * if it has not already ended with another status.
 * @return void
 */
PHP_METHOD(Call, cancel) {
  wrapped_grpc_call *call = PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call,
                                                        getThis());
  grpc_call_cancel(call->wrapped, NULL);
}

/**
 * Set the CallCredentials for this call.
 * @param CallCredentials $creds_obj The CallCredentials object
 * @return int The error code
 */
PHP_METHOD(Call, setCredentials) {
  zval *creds_obj;

  /* "O" == 1 Object */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &creds_obj,
                            grpc_ce_call_credentials) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "setCredentials expects 1 CallCredentials",
                         1 TSRMLS_CC);
    return;
  }

  wrapped_grpc_call_credentials *creds =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call_credentials, creds_obj);
  wrapped_grpc_call *call = PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call,
                                                        getThis());

  grpc_call_error error = GRPC_CALL_ERROR;
  error = grpc_call_set_credentials(call->wrapped, creds->wrapped);
  RETURN_LONG(error);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_construct, 0, 0, 3)
  ZEND_ARG_INFO(0, channel)
  ZEND_ARG_INFO(0, method)
  ZEND_ARG_INFO(0, deadline)
  ZEND_ARG_INFO(0, host_override)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_startBatch, 0, 0, 1)
  ZEND_ARG_INFO(0, ops)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_getPeer, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cancel, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_setCredentials, 0, 0, 1)
  ZEND_ARG_INFO(0, credentials)
ZEND_END_ARG_INFO()

static zend_function_entry call_methods[] = {
  PHP_ME(Call, __construct, arginfo_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
  PHP_ME(Call, startBatch, arginfo_startBatch, ZEND_ACC_PUBLIC)
  PHP_ME(Call, getPeer, arginfo_getPeer, ZEND_ACC_PUBLIC)
  PHP_ME(Call, cancel, arginfo_cancel, ZEND_ACC_PUBLIC)
  PHP_ME(Call, setCredentials, arginfo_setCredentials, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

void grpc_init_call(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Call", call_methods);
  ce.create_object = create_wrapped_grpc_call;
  grpc_ce_call = zend_register_internal_class(&ce TSRMLS_CC);
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_call, call_ce_handlers);
}
