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
typedef struct wrapped_grpc_timeval {
  zend_object std;

  gpr_timespec wrapped;
} wrapped_grpc_timeval;

/* Initialize the Timeval PHP class */
void grpc_init_timeval(TSRMLS_D);

/* Shutdown the Timeval PHP class */
void grpc_shutdown_timeval(TSRMLS_D);

/* Creates a Timeval object that wraps the given timeval struct */
zval *grpc_php_wrap_timeval(gpr_timespec wrapped TSRMLS_DC);

#endif /* NET_GRPC_PHP_GRPC_TIMEVAL_H_ */
