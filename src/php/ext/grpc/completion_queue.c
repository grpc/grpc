#include "completion_queue.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/spl/spl_exceptions.h"
#include "php_grpc.h"

#include "zend_exceptions.h"

#include <stdbool.h>

#include "grpc/grpc.h"

#include "event.h"
#include "timeval.h"

/* Frees and destroys a wrapped instance of grpc_completion_queue */
void free_wrapped_grpc_completion_queue(void *object TSRMLS_DC){
  wrapped_grpc_completion_queue *queue = NULL;
  grpc_event *event;
  queue = (wrapped_grpc_completion_queue*)object;
  if(queue->wrapped != NULL){
    grpc_completion_queue_shutdown(queue->wrapped);
    event = grpc_completion_queue_next(queue->wrapped, gpr_inf_future);
    while(event != NULL){
      if(event->type == GRPC_QUEUE_SHUTDOWN){
        break;
      }
      event = grpc_completion_queue_next(queue->wrapped, gpr_inf_future);
    }
    grpc_completion_queue_destroy(queue->wrapped);
  }
  efree(queue);
}

/* Initializes an instance of wrapped_grpc_channel to be associated with an
 * object of a class specified by class_type */
zend_object_value create_wrapped_grpc_completion_queue(
    zend_class_entry *class_type TSRMLS_DC){
  zend_object_value retval;
  wrapped_grpc_completion_queue *intern;

  intern = (wrapped_grpc_completion_queue*)emalloc(
      sizeof(wrapped_grpc_completion_queue));
  memset(intern, 0, sizeof(wrapped_grpc_completion_queue));

  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  retval.handle = zend_objects_store_put(
      intern,
      (zend_objects_store_dtor_t) zend_objects_destroy_object,
      free_wrapped_grpc_completion_queue,
      NULL TSRMLS_CC);
  retval.handlers = zend_get_std_object_handlers();
  return retval;
}

/**
 * Construct an instance of CompletionQueue
 */
PHP_METHOD(CompletionQueue, __construct){
  wrapped_grpc_completion_queue *queue =
    (wrapped_grpc_completion_queue*)zend_object_store_get_object(
        getThis() TSRMLS_CC);
  queue->wrapped = grpc_completion_queue_create();
}

/**
 * Blocks until an event is available, the completion queue is being shutdown,
 * or timeout is reached. Returns NULL on timeout, otherwise the event that
 * occurred. Callers should call event.finish once they have processed the
 * event.
 * @param Timeval $timeout The timeout for the event
 * @return Event The event that occurred
 */
PHP_METHOD(CompletionQueue, next){
  zval *timeout;
  /* "O" == 1 Object */
  if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                           "O",
                           &timeout, grpc_ce_timeval)==FAILURE){
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "next needs a Timeval",
                         1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_completion_queue *completion_queue =
    (wrapped_grpc_completion_queue*)zend_object_store_get_object(
        getThis() TSRMLS_CC);
  wrapped_grpc_timeval *wrapped_timeout =
    (wrapped_grpc_timeval*)zend_object_store_get_object(timeout TSRMLS_CC);
  grpc_event *event = grpc_completion_queue_next(completion_queue->wrapped,
                                                 wrapped_timeout->wrapped);
  if(event == NULL){
    RETURN_NULL();
  }
  zval *wrapped_event = grpc_php_wrap_event(event);
  RETURN_DESTROY_ZVAL(wrapped_event);
}

PHP_METHOD(CompletionQueue, pluck){
  long tag;
  zval *timeout;
  /* "lO" == 1 long, 1 Object */
  if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                           "lO",
                           &tag,
                           &timeout, grpc_ce_timeval)==FAILURE){
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "pluck needs a long and a Timeval",
                         1 TSRMLS_CC);
  }
  wrapped_grpc_completion_queue *completion_queue =
      (wrapped_grpc_completion_queue*)zend_object_store_get_object(
          getThis() TSRMLS_CC);
  wrapped_grpc_timeval *wrapped_timeout =
      (wrapped_grpc_timeval*)zend_object_store_get_object(timeout TSRMLS_CC);
  grpc_event *event = grpc_completion_queue_pluck(completion_queue->wrapped,
                                                    (void*)tag,
                                                    wrapped_timeout->wrapped);
  if(event == NULL){
    RETURN_NULL();
  }
  zval *wrapped_event = grpc_php_wrap_event(event);
  RETURN_DESTROY_ZVAL(wrapped_event);
}

static zend_function_entry completion_queue_methods[] = {
  PHP_ME(CompletionQueue, __construct, NULL, ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
  PHP_ME(CompletionQueue, next, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(CompletionQueue, pluck, NULL, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

void grpc_init_completion_queue(TSRMLS_D){
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\CompletionQueue", completion_queue_methods);
  ce.create_object = create_wrapped_grpc_completion_queue;
  grpc_ce_completion_queue = zend_register_internal_class(&ce TSRMLS_CC);
}
