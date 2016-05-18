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
#include "call_credentials.h"

#include <zend_exceptions.h>
#include <zend_hash.h>

#include <stdbool.h>

#include <grpc/support/alloc.h>
#include <grpc/grpc.h>

#include "completion_queue.h"
#include "timeval.h"
#include "channel.h"
#include "byte_buffer.h"

zend_class_entry *grpc_ce_call;

/* Frees and destroys an instance of wrapped_grpc_call */
void free_wrapped_grpc_call(void *object TSRMLS_DC) {
  wrapped_grpc_call *call = (wrapped_grpc_call *)object;
  if (call->owned && call->wrapped != NULL) {
    grpc_call_destroy(call->wrapped);
  }
  efree(call);
}

/* Initializes an instance of wrapped_grpc_call to be associated with an object
 * of a class specified by class_type */
zend_object_value create_wrapped_grpc_call(zend_class_entry *class_type
                                               TSRMLS_DC) {
  zend_object_value retval;
  wrapped_grpc_call *intern;

  intern = (wrapped_grpc_call *)emalloc(sizeof(wrapped_grpc_call));
  memset(intern, 0, sizeof(wrapped_grpc_call));

  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  retval.handle = zend_objects_store_put(
      intern, (zend_objects_store_dtor_t)zend_objects_destroy_object,
      free_wrapped_grpc_call, NULL TSRMLS_CC);
  retval.handlers = zend_get_std_object_handlers();
  return retval;
}

/* Wraps a grpc_call struct in a PHP object. Owned indicates whether the struct
   should be destroyed at the end of the object's lifecycle */
zval *grpc_php_wrap_call(grpc_call *wrapped, bool owned TSRMLS_DC) {
  zval *call_object;
  MAKE_STD_ZVAL(call_object);
  object_init_ex(call_object, grpc_ce_call);
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(call_object TSRMLS_CC);
  call->wrapped = wrapped;
  call->owned = owned;
  return call_object;
}

/* Creates and returns a PHP array object with the data in a
 * grpc_metadata_array. Returns NULL on failure */
zval *grpc_parse_metadata_array(grpc_metadata_array *metadata_array TSRMLS_DC) {
  int count = metadata_array->count;
  grpc_metadata *elements = metadata_array->metadata;
  int i;
  zval *array;
  zval **data = NULL;
  HashTable *array_hash;
  zval *inner_array;
  char *str_key;
  char *str_val;
  size_t key_len;
  MAKE_STD_ZVAL(array);
  array_init(array);
  array_hash = Z_ARRVAL_P(array);
  grpc_metadata *elem;
  for (i = 0; i < count; i++) {
    elem = &elements[i];
    key_len = strlen(elem->key);
    str_key = ecalloc(key_len + 1, sizeof(char));
    memcpy(str_key, elem->key, key_len);
    str_val = ecalloc(elem->value_length + 1, sizeof(char));
    memcpy(str_val, elem->value, elem->value_length);
    if (zend_hash_find(array_hash, str_key, key_len, (void **)data) ==
        SUCCESS) {
      if (Z_TYPE_P(*data) != IS_ARRAY) {
        zend_throw_exception(zend_exception_get_default(TSRMLS_C),
                             "Metadata hash somehow contains wrong types.",
                             1 TSRMLS_CC);
        efree(str_key);
        efree(str_val);
        return NULL;
      }
      add_next_index_stringl(*data, str_val, elem->value_length, false);
    } else {
      MAKE_STD_ZVAL(inner_array);
      array_init(inner_array);
      add_next_index_stringl(inner_array, str_val, elem->value_length, false);
      add_assoc_zval(array, str_key, inner_array);
    }
  }
  return array;
}

/* Populates a grpc_metadata_array with the data in a PHP array object.
   Returns true on success and false on failure */
bool create_metadata_array(zval *array, grpc_metadata_array *metadata) {
  zval **inner_array;
  zval **value;
  HashTable *array_hash;
  HashPosition array_pointer;
  HashTable *inner_array_hash;
  HashPosition inner_array_pointer;
  char *key;
  uint key_len;
  ulong index;
  if (Z_TYPE_P(array) != IS_ARRAY) {
    return false;
  }
  grpc_metadata_array_init(metadata);
  array_hash = Z_ARRVAL_P(array);
  for (zend_hash_internal_pointer_reset_ex(array_hash, &array_pointer);
       zend_hash_get_current_data_ex(array_hash, (void**)&inner_array,
                                     &array_pointer) == SUCCESS;
       zend_hash_move_forward_ex(array_hash, &array_pointer)) {
    if (zend_hash_get_current_key_ex(array_hash, &key, &key_len, &index, 0,
                                     &array_pointer) != HASH_KEY_IS_STRING) {
      return false;
    }
    if (Z_TYPE_P(*inner_array) != IS_ARRAY) {
      return false;
    }
    inner_array_hash = Z_ARRVAL_P(*inner_array);
    metadata->capacity += zend_hash_num_elements(inner_array_hash);
  }
  metadata->metadata = gpr_malloc(metadata->capacity * sizeof(grpc_metadata));
  for (zend_hash_internal_pointer_reset_ex(array_hash, &array_pointer);
       zend_hash_get_current_data_ex(array_hash, (void**)&inner_array,
                                     &array_pointer) == SUCCESS;
       zend_hash_move_forward_ex(array_hash, &array_pointer)) {
    if (zend_hash_get_current_key_ex(array_hash, &key, &key_len, &index, 0,
                                     &array_pointer) != HASH_KEY_IS_STRING) {
      return false;
    }
    inner_array_hash = Z_ARRVAL_P(*inner_array);
    for (zend_hash_internal_pointer_reset_ex(inner_array_hash,
                                             &inner_array_pointer);
         zend_hash_get_current_data_ex(inner_array_hash, (void**)&value,
                                       &inner_array_pointer) == SUCCESS;
         zend_hash_move_forward_ex(inner_array_hash, &inner_array_pointer)) {
      if (Z_TYPE_P(*value) != IS_STRING) {
        return false;
      }
      metadata->metadata[metadata->count].key = key;
      metadata->metadata[metadata->count].value = Z_STRVAL_P(*value);
      metadata->metadata[metadata->count].value_length = Z_STRLEN_P(*value);
      metadata->count += 1;
    }
  }
  return true;
}

/**
 * Constructs a new instance of the Call class.
 * @param Channel $channel The channel to associate the call with. Must not be
 *     closed.
 * @param string $method The method to call
 * @param Timeval $absolute_deadline The deadline for completing the call
 */
PHP_METHOD(Call, __construct) {
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  zval *channel_obj;
  char *method;
  int method_len;
  zval *deadline_obj;
  char *host_override = NULL;
  int host_override_len = 0;
  /* "OsO|s" == 1 Object, 1 string, 1 Object, 1 optional string */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "OsO|s",
                            &channel_obj, grpc_ce_channel,
                            &method, &method_len,
                            &deadline_obj, grpc_ce_timeval,
                            &host_override, &host_override_len)
      == FAILURE) {
    zend_throw_exception(
        spl_ce_InvalidArgumentException,
        "Call expects a Channel, a String, a Timeval and an optional String",
        1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_channel *channel =
      (wrapped_grpc_channel *)zend_object_store_get_object(
          channel_obj TSRMLS_CC);
  if (channel->wrapped == NULL) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Call cannot be constructed from a closed Channel",
                         1 TSRMLS_CC);
    return;
  }
  add_property_zval(getThis(), "channel", channel_obj);
  wrapped_grpc_timeval *deadline =
      (wrapped_grpc_timeval *)zend_object_store_get_object(
          deadline_obj TSRMLS_CC);
  call->wrapped = grpc_channel_create_call(
      channel->wrapped, NULL, GRPC_PROPAGATE_DEFAULTS, completion_queue, method,
      host_override, deadline->wrapped, NULL);
}

/**
 * Start a batch of RPC actions.
 * @param array batch Array of actions to take
 * @return object Object with results of all actions
 */
PHP_METHOD(Call, startBatch) {
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  grpc_op ops[8];
  size_t op_num = 0;
  zval *array;
  zval **value;
  zval **inner_value;
  HashTable *array_hash;
  HashPosition array_pointer;
  HashTable *status_hash;
  HashTable *message_hash;
  zval **message_value;
  zval **message_flags;
  char *key;
  uint key_len;
  ulong index;
  grpc_metadata_array metadata;
  grpc_metadata_array trailing_metadata;
  grpc_metadata_array recv_metadata;
  grpc_metadata_array recv_trailing_metadata;
  grpc_status_code status;
  char *status_details = NULL;
  size_t status_details_capacity = 0;
  grpc_byte_buffer *message;
  int cancelled;
  grpc_call_error error;
  zval *result;
  char *message_str;
  size_t message_len;
  zval *recv_status;
  grpc_metadata_array_init(&metadata);
  grpc_metadata_array_init(&trailing_metadata);
  grpc_metadata_array_init(&recv_metadata);
  grpc_metadata_array_init(&recv_trailing_metadata);
  MAKE_STD_ZVAL(result);
  object_init(result);
  memset(ops, 0, sizeof(ops));
  /* "a" == 1 array */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &array) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "start_batch expects an array", 1 TSRMLS_CC);
    goto cleanup;
  }
  array_hash = Z_ARRVAL_P(array);
  for (zend_hash_internal_pointer_reset_ex(array_hash, &array_pointer);
       zend_hash_get_current_data_ex(array_hash, (void**)&value,
                                     &array_pointer) == SUCCESS;
       zend_hash_move_forward_ex(array_hash, &array_pointer)) {
    if (zend_hash_get_current_key_ex(array_hash, &key, &key_len, &index, 0,
                                     &array_pointer) != HASH_KEY_IS_LONG) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "batch keys must be integers", 1 TSRMLS_CC);
      goto cleanup;
    }
    switch(index) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        if (!create_metadata_array(*value, &metadata)) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Bad metadata value given", 1 TSRMLS_CC);
          goto cleanup;
        }
        ops[op_num].data.send_initial_metadata.count =
            metadata.count;
        ops[op_num].data.send_initial_metadata.metadata =
            metadata.metadata;
        break;
      case GRPC_OP_SEND_MESSAGE:
        if (Z_TYPE_PP(value) != IS_ARRAY) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Expected an array for send message",
                               1 TSRMLS_CC);
          goto cleanup;
        }
        message_hash = Z_ARRVAL_PP(value);
        if (zend_hash_find(message_hash, "flags", sizeof("flags"),
                           (void **)&message_flags) == SUCCESS) {
          if (Z_TYPE_PP(message_flags) != IS_LONG) {
            zend_throw_exception(spl_ce_InvalidArgumentException,
                                 "Expected an int for message flags",
                                 1 TSRMLS_CC);
          }
          ops[op_num].flags = Z_LVAL_PP(message_flags) & GRPC_WRITE_USED_MASK;
        }
        if (zend_hash_find(message_hash, "message", sizeof("message"),
                           (void **)&message_value) != SUCCESS ||
            Z_TYPE_PP(message_value) != IS_STRING) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Expected a string for send message",
                               1 TSRMLS_CC);
          goto cleanup;
        }
        ops[op_num].data.send_message =
            string_to_byte_buffer(Z_STRVAL_PP(message_value),
                                  Z_STRLEN_PP(message_value));
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        status_hash = Z_ARRVAL_PP(value);
        if (zend_hash_find(status_hash, "metadata", sizeof("metadata"),
                           (void **)&inner_value) == SUCCESS) {
          if (!create_metadata_array(*inner_value, &trailing_metadata)) {
            zend_throw_exception(spl_ce_InvalidArgumentException,
                                 "Bad trailing metadata value given",
                                 1 TSRMLS_CC);
            goto cleanup;
          }
          ops[op_num].data.send_status_from_server.trailing_metadata =
              trailing_metadata.metadata;
          ops[op_num].data.send_status_from_server.trailing_metadata_count =
              trailing_metadata.count;
        }
        if (zend_hash_find(status_hash, "code", sizeof("code"),
                           (void**)&inner_value) == SUCCESS) {
          if (Z_TYPE_PP(inner_value) != IS_LONG) {
            zend_throw_exception(spl_ce_InvalidArgumentException,
                                 "Status code must be an integer",
                                 1 TSRMLS_CC);
            goto cleanup;
          }
          ops[op_num].data.send_status_from_server.status =
              Z_LVAL_PP(inner_value);
        } else {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Integer status code is required",
                               1 TSRMLS_CC);
          goto cleanup;
        }
        if (zend_hash_find(status_hash, "details", sizeof("details"),
                           (void**)&inner_value) == SUCCESS) {
          if (Z_TYPE_PP(inner_value) != IS_STRING) {
            zend_throw_exception(spl_ce_InvalidArgumentException,
                                 "Status details must be a string",
                                 1 TSRMLS_CC);
            goto cleanup;
          }
          ops[op_num].data.send_status_from_server.status_details =
              Z_STRVAL_PP(inner_value);
        } else {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "String status details is required",
                               1 TSRMLS_CC);
          goto cleanup;
        }
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        ops[op_num].data.recv_initial_metadata = &recv_metadata;
        break;
      case GRPC_OP_RECV_MESSAGE:
        ops[op_num].data.recv_message = &message;
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        ops[op_num].data.recv_status_on_client.trailing_metadata =
            &recv_trailing_metadata;
        ops[op_num].data.recv_status_on_client.status = &status;
        ops[op_num].data.recv_status_on_client.status_details =
            &status_details;
        ops[op_num].data.recv_status_on_client.status_details_capacity =
            &status_details_capacity;
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        ops[op_num].data.recv_close_on_server.cancelled = &cancelled;
        break;
      default:
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "Unrecognized key in batch", 1 TSRMLS_CC);
        goto cleanup;
    }
    ops[op_num].op = (grpc_op_type)index;
    ops[op_num].flags = 0;
    ops[op_num].reserved = NULL;
    op_num++;
  }
  error = grpc_call_start_batch(call->wrapped, ops, op_num, call->wrapped,
                                NULL);
  if (error != GRPC_CALL_OK) {
    zend_throw_exception(spl_ce_LogicException,
                         "start_batch was called incorrectly",
                         (long)error TSRMLS_CC);
    goto cleanup;
  }
  grpc_completion_queue_pluck(completion_queue, call->wrapped,
                              gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  for (int i = 0; i < op_num; i++) {
    switch(ops[i].op) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        add_property_bool(result, "send_metadata", true);
        break;
      case GRPC_OP_SEND_MESSAGE:
        add_property_bool(result, "send_message", true);
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        add_property_bool(result, "send_close", true);
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        add_property_bool(result, "send_status", true);
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        array = grpc_parse_metadata_array(&recv_metadata TSRMLS_CC);
        add_property_zval(result, "metadata", array);
        Z_DELREF_P(array);
        break;
      case GRPC_OP_RECV_MESSAGE:
        byte_buffer_to_string(message, &message_str, &message_len);
        if (message_str == NULL) {
          add_property_null(result, "message");
        } else {
          add_property_stringl(result, "message", message_str, message_len,
                               false);
        }
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        MAKE_STD_ZVAL(recv_status);
        object_init(recv_status);
        array = grpc_parse_metadata_array(&recv_trailing_metadata TSRMLS_CC);
        add_property_zval(recv_status, "metadata", array);
        Z_DELREF_P(array);
        add_property_long(recv_status, "code", status);
        add_property_string(recv_status, "details", status_details, true);
        add_property_zval(result, "status", recv_status);
        Z_DELREF_P(recv_status);
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        add_property_bool(result, "cancelled", cancelled);
        break;
      default:
        break;
    }
  }
cleanup:
  grpc_metadata_array_destroy(&metadata);
  grpc_metadata_array_destroy(&trailing_metadata);
  grpc_metadata_array_destroy(&recv_metadata);
  grpc_metadata_array_destroy(&recv_trailing_metadata);
  if (status_details != NULL) {
    gpr_free(status_details);
  }
  for (int i = 0; i < op_num; i++) {
    if (ops[i].op == GRPC_OP_SEND_MESSAGE) {
      grpc_byte_buffer_destroy(ops[i].data.send_message);
    }
    if (ops[i].op == GRPC_OP_RECV_MESSAGE) {
      grpc_byte_buffer_destroy(message);
    }
  }
  RETURN_DESTROY_ZVAL(result);
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
PHP_METHOD(Call, getPeer) {
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  RETURN_STRING(grpc_call_get_peer(call->wrapped), 1);
}

/**
 * Cancel the call. This will cause the call to end with STATUS_CANCELLED if it
 * has not already ended with another status.
 */
PHP_METHOD(Call, cancel) {
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  grpc_call_cancel(call->wrapped, NULL);
}

/**
 * Set the CallCredentials for this call.
 * @param CallCredentials creds_obj The CallCredentials object
 * @param int The error code
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
      (wrapped_grpc_call_credentials *)zend_object_store_get_object(
          creds_obj TSRMLS_CC);

  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);

  grpc_call_error error = GRPC_CALL_ERROR;
  error = grpc_call_set_credentials(call->wrapped, creds->wrapped);
  RETURN_LONG(error);
}

static zend_function_entry call_methods[] = {
    PHP_ME(Call, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    PHP_ME(Call, startBatch, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, getPeer, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, cancel, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, setCredentials, NULL, ZEND_ACC_PUBLIC)
    PHP_FE_END};

void grpc_init_call(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Call", call_methods);
  ce.create_object = create_wrapped_grpc_call;
  grpc_ce_call = zend_register_internal_class(&ce TSRMLS_CC);
}
