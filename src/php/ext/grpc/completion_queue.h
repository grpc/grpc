#ifndef NET_GRPC_PHP_GRPC_COMPLETION_QUEUE_H_
#define NET_GRPC_PHP_GRPC_COMPLETION_QUEUE_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_grpc.h"

#include "grpc/grpc.h"

/* Class entry for the PHP CompletionQueue class */
zend_class_entry *grpc_ce_completion_queue;

/* Wrapper class for grpc_completion_queue that can be associated with a
   PHP object */
typedef struct wrapped_grpc_completion_queue {
  zend_object std;

  grpc_completion_queue *wrapped;
} wrapped_grpc_completion_queue;

/* Initialize the CompletionQueue class */
void grpc_init_completion_queue(TSRMLS_D);

#endif /* NET_GRPC_PHP_GRPC_COMPLETION_QUEUE_H_ */
