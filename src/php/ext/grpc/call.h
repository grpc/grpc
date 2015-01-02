#ifndef NET_GRPC_PHP_GRPC_CALL_H_
#define NET_GRPC_PHP_GRPC_CALL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_grpc.h"

#include "grpc/grpc.h"

/* Class entry for the Call PHP class */
zend_class_entry *grpc_ce_call;

/* Wrapper struct for grpc_call that can be associated with a PHP object */
typedef struct wrapped_grpc_call {
  zend_object std;

  bool owned;
  grpc_call *wrapped;
} wrapped_grpc_call;

/* Initializes the Call PHP class */
void grpc_init_call(TSRMLS_D);

/* Creates a Call object that wraps the given grpc_call struct */
zval *grpc_php_wrap_call(grpc_call *wrapped, bool owned);

/* Creates and returns a PHP associative array of metadata from a C array of
 * call metadata */
zval *grpc_call_create_metadata_array(int count, grpc_metadata *elements);

#endif /* NET_GRPC_PHP_GRPC_CHANNEL_H_ */
