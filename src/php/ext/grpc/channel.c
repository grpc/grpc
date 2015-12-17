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

#include "channel.h"

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
#include "channel_credentials.h"
#include "server.h"
#include "timeval.h"

zend_class_entry *grpc_ce_channel;

/* Frees and destroys an instance of wrapped_grpc_channel */
void free_wrapped_grpc_channel(void *object TSRMLS_DC) {
  wrapped_grpc_channel *channel = (wrapped_grpc_channel *)object;
  if (channel->wrapped != NULL) {
    grpc_channel_destroy(channel->wrapped);
  }
  efree(channel);
}

/* Initializes an instance of wrapped_grpc_channel to be associated with an
 * object of a class specified by class_type */
zend_object_value create_wrapped_grpc_channel(zend_class_entry *class_type
                                                  TSRMLS_DC) {
  zend_object_value retval;
  wrapped_grpc_channel *intern;
  intern = (wrapped_grpc_channel *)emalloc(sizeof(wrapped_grpc_channel));
  memset(intern, 0, sizeof(wrapped_grpc_channel));
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  retval.handle = zend_objects_store_put(
      intern, (zend_objects_store_dtor_t)zend_objects_destroy_object,
      free_wrapped_grpc_channel, NULL TSRMLS_CC);
  retval.handlers = zend_get_std_object_handlers();
  return retval;
}

void php_grpc_read_args_array(zval *args_array, grpc_channel_args *args) {
  HashTable *array_hash;
  HashPosition array_pointer;
  int args_index;
  zval **data;
  char *key;
  uint key_len;
  ulong index;
  array_hash = Z_ARRVAL_P(args_array);
  args->num_args = zend_hash_num_elements(array_hash);
  args->args = ecalloc(args->num_args, sizeof(grpc_arg));
  args_index = 0;
  for (zend_hash_internal_pointer_reset_ex(array_hash, &array_pointer);
       zend_hash_get_current_data_ex(array_hash, (void **)&data,
                                     &array_pointer) == SUCCESS;
       zend_hash_move_forward_ex(array_hash, &array_pointer)) {
    if (zend_hash_get_current_key_ex(array_hash, &key, &key_len, &index, 0,
                                     &array_pointer) != HASH_KEY_IS_STRING) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "args keys must be strings", 1 TSRMLS_CC);
      return;
    }
    args->args[args_index].key = key;
    switch (Z_TYPE_P(*data)) {
      case IS_LONG:
        args->args[args_index].value.integer = (int)Z_LVAL_P(*data);
        break;
      case IS_STRING:
        args->args[args_index].value.string = Z_STRVAL_P(*data);
        break;
      default:
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "args values must be int or string", 1 TSRMLS_CC);
        return;
    }
    args_index++;
  }
}

/**
 * Construct an instance of the Channel class. If the $args array contains a
 * "credentials" key mapping to a ChannelCredentials object, a secure channel
 * will be created with those credentials.
 * @param string $target The hostname to associate with this channel
 * @param array $args The arguments to pass to the Channel (optional)
 */
PHP_METHOD(Channel, __construct) {
  wrapped_grpc_channel *channel =
      (wrapped_grpc_channel *)zend_object_store_get_object(
          getThis() TSRMLS_CC);
  char *target;
  int target_length;
  zval *args_array = NULL;
  grpc_channel_args args;
  HashTable *array_hash;
  zval **creds_obj = NULL;
  wrapped_grpc_channel_credentials *creds = NULL;
  /* "s|a" == 1 string, 1 optional array */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|a", &target,
                            &target_length, &args_array) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Channel expects a string and an array", 1 TSRMLS_CC);
    return;
  }
  if (args_array == NULL) {
    channel->wrapped = grpc_insecure_channel_create(target, NULL, NULL);
  } else {
    array_hash = Z_ARRVAL_P(args_array);
    if (zend_hash_find(array_hash, "credentials", sizeof("credentials"),
                       (void **)&creds_obj) == SUCCESS) {
      if (Z_TYPE_P(*creds_obj) == IS_NULL) {
        creds = NULL;
        zend_hash_del(array_hash, "credentials", 12);
      } else if (zend_get_class_entry(*creds_obj TSRMLS_CC) !=
          grpc_ce_channel_credentials) {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "credentials must be a ChannelCredentials object",
                             1 TSRMLS_CC);
        return;
      } else {
        creds = (wrapped_grpc_channel_credentials *)zend_object_store_get_object(
            *creds_obj TSRMLS_CC);
        zend_hash_del(array_hash, "credentials", 12);
      }
    }
    php_grpc_read_args_array(args_array, &args);
    if (creds == NULL) {
      channel->wrapped = grpc_insecure_channel_create(target, &args, NULL);
    } else {
      gpr_log(GPR_DEBUG, "Initialized secure channel");
      channel->wrapped =
          grpc_secure_channel_create(creds->wrapped, target, &args, NULL);
    }
    efree(args.args);
  }
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
PHP_METHOD(Channel, getTarget) {
  wrapped_grpc_channel *channel =
      (wrapped_grpc_channel *)zend_object_store_get_object(getThis() TSRMLS_CC);
  RETURN_STRING(grpc_channel_get_target(channel->wrapped), 1);
}

/**
 * Get the connectivity state of the channel
 * @param bool (optional) try to connect on the channel
 * @return long The grpc connectivity state
 */
PHP_METHOD(Channel, getConnectivityState) {
  wrapped_grpc_channel *channel =
      (wrapped_grpc_channel *)zend_object_store_get_object(getThis() TSRMLS_CC);
  bool try_to_connect;
  /* "|b" == 1 optional bool */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|b", &try_to_connect) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "getConnectivityState expects a bool", 1 TSRMLS_CC);
    return;
  }
  RETURN_LONG(grpc_channel_check_connectivity_state(channel->wrapped,
                                                    (int)try_to_connect));
}

/**
 * Watch the connectivity state of the channel until it changed
 * @param long The previous connectivity state of the channel
 * @param Timeval The deadline this function should wait until
 * @return bool If the connectivity state changes from last_state
 *              before deadline
 */
PHP_METHOD(Channel, watchConnectivityState) {
  wrapped_grpc_channel *channel =
      (wrapped_grpc_channel *)zend_object_store_get_object(getThis() TSRMLS_CC);
  long last_state;
  zval *deadline_obj;
  /* "lO" == 1 long 1 object */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "lO",
          &last_state, &deadline_obj, grpc_ce_timeval) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
        "watchConnectivityState expects 1 long 1 timeval",
        1 TSRMLS_CC);
    return;
  }

  wrapped_grpc_timeval *deadline =
      (wrapped_grpc_timeval *)zend_object_store_get_object(
          deadline_obj TSRMLS_CC);
  grpc_channel_watch_connectivity_state(
      channel->wrapped, (grpc_connectivity_state)last_state,
      deadline->wrapped, completion_queue, NULL);
  grpc_event event = grpc_completion_queue_pluck(
      completion_queue, NULL,
      gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  RETURN_BOOL(event.success);
}

/**
 * Close the channel
 */
PHP_METHOD(Channel, close) {
  wrapped_grpc_channel *channel =
      (wrapped_grpc_channel *)zend_object_store_get_object(getThis() TSRMLS_CC);
  if (channel->wrapped != NULL) {
    grpc_channel_destroy(channel->wrapped);
    channel->wrapped = NULL;
  }
}

static zend_function_entry channel_methods[] = {
    PHP_ME(Channel, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    PHP_ME(Channel, getTarget, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Channel, getConnectivityState, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Channel, watchConnectivityState, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Channel, close, NULL, ZEND_ACC_PUBLIC)
    PHP_FE_END};

void grpc_init_channel(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Channel", channel_methods);
  ce.create_object = create_wrapped_grpc_channel;
  grpc_ce_channel = zend_register_internal_class(&ce TSRMLS_CC);
}
