#include "event.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_grpc.h"

#include <stdbool.h>

#include "grpc/grpc.h"

#include "byte_buffer.h"
#include "call.h"
#include "timeval.h"

/* Frees and finishes a wrapped instance of grpc_event */
void free_wrapped_grpc_event(void *object TSRMLS_DC){
  wrapped_grpc_event *event = (wrapped_grpc_event*)object;
  if(event->wrapped != NULL){
    grpc_event_finish(event->wrapped);
  }
  efree(event);
}

/* Initializes an instance of wrapped_grpc_channel to be associated with an
 * object of a class specified by class_type */
zend_object_value create_wrapped_grpc_event(
    zend_class_entry *class_type TSRMLS_DC){
  zend_object_value retval;
  wrapped_grpc_event *intern;
  intern = (wrapped_grpc_event*)emalloc(sizeof(wrapped_grpc_event));
  memset(intern, 0, sizeof(wrapped_grpc_event));
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  retval.handle = zend_objects_store_put(
      intern,
      (zend_objects_store_dtor_t)zend_objects_destroy_object,
      free_wrapped_grpc_event,
      NULL TSRMLS_CC);
  retval.handlers = zend_get_std_object_handlers();
  return retval;
}

zval *grpc_php_wrap_event(grpc_event *wrapped){
  zval *event_object;
  MAKE_STD_ZVAL(event_object);

  object_init_ex(event_object, grpc_ce_event);
  wrapped_grpc_event *event = (wrapped_grpc_event*)zend_object_store_get_object(
      event_object TSRMLS_CC);
  event->wrapped = wrapped;
  return event_object;
}

/**
 * Get the type of the event
 * @return long Integer representing the type
 */
PHP_METHOD(Event, get_type){
  wrapped_grpc_event *event = (wrapped_grpc_event*)zend_object_store_get_object(
      getThis() TSRMLS_CC);
  RETURN_LONG((long)(event->wrapped->type));
}

/**
 * Get the tag of the event
 * @return long The event's tag
 */
PHP_METHOD(Event, get_tag){
  wrapped_grpc_event *event = (wrapped_grpc_event*)zend_object_store_get_object(
      getThis() TSRMLS_CC);
  RETURN_LONG((long)(event->wrapped->tag));
}

/**
 * Get the call associated with the event
 * @return Call The call
 */
PHP_METHOD(Event, get_call){
  wrapped_grpc_event *event = (wrapped_grpc_event*)zend_object_store_get_object(
      getThis() TSRMLS_CC);
  zval *call_obj = grpc_php_wrap_call(event->wrapped->call);
  RETURN_DESTROY_ZVAL(call_obj);
}

/**
 * Get the data associated with the event
 * @return object The data, with type depending on the type field
 */
PHP_METHOD(Event, get_data){
  zval *retval;
  wrapped_grpc_event *wrapped_event =
    (wrapped_grpc_event*)zend_object_store_get_object(
        getThis() TSRMLS_CC);
  grpc_event *event = wrapped_event->wrapped;
  char *detail_string;
  size_t detail_len;
  char *method_string;
  size_t method_len;
  char *host_string;
  size_t host_len;
  char *read_string;
  size_t read_len;

  switch(event->type){
    case GRPC_QUEUE_SHUTDOWN: RETURN_NULL(); break;
    case GRPC_READ:
      if(event->data.read == NULL){
        RETURN_NULL();
      } else {
        byte_buffer_to_string(event->data.read, &read_string, &read_len);
        RETURN_STRINGL(read_string, read_len, true);
      }
      break;
    case GRPC_INVOKE_ACCEPTED:
      RETURN_LONG((long)event->data.invoke_accepted); break;
    case GRPC_WRITE_ACCEPTED:
      RETURN_LONG((long)event->data.write_accepted); break;
    case GRPC_FINISH_ACCEPTED:
      RETURN_LONG((long)event->data.finish_accepted); break;
    case GRPC_CLIENT_METADATA_READ:
      retval = grpc_call_create_metadata_array(
          event->data.client_metadata_read.count,
          event->data.client_metadata_read.elements);
      break;
    case GRPC_FINISHED:
      MAKE_STD_ZVAL(retval);
      object_init(retval);
      add_property_long(retval, "code", event->data.finished.status);
      if(event->data.finished.details == NULL){
        add_property_null(retval, "details");
      } else {
        detail_len = strlen(event->data.finished.details);
        detail_string = ecalloc(detail_len+1, sizeof(char));
        memcpy(detail_string, event->data.finished.details, detail_len);
        add_property_string(retval,
                            "details",
                            detail_string,
                            true);
      }
      add_property_zval(retval, "metadata", grpc_call_create_metadata_array(
          event->data.finished.metadata_count,
          event->data.finished.metadata_elements));
      break;
    case GRPC_SERVER_RPC_NEW:
      MAKE_STD_ZVAL(retval);
      object_init(retval);
      method_len = strlen(event->data.server_rpc_new.method);
      method_string = ecalloc(method_len+1, sizeof(char));
      memcpy(method_string, event->data.server_rpc_new.method, method_len);
      add_property_string(retval,
                          "method",
                          method_string,
                          false);
      host_len = strlen(event->data.server_rpc_new.host);
      host_string = ecalloc(host_len+1, sizeof(char));
      memcpy(host_string, event->data.server_rpc_new.host, host_len);
      add_property_string(retval,
                          "host",
                          host_string,
                          false);
      add_property_zval(retval,
                        "absolute_timeout",
                        grpc_php_wrap_timeval(
                            event->data.server_rpc_new.deadline));
      add_property_zval(retval,
                        "metadata",
                        grpc_call_create_metadata_array(
                            event->data.server_rpc_new.metadata_count,
                            event->data.server_rpc_new.metadata_elements));
      break;
    default: RETURN_NULL(); break;
  }
  RETURN_DESTROY_ZVAL(retval);
}

static zend_function_entry event_methods[] = {
  PHP_ME(Event, get_call, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(Event, get_data, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(Event, get_tag, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(Event, get_type, NULL, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

void grpc_init_event(TSRMLS_D){
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Event", event_methods);
  ce.create_object = create_wrapped_grpc_event;
  grpc_ce_event = zend_register_internal_class(&ce TSRMLS_CC);
}
