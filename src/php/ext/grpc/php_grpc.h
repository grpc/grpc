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


#ifndef PHP_GRPC_H
#define PHP_GRPC_H

#include <stdbool.h>

extern zend_module_entry grpc_module_entry;
#define phpext_grpc_ptr &grpc_module_entry

#define PHP_GRPC_VERSION \
  "0.1.0" /* Replace with version number for your extension */

#ifdef PHP_WIN32
#define PHP_GRPC_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#define PHP_GRPC_API __attribute__((visibility("default")))
#else
#define PHP_GRPC_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#include "php.h"

#include "php7_wrapper.h"

#include "grpc/grpc.h"

/* These are all function declarations */
/* Code that runs at module initialization */
PHP_MINIT_FUNCTION(grpc);
/* Code that runs at module shutdown */
PHP_MSHUTDOWN_FUNCTION(grpc);
/* Displays information about the module */
PHP_MINFO_FUNCTION(grpc);

/*
  Declare any global variables you may need between the BEGIN
  and END macros here:

ZEND_BEGIN_MODULE_GLOBALS(grpc)
ZEND_END_MODULE_GLOBALS(grpc)
*/

/* In every utility function you add that needs to use variables
   in php_grpc_globals, call TSRMLS_FETCH(); after declaring other
   variables used by that function, or better yet, pass in TSRMLS_CC
   after the last function argument and declare your utility function
   with TSRMLS_DC after the last declared argument.  Always refer to
   the globals in your function as GRPC_G(variable).  You are
   encouraged to rename these macros something shorter, see
   examples in any other php module directory.
*/

#ifdef ZTS
#define GRPC_G(v) TSRMG(grpc_globals_id, zend_grpc_globals *, v)
#else
#define GRPC_G(v) (grpc_globals.v)
#endif

#endif /* PHP_GRPC_H */
