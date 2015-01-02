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

/* Create a new Event object that wraps an existing grpc_event struct */
zval *grpc_php_convert_event(grpc_event *event);

#endif /* NET_GRPC_PHP_GRPC_COMPLETION_CHANNEL_H */
