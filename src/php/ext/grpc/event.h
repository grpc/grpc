#ifndef NET_GRPC_PHP_GRPC_EVENT_H_
#define NET_GRPC_PHP_GRPC_EVENT_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_grpc.h"

#include "grpc/grpc.h"

/* Class entry for the PHP Event class */
zend_class_entry *grpc_ce_event;

/* Struct wrapping grpc_event that can be associated with a PHP object */
typedef struct wrapped_grpc_event {
  zend_object std;

  grpc_event *wrapped;
} wrapped_grpc_event;

/* Initialize the Event class */
void grpc_init_event(TSRMLS_D);

/* Create a new Event object that wraps an existing grpc_event struct */
zval *grpc_php_wrap_event(grpc_event *wrapped);

#endif /* NET_GRPC_PHP_GRPC_COMPLETION_CHANNEL_H */
