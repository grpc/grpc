/*
 *
 * Copyright 2016, Google Inc.
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


#ifndef PHP7_WRAPPER_GRPC_H
#define PHP7_WRAPPER_GRPC_H

#if PHP_MAJOR_VERSION < 7

#define php_grpc_int int
#define php_grpc_long long
#define php_grpc_ulong ulong
#define php_grpc_zend_object zend_object_value
#define php_grpc_add_property_string(arg, name, context, b) \
  add_property_string(arg, name, context, b)
#define php_grpc_add_property_stringl(res, name, str, len, b) \
  add_property_stringl(res, name, str, len, b)
#define php_grpc_add_next_index_stringl(data, str, len, b) \
  add_next_index_stringl(data, str, len, b)

#define PHP_GRPC_RETURN_STRING(val, dup) RETURN_STRING(val, dup)
#define PHP_GRPC_MAKE_STD_ZVAL(pzv) MAKE_STD_ZVAL(pzv)
#define PHP_GRPC_FREE_STD_ZVAL(pzv)
#define PHP_GRPC_DELREF(zv) Z_DELREF_P(zv)

#define RETURN_DESTROY_ZVAL(val) \
  RETURN_ZVAL(val, false /* Don't execute copy constructor */, \
              true /* Dealloc original before returning */)

#define PHP_GRPC_WRAP_OBJECT_START(name) \
  typedef struct name { \
    zend_object std;
#define PHP_GRPC_WRAP_OBJECT_END(name) \
  } name;

#define PHP_GRPC_FREE_WRAPPED_FUNC_START(class_object) \
  void free_##class_object(void *object TSRMLS_DC) { \
    class_object *p = (class_object *)object;
#define PHP_GRPC_FREE_WRAPPED_FUNC_END() \
    zend_object_std_dtor(&p->std TSRMLS_CC); \
    efree(p); \
  }

#define PHP_GRPC_ALLOC_CLASS_OBJECT(class_object) \
  class_object *intern; \
  zend_object_value retval; \
  intern = (class_object *)emalloc(sizeof(class_object)); \
  memset(intern, 0, sizeof(class_object));

#define PHP_GRPC_FREE_CLASS_OBJECT(class_object, handler) \
  retval.handle = zend_objects_store_put( \
    intern, (zend_objects_store_dtor_t)zend_objects_destroy_object, \
    free_##class_object, NULL TSRMLS_CC); \
  retval.handlers = zend_get_std_object_handlers(); \
  return retval;

#define PHP_GRPC_HASH_FOREACH_VAL_START(ht, data) \
  zval **tmp_data = NULL; \
  for (zend_hash_internal_pointer_reset(ht); \
       zend_hash_get_current_data(ht, (void**)&tmp_data) == SUCCESS; \
       zend_hash_move_forward(ht)) { \
    data = *tmp_data;

#define PHP_GRPC_HASH_FOREACH_STR_KEY_VAL_START(ht, key, key_type, data) \
  zval **tmp##key = NULL; \
  ulong index##key; \
  uint len##key; \
  for (zend_hash_internal_pointer_reset(ht); \
       zend_hash_get_current_data(ht, (void**)&tmp##key) == SUCCESS; \
       zend_hash_move_forward(ht)) { \
    key_type = zend_hash_get_current_key_ex(ht, &key, &len##key, &index##key,\
                                         0, NULL); \
    data = *tmp##key;

#define PHP_GRPC_HASH_FOREACH_LONG_KEY_VAL_START(ht, key, key_type, index,\
                                                 data) \
  zval **tmp##key = NULL; \
  uint len##key; \
  for (zend_hash_internal_pointer_reset(ht); \
       zend_hash_get_current_data(ht, (void**)&tmp##key) == SUCCESS; \
       zend_hash_move_forward(ht)) { \
    key_type = zend_hash_get_current_key_ex(ht, &key, &len##key, &index,\
                                         0, NULL); \
    data = *tmp##key;

#define PHP_GRPC_HASH_FOREACH_END() }

static inline int php_grpc_zend_hash_find(HashTable *ht, char *key, int len,
                                          void **value) {
  zval **data = NULL;
  if (zend_hash_find(ht, key, len, (void **)&data) == SUCCESS) {
    *value = *data;
    return SUCCESS;
  } else {
    *value = NULL;
    return FAILURE;
  }
}

#define php_grpc_zend_hash_del zend_hash_del

#define PHP_GRPC_GET_CLASS_ENTRY(object) zend_get_class_entry(object TSRMLS_CC)

#define PHP_GRPC_INIT_HANDLER(class_object, handler_name)

#else

#define php_grpc_int size_t
#define php_grpc_long zend_long
#define php_grpc_ulong zend_ulong
#define php_grpc_zend_object zend_object*
#define php_grpc_add_property_string(arg, name, context, b) \
  add_property_string(arg, name, context)
#define php_grpc_add_property_stringl(res, name, str, len, b) \
  add_property_stringl(res, name, str, len)
#define php_grpc_add_next_index_stringl(data, str, len, b) \
  add_next_index_stringl(data, str, len)

#define PHP_GRPC_RETURN_STRING(val, dup) RETURN_STRING(val)
#define PHP_GRPC_MAKE_STD_ZVAL(pzv) \
  pzv = (zval *)emalloc(sizeof(zval));
#define PHP_GRPC_FREE_STD_ZVAL(pzv) efree(pzv);
#define PHP_GRPC_DELREF(zv)

#define RETURN_DESTROY_ZVAL(val) \
  RETVAL_ZVAL(val, false /* Don't execute copy constructor */, \
              true /* Dealloc original before returning */); \
  efree(val); \
  return

#define PHP_GRPC_WRAP_OBJECT_START(name) \
  typedef struct name {
#define PHP_GRPC_WRAP_OBJECT_END(name) \
    zend_object std; \
  } name;

#define WRAPPED_OBJECT_FROM_OBJ(class_object, obj) \
  class_object##_from_obj(obj);

#define PHP_GRPC_FREE_WRAPPED_FUNC_START(class_object) \
  static void free_##class_object(zend_object *object) { \
    class_object *p = WRAPPED_OBJECT_FROM_OBJ(class_object, object)
#define PHP_GRPC_FREE_WRAPPED_FUNC_END() \
    zend_object_std_dtor(&p->std); \
  }

#define PHP_GRPC_ALLOC_CLASS_OBJECT(class_object) \
  class_object *intern; \
  intern = ecalloc(1, sizeof(class_object) + \
                   zend_object_properties_size(class_type));

#define PHP_GRPC_FREE_CLASS_OBJECT(class_object, handler) \
  intern->std.handlers = &handler; \
  return &intern->std;

#define PHP_GRPC_HASH_FOREACH_VAL_START(ht, data) \
  ZEND_HASH_FOREACH_VAL(ht, data) {

#define PHP_GRPC_HASH_FOREACH_STR_KEY_VAL_START(ht, key, key_type, data) \
  zend_string *(zs_##key); \
  ZEND_HASH_FOREACH_STR_KEY_VAL(ht, (zs_##key), data) { \
    if ((zs_##key) == NULL) {key = NULL; key_type = HASH_KEY_IS_LONG;} \
    else {key = (zs_##key)->val; key_type = HASH_KEY_IS_STRING;}

#define PHP_GRPC_HASH_FOREACH_LONG_KEY_VAL_START(ht, key, key_type, index, \
                                                 data) \
  zend_string *(zs_##key); \
  ZEND_HASH_FOREACH_KEY_VAL(ht, index, zs_##key, data) { \
    if ((zs_##key) == NULL) {key = NULL; key_type = HASH_KEY_IS_LONG;} \
    else {key = (zs_##key)->val; key_type = HASH_KEY_IS_STRING;}

#define PHP_GRPC_HASH_FOREACH_END() } ZEND_HASH_FOREACH_END();

static inline int php_grpc_zend_hash_find(HashTable *ht, char *key, int len,
                                          void **value) {
  zval *value_tmp = zend_hash_str_find(ht, key, len -1);
  if (value_tmp == NULL) {
    return FAILURE;
  } else {
    *value = (void *)value_tmp;
    return SUCCESS;
  }
}

static inline int php_grpc_zend_hash_del(HashTable *ht, char *key, int len) {
  return zend_hash_str_del(ht, key, len - 1);
}

#define PHP_GRPC_GET_CLASS_ENTRY(object) Z_OBJ_P(object)->ce

#define PHP_GRPC_INIT_HANDLER(class_object, handler_name) \
  memcpy(&handler_name, zend_get_std_object_handlers(), \
         sizeof(zend_object_handlers)); \
  handler_name.offset = XtOffsetOf(class_object, std); \
  handler_name.free_obj = free_##class_object

#endif /* PHP_MAJOR_VERSION */

#endif /* PHP7_WRAPPER_GRPC_H */
