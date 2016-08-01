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
