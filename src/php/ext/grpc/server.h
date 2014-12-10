#ifndef NET_GRPC_PHP_GRPC_SERVER_H_
#define NET_GRPC_PHP_GRPC_SERVER_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_grpc.h"

#include "grpc/grpc.h"

/* Class entry for the Server PHP class */
zend_class_entry *grpc_ce_server;

/* Wrapper struct for grpc_server that can be associated with a PHP object */
typedef struct wrapped_grpc_server {
  zend_object std;

  grpc_server *wrapped;
} wrapped_grpc_server;

/* Initializes the Server class */
void grpc_init_server(TSRMLS_D);

#endif /* NET_GRPC_PHP_GRPC_SERVER_H_ */
