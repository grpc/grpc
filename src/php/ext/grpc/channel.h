#ifndef NET_GRPC_PHP_GRPC_CHANNEL_H_
#define NET_GRPC_PHP_GRPC_CHANNEL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_grpc.h"

#include "grpc/grpc.h"

/* Class entry for the PHP Channel class */
zend_class_entry *grpc_ce_channel;

/* Wrapper struct for grpc_channel that can be associated with a PHP object */
typedef struct wrapped_grpc_channel {
  zend_object std;

  grpc_channel *wrapped;
  char *target;
} wrapped_grpc_channel;

/* Initializes the Channel class */
void grpc_init_channel(TSRMLS_D);

/* Iterates through a PHP array and populates args with the contents */
void php_grpc_read_args_array(zval *args_array, grpc_channel_args *args);

#endif /* NET_GRPC_PHP_GRPC_CHANNEL_H_ */
