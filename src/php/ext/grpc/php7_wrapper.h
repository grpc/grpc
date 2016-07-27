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
#define php_grpc_add_property_string(arg, name, context, b) \
  add_property_string(arg, name, context, b)
#define php_grpc_add_property_stringl(res, name, str, len, b) \
  add_property_stringl(res, name, str, len, b)
#define php_grpc_add_next_index_stringl(data, str, len, b) \
  add_next_index_stringl(data, str, len, b)

#define PHP_GRPC_RETURN_STRING(val, dup) RETURN_STRING(val, dup)
#define PHP_GRPC_MAKE_STD_ZVAL(pzv) MAKE_STD_ZVAL(pzv)

#define PHP_GRPC_WRAP_OBJECT_START(name) \
  typedef struct name { \
    zend_object std;
#define PHP_GRPC_WRAP_OBJECT_END(name) \
  } name;

#else

#define php_grpc_int size_t
#define php_grpc_long zend_long
#define php_grpc_ulong zend_ulong
#define php_grpc_add_property_string(arg, name, context, b) \
  add_property_string(arg, name, context)
#define php_grpc_add_property_stringl(res, name, str, len, b) \
  add_property_stringl(res, name, str, len)
#define php_grpc_add_next_index_stringl(data, str, len, b) \
  add_next_index_stringl(data, str, len)

#define PHP_GRPC_RETURN_STRING(val, dup) RETURN_STRING(val)
#define PHP_GRPC_MAKE_STD_ZVAL(pzv) \
  zval _stack_zval_##pzv; \
  pzv = &(_stack_zval_##pzv)

#define PHP_GRPC_WRAP_OBJECT_START(name) \
  typedef struct name {
#define PHP_GRPC_WRAP_OBJECT_END(name) \
    zend_object std; \
  } name;

#endif /* PHP_MAJOR_VERSION */

#endif /* PHP7_WRAPPER_GRPC_H */
