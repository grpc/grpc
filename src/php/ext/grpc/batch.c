/*
 *
 * Copyright 2021 gRPC authors.
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

#include "batch.h"

#include <zend_exceptions.h>
#include <ext/spl/spl_exceptions.h>

#include <grpc/support/alloc.h>

#include "byte_buffer.h"
#include "metadata_array.h"

void batch_init(struct batch* batch) {
  memset(batch, 0, sizeof(*batch));
  grpc_metadata_array_init(&batch->metadata);
  grpc_metadata_array_init(&batch->trailing_metadata);
  grpc_metadata_array_init(&batch->recv_metadata);
  grpc_metadata_array_init(&batch->recv_trailing_metadata);
  batch->recv_status_details = grpc_empty_slice();
  batch->send_status_details = grpc_empty_slice();
}

void batch_destroy(struct batch* batch) {
  grpc_php_metadata_array_destroy_including_entries(&batch->metadata);
  grpc_php_metadata_array_destroy_including_entries(&batch->trailing_metadata);
  grpc_metadata_array_destroy(&batch->recv_metadata);
  grpc_metadata_array_destroy(&batch->recv_trailing_metadata);
  grpc_slice_unref(batch->recv_status_details);
  grpc_slice_unref(batch->send_status_details);

  for (int i = 0; i < batch->op_num; i++) {
    if (batch->ops[i].op == GRPC_OP_SEND_MESSAGE) {
      grpc_byte_buffer_destroy(batch->ops[i].data.send_message.send_message);
    }
    if (batch->ops[i].op == GRPC_OP_RECV_MESSAGE) {
      grpc_byte_buffer_destroy(batch->message);
    }
  }
}

bool batch_populate_ops(struct batch* batch, HashTable* array_hash) {
  php_grpc_ulong index;
  zval* value;
  zval* inner_value;
  zval* message_value;
  zval* message_flags;
  HashTable* message_hash;
  HashTable *status_hash;

  char* key = NULL;
  int key_type;
  PHP_GRPC_HASH_FOREACH_LONG_KEY_VAL_START(array_hash, key, key_type, index,
                                           value)
  if (key_type != HASH_KEY_IS_LONG || key != NULL) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "batch keys must be integers", 1 TSRMLS_CC);
    goto cleanup;
  }

  batch->ops[batch->op_num].op = (grpc_op_type)index;
  batch->ops[batch->op_num].flags = 0;
  batch->ops[batch->op_num].reserved = NULL;

  switch (index) {
    case GRPC_OP_SEND_INITIAL_METADATA:
      if (!create_metadata_array(value, &batch->metadata)) {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "Bad metadata value given", 1 TSRMLS_CC);
        goto cleanup;
      }
      batch->ops[batch->op_num].data.send_initial_metadata.count =
          batch->metadata.count;
      batch->ops[batch->op_num].data.send_initial_metadata.metadata =
          batch->metadata.metadata;
      break;
    case GRPC_OP_SEND_MESSAGE:
      if (Z_TYPE_P(value) != IS_ARRAY) {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "Expected an array for send message", 1 TSRMLS_CC);
        goto cleanup;
      }
      message_hash = Z_ARRVAL_P(value);
      if (php_grpc_zend_hash_find(message_hash, "flags", sizeof("flags"),
                                  (void**)&message_flags) == SUCCESS) {
        if (Z_TYPE_P(message_flags) != IS_LONG) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Expected an int for message flags",
                               1 TSRMLS_CC);
        }
        batch->ops[batch->op_num].flags =
            Z_LVAL_P(message_flags) & GRPC_WRITE_USED_MASK;
      }
      if (php_grpc_zend_hash_find(message_hash, "message", sizeof("message"),
                                  (void**)&message_value) != SUCCESS ||
          Z_TYPE_P(message_value) != IS_STRING) {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "Expected a string for send message", 1 TSRMLS_CC);
        goto cleanup;
      }
      batch->ops[batch->op_num].data.send_message.send_message =
          string_to_byte_buffer(Z_STRVAL_P(message_value),
                                Z_STRLEN_P(message_value));
      break;
    case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
      break;
    case GRPC_OP_SEND_STATUS_FROM_SERVER:
      status_hash = Z_ARRVAL_P(value);
      if (php_grpc_zend_hash_find(status_hash, "metadata", sizeof("metadata"),
                                  (void**)&inner_value) == SUCCESS) {
        if (!create_metadata_array(inner_value, &batch->trailing_metadata)) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Bad trailing metadata value given",
                               1 TSRMLS_CC);
          goto cleanup;
        }
        batch->ops[batch->op_num]
            .data.send_status_from_server.trailing_metadata =
            batch->trailing_metadata.metadata;
        batch->ops[batch->op_num]
            .data.send_status_from_server.trailing_metadata_count =
            batch->trailing_metadata.count;
      }
      if (php_grpc_zend_hash_find(status_hash, "code", sizeof("code"),
                                  (void**)&inner_value) == SUCCESS) {
        if (Z_TYPE_P(inner_value) != IS_LONG) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Status code must be an integer", 1 TSRMLS_CC);
          goto cleanup;
        }
        batch->ops[batch->op_num].data.send_status_from_server.status =
            Z_LVAL_P(inner_value);
      } else {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "Integer status code is required", 1 TSRMLS_CC);
        goto cleanup;
      }
      if (php_grpc_zend_hash_find(status_hash, "details", sizeof("details"),
                                  (void**)&inner_value) == SUCCESS) {
        if (Z_TYPE_P(inner_value) != IS_STRING) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Status details must be a string", 1 TSRMLS_CC);
          goto cleanup;
        }
        batch->send_status_details =
            grpc_slice_from_copied_string(Z_STRVAL_P(inner_value));
        batch->ops[batch->op_num].data.send_status_from_server.status_details =
            &batch->send_status_details;
      } else {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "String status details is required", 1 TSRMLS_CC);
        goto cleanup;
      }
      break;
    case GRPC_OP_RECV_INITIAL_METADATA:
      batch->ops[batch->op_num]
          .data.recv_initial_metadata.recv_initial_metadata =
          &batch->recv_metadata;
      break;
    case GRPC_OP_RECV_MESSAGE:
      batch->ops[batch->op_num].data.recv_message.recv_message =
          &batch->message;
      break;
    case GRPC_OP_RECV_STATUS_ON_CLIENT:
      batch->ops[batch->op_num].data.recv_status_on_client.trailing_metadata =
          &batch->recv_trailing_metadata;
      batch->ops[batch->op_num].data.recv_status_on_client.status =
          &batch->status;
      batch->ops[batch->op_num].data.recv_status_on_client.status_details =
          &batch->recv_status_details;
      break;
    case GRPC_OP_RECV_CLOSE_ON_SERVER:
      batch->ops[batch->op_num].data.recv_close_on_server.cancelled =
          &batch->cancelled;
      break;
    default:
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "Unrecognized key in batch", 1 TSRMLS_CC);
      goto cleanup;
  }
  batch->op_num++;
  PHP_GRPC_HASH_FOREACH_END()

  return true;

cleanup:
  return false;
}

zval* batch_process_ops(struct batch* batch) {
  zval* result;
  PHP_GRPC_MAKE_STD_ZVAL(result);
  object_init(result);

  zval* recv_md;
  zval* recv_status;
  zend_string* zmessage = NULL;
  for (int i = 0; i < batch->op_num; i++) {
    switch (batch->ops[i].op) {
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
        recv_md = grpc_parse_metadata_array(&batch->recv_metadata);
        add_property_zval(result, "metadata", recv_md);
        zval_ptr_dtor(recv_md);
        PHP_GRPC_FREE_STD_ZVAL(recv_md);
        PHP_GRPC_DELREF(array);
        break;
      case GRPC_OP_RECV_MESSAGE:
        zmessage = byte_buffer_to_zend_string(batch->message);

        if (zmessage == NULL) {
          add_property_null(result, "message");
        } else {
          zval zmessage_val;
          ZVAL_NEW_STR(&zmessage_val, zmessage);
          add_property_zval(result, "message", &zmessage_val);
          zval_ptr_dtor(&zmessage_val);
        }
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        PHP_GRPC_MAKE_STD_ZVAL(recv_status);
        object_init(recv_status);
        recv_md = grpc_parse_metadata_array(&batch->recv_trailing_metadata);
        add_property_zval(recv_status, "metadata", recv_md);
        zval_ptr_dtor(recv_md);
        PHP_GRPC_FREE_STD_ZVAL(recv_md);
        PHP_GRPC_DELREF(array);
        add_property_long(recv_status, "code", batch->status);
        char* status_details_text =
            grpc_slice_to_c_string(batch->recv_status_details);
        php_grpc_add_property_string(recv_status, "details",
                                     status_details_text, true);
        gpr_free(status_details_text);
        add_property_zval(result, "status", recv_status);
        zval_ptr_dtor(recv_status);
        PHP_GRPC_DELREF(recv_status);
        PHP_GRPC_FREE_STD_ZVAL(recv_status);
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        add_property_bool(result, "cancelled", batch->cancelled);
        break;
      default:
        break;
    }
  }
  return result;
}