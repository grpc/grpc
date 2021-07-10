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

#include "metadata_array.h"

#include <zend_exceptions.h>

#include <grpc/support/alloc.h>

/* Creates and returns a PHP array object with the data in a
 * grpc_metadata_array. Returns NULL on failure */
zval* grpc_parse_metadata_array(grpc_metadata_array* metadata_array TSRMLS_DC) {
  int count = metadata_array->count;
  grpc_metadata* elements = metadata_array->metadata;
  zval* array;
  PHP_GRPC_MAKE_STD_ZVAL(array);
  array_init(array);
  int i;
  HashTable* array_hash;
  zval* inner_array;
  char* str_key;
  char* str_val;
  size_t key_len;
  zval* data = NULL;

  array_hash = Z_ARRVAL_P(array);
  grpc_metadata* elem;
  for (i = 0; i < count; i++) {
    elem = &elements[i];
    key_len = GRPC_SLICE_LENGTH(elem->key);
    str_key = ecalloc(key_len + 1, sizeof(char));
    memcpy(str_key, GRPC_SLICE_START_PTR(elem->key), key_len);
    str_val = ecalloc(GRPC_SLICE_LENGTH(elem->value) + 1, sizeof(char));
    memcpy(str_val, GRPC_SLICE_START_PTR(elem->value),
           GRPC_SLICE_LENGTH(elem->value));
    if (php_grpc_zend_hash_find(array_hash, str_key, key_len, (void**)&data) ==
        SUCCESS) {
      if (Z_TYPE_P(data) != IS_ARRAY) {
        zend_throw_exception(zend_exception_get_default(TSRMLS_C),
                             "Metadata hash somehow contains wrong types.",
                             1 TSRMLS_CC);
        efree(str_key);
        efree(str_val);
        PHP_GRPC_FREE_STD_ZVAL(array);
        return NULL;
      }
      php_grpc_add_next_index_stringl(data, str_val,
                                      GRPC_SLICE_LENGTH(elem->value), false);
    } else {
      PHP_GRPC_MAKE_STD_ZVAL(inner_array);
      array_init(inner_array);
      php_grpc_add_next_index_stringl(inner_array, str_val,
                                      GRPC_SLICE_LENGTH(elem->value), false);
      add_assoc_zval(array, str_key, inner_array);
      PHP_GRPC_FREE_STD_ZVAL(inner_array);
    }
    efree(str_key);
    efree(str_val);
  }
  return array;
}

/* Populates a grpc_metadata_array with the data in a PHP array object.
   Returns true on success and false on failure */
bool create_metadata_array(zval* array, grpc_metadata_array* metadata) {
  HashTable* array_hash;
  HashTable* inner_array_hash;
  zval* value;
  zval* inner_array;
  grpc_metadata_array_init(metadata);
  metadata->count = 0;
  metadata->metadata = NULL;
  if (Z_TYPE_P(array) != IS_ARRAY) {
    return false;
  }
  array_hash = Z_ARRVAL_P(array);

  char* key;
  int key_type;
  PHP_GRPC_HASH_FOREACH_STR_KEY_VAL_START(array_hash, key, key_type,
                                          inner_array)
  if (key_type != HASH_KEY_IS_STRING || key == NULL) {
    return false;
  }
  if (Z_TYPE_P(inner_array) != IS_ARRAY) {
    return false;
  }
  inner_array_hash = Z_ARRVAL_P(inner_array);
  metadata->capacity += zend_hash_num_elements(inner_array_hash);
  PHP_GRPC_HASH_FOREACH_END()

  metadata->metadata = gpr_malloc(metadata->capacity * sizeof(grpc_metadata));

  char* key1 = NULL;
  int key_type1;
  PHP_GRPC_HASH_FOREACH_STR_KEY_VAL_START(array_hash, key1, key_type1,
                                          inner_array)
  if (key_type1 != HASH_KEY_IS_STRING) {
    return false;
  }
  if (!grpc_header_key_is_legal(grpc_slice_from_static_string(key1))) {
    return false;
  }
  inner_array_hash = Z_ARRVAL_P(inner_array);
  PHP_GRPC_HASH_FOREACH_VAL_START(inner_array_hash, value)
  if (Z_TYPE_P(value) != IS_STRING) {
    return false;
  }
  metadata->metadata[metadata->count].key = grpc_slice_from_copied_string(key1);
  metadata->metadata[metadata->count].value =
      grpc_slice_from_copied_buffer(Z_STRVAL_P(value), Z_STRLEN_P(value));
  metadata->count += 1;
  PHP_GRPC_HASH_FOREACH_END()
  PHP_GRPC_HASH_FOREACH_END()
  return true;
}

void grpc_php_metadata_array_destroy_including_entries(
    grpc_metadata_array* array) {
  size_t i;
  if (array->metadata) {
    for (i = 0; i < array->count; i++) {
      grpc_slice_unref(array->metadata[i].key);
      grpc_slice_unref(array->metadata[i].value);
    }
  }
  grpc_metadata_array_destroy(array);
}
