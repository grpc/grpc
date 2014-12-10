#ifndef NET_GRPC_PHP_GRPC_SERVER_CREDENTIALS_H_
#define NET_GRPC_PHP_GRPC_SERVER_CREDENTIALS_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_grpc.h"

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

/* Class entry for the Server_Credentials PHP class */
zend_class_entry *grpc_ce_server_credentials;

/* Wrapper struct for grpc_server_credentials that can be associated with a PHP
 * object */
typedef struct wrapped_grpc_server_credentials {
  zend_object std;

  grpc_server_credentials *wrapped;
} wrapped_grpc_server_credentials;

/* Initializes the Server_Credentials PHP class */
void grpc_init_server_credentials(TSRMLS_D);

#endif /* NET_GRPC_PHP_GRPC_SERVER_CREDENTIALS_H_ */
