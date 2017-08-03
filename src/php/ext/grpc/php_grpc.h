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


#ifndef PHP_GRPC_H
#define PHP_GRPC_H

#include <stdbool.h>

extern zend_module_entry grpc_module_entry;
#define phpext_grpc_ptr &grpc_module_entry

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
#include "version.h"

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
