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

#ifndef NET_GRPC_PHP_GRPC_CHANNEL_H_
#define NET_GRPC_PHP_GRPC_CHANNEL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include "php_grpc.h"

#include <grpc/grpc.h>

/* Class entry for the PHP Channel class */
extern zend_class_entry *grpc_ce_channel;

/* Wrapper struct for grpc_channel that can be associated with a PHP object */
PHP_GRPC_WRAP_OBJECT_START(wrapped_grpc_channel)
  grpc_channel *wrapped;
PHP_GRPC_WRAP_OBJECT_END(wrapped_grpc_channel)

#if PHP_MAJOR_VERSION < 7

#define Z_WRAPPED_GRPC_CHANNEL_P(zv) \
  (wrapped_grpc_channel *)zend_object_store_get_object(zv TSRMLS_CC)

#else

static inline wrapped_grpc_channel
*wrapped_grpc_channel_from_obj(zend_object *obj) {
  return (wrapped_grpc_channel*)((char*)(obj) -
                                 XtOffsetOf(wrapped_grpc_channel, std));
}

#define Z_WRAPPED_GRPC_CHANNEL_P(zv) \
  wrapped_grpc_channel_from_obj(Z_OBJ_P((zv)))

#endif /* PHP_MAJOR_VERSION */

/* Initializes the Channel class */
void grpc_init_channel(TSRMLS_D);

/* Iterates through a PHP array and populates args with the contents */
void php_grpc_read_args_array(zval *args_array, grpc_channel_args *args
                              TSRMLS_DC);

#endif /* NET_GRPC_PHP_GRPC_CHANNEL_H_ */
