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

#ifndef NET_GRPC_PHP_GRPC_TIMEVAL_H_
#define NET_GRPC_PHP_GRPC_TIMEVAL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include "php_grpc.h"

#include <grpc/grpc.h>
#include <grpc/support/time.h>

/* Class entry for the Timeval PHP Class */
extern zend_class_entry *grpc_ce_timeval;

/* Wrapper struct for timeval that can be associated with a PHP object */
PHP_GRPC_WRAP_OBJECT_START(wrapped_grpc_timeval)
  gpr_timespec wrapped;
PHP_GRPC_WRAP_OBJECT_END(wrapped_grpc_timeval)

#if PHP_MAJOR_VERSION < 7

#define Z_WRAPPED_GRPC_TIMEVAL_P(zv) \
  (wrapped_grpc_timeval *)zend_object_store_get_object(zv TSRMLS_CC)

#else

static inline wrapped_grpc_timeval
*wrapped_grpc_timeval_from_obj(zend_object *obj) {
  return (wrapped_grpc_timeval*)((char*)(obj) -
                                 XtOffsetOf(wrapped_grpc_timeval, std));
}

#define Z_WRAPPED_GRPC_TIMEVAL_P(zv) \
  wrapped_grpc_timeval_from_obj(Z_OBJ_P((zv)))

#endif /* PHP_MAJOR_VERSION */

/* Initialize the Timeval PHP class */
void grpc_init_timeval(TSRMLS_D);

/* Shutdown the Timeval PHP class */
void grpc_shutdown_timeval(TSRMLS_D);

/* Creates a Timeval object that wraps the given timeval struct */
zval *grpc_php_wrap_timeval(gpr_timespec wrapped TSRMLS_DC);

#endif /* NET_GRPC_PHP_GRPC_TIMEVAL_H_ */
