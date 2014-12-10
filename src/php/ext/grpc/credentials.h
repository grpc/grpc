#ifndef NET_GRPC_PHP_GRPC_CREDENTIALS_H_
#define NET_GRPC_PHP_GRPC_CREDENTIALS_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_grpc.h"

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

/* Class entry for the Credentials PHP class */
zend_class_entry *grpc_ce_credentials;

/* Wrapper struct for grpc_credentials that can be associated with a PHP
 * object */
typedef struct wrapped_grpc_credentials {
  zend_object std;

  grpc_credentials *wrapped;
} wrapped_grpc_credentials;

/* Initializes the Credentials PHP class */
void grpc_init_credentials(TSRMLS_D);

#endif /* NET_GRPC_PHP_GRPC_CREDENTIALS_H_ */
