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

#ifndef NET_GRPC_PHP_GRPC_CALL_H_
#define NET_GRPC_PHP_GRPC_CALL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include "php_grpc.h"

#include <grpc/grpc.h>

/* Class entry for the Call PHP class */
extern zend_class_entry *grpc_ce_call;

/* Wrapper struct for grpc_call that can be associated with a PHP object */
PHP_GRPC_WRAP_OBJECT_START(wrapped_grpc_call)
  bool owned;
  grpc_call *wrapped;
PHP_GRPC_WRAP_OBJECT_END(wrapped_grpc_call)

#if PHP_MAJOR_VERSION < 7

#define Z_WRAPPED_GRPC_CALL_P(zv) \
  (wrapped_grpc_call *)zend_object_store_get_object(zv TSRMLS_CC)

#else

static inline wrapped_grpc_call
*wrapped_grpc_call_from_obj(zend_object *obj) {
  return (wrapped_grpc_call*)((char*)(obj) -
                              XtOffsetOf(wrapped_grpc_call, std));
}

#define Z_WRAPPED_GRPC_CALL_P(zv) wrapped_grpc_call_from_obj(Z_OBJ_P((zv)))

#endif /* PHP_MAJOR_VERSION */

/* Creates and returns a PHP associative array of metadata from a C array of
 * call metadata */
zval *grpc_parse_metadata_array(grpc_metadata_array *metadata_array TSRMLS_DC);

/* Creates a Call object that wraps the given grpc_call struct */
zval *grpc_php_wrap_call(grpc_call *wrapped, bool owned TSRMLS_DC);

/* Initializes the Call PHP class */
void grpc_init_call(TSRMLS_D);

/* Populates a grpc_metadata_array with the data in a PHP array object.
   Returns true on success and false on failure */
bool create_metadata_array(zval *array, grpc_metadata_array *metadata);

#endif /* NET_GRPC_PHP_GRPC_CHANNEL_H_ */
