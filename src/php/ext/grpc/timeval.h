#ifndef NET_GRPC_PHP_GRPC_TIMEVAL_H_
#define NET_GRPC_PHP_GRPC_TIMEVAL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_grpc.h"

#include "grpc/grpc.h"
#include "grpc/support/time.h"

/* Class entry for the Timeval PHP Class */
zend_class_entry *grpc_ce_timeval;

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
zval *grpc_php_wrap_timeval(gpr_timespec wrapped);

#endif /* NET_GRPC_PHP_GRPC_TIMEVAL_H_ */
