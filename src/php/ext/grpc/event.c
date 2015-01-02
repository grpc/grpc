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

/* Create a new PHP object containing the event data in the event struct.
   event must not be used after this function is called */
zval *grpc_php_convert_event(grpc_event *event) {
  zval *data_object;
  char *detail_string;
  size_t detail_len;
  char *method_string;
  size_t method_len;
  char *host_string;
  size_t host_len;
  char *read_string;
  size_t read_len;

  zval *event_object;

  if(event == NULL) {
    return NULL;
  }

  MAKE_STD_ZVAL(event_object);
  object_init(event_object);

  add_property_zval(event_object,
                    "call",
                    grpc_php_wrap_call(event->call,
                                       event->type==GRPC_SERVER_RPC_NEW));
  add_property_long(event_object, "type", event->type);
  add_property_long(event_object, "tag", (long)event->tag);

  switch(event->type){
    case GRPC_QUEUE_SHUTDOWN: add_property_null(event_object, "data"); break;
    case GRPC_READ:
      if(event->data.read == NULL){
        add_property_null(event_object, "data");
      } else {
        byte_buffer_to_string(event->data.read, &read_string, &read_len);
        add_property_stringl(event_object, "data", read_string, read_len, true);
      }
      break;
    case GRPC_INVOKE_ACCEPTED:
      add_property_long(event_object,
                        "data",
                        (long)event->data.invoke_accepted);
      break;
    case GRPC_WRITE_ACCEPTED:
      add_property_long(event_object, "data", (long)event->data.write_accepted);
      break;
    case GRPC_FINISH_ACCEPTED:
      add_property_long(event_object,
                        "data",
                        (long)event->data.finish_accepted);
      break;
    case GRPC_CLIENT_METADATA_READ:
      data_object = grpc_call_create_metadata_array(
          event->data.client_metadata_read.count,
          event->data.client_metadata_read.elements);
      add_property_zval(event_object, "data", data_object);
      break;
    case GRPC_FINISHED:
      MAKE_STD_ZVAL(data_object);
      object_init(data_object);
      add_property_long(data_object, "code", event->data.finished.status);
      if(event->data.finished.details == NULL){
        add_property_null(data_object, "details");
      } else {
        detail_len = strlen(event->data.finished.details);
        detail_string = ecalloc(detail_len+1, sizeof(char));
        memcpy(detail_string, event->data.finished.details, detail_len);
        add_property_string(data_object,
                            "details",
                            detail_string,
                            true);
      }
      add_property_zval(data_object,
                        "metadata",
                        grpc_call_create_metadata_array(
                            event->data.finished.metadata_count,
                            event->data.finished.metadata_elements));
      add_property_zval(event_object, "data", data_object);
      break;
    case GRPC_SERVER_RPC_NEW:
      MAKE_STD_ZVAL(data_object);
      object_init(data_object);
      method_len = strlen(event->data.server_rpc_new.method);
      method_string = ecalloc(method_len+1, sizeof(char));
      memcpy(method_string, event->data.server_rpc_new.method, method_len);
      add_property_string(data_object,
                          "method",
                          method_string,
                          false);
      host_len = strlen(event->data.server_rpc_new.host);
      host_string = ecalloc(host_len+1, sizeof(char));
      memcpy(host_string, event->data.server_rpc_new.host, host_len);
      add_property_string(data_object,
                          "host",
                          host_string,
                          false);
      add_property_zval(data_object,
                        "absolute_timeout",
                        grpc_php_wrap_timeval(
                            event->data.server_rpc_new.deadline));
      add_property_zval(data_object,
                        "metadata",
                        grpc_call_create_metadata_array(
                            event->data.server_rpc_new.metadata_count,
                            event->data.server_rpc_new.metadata_elements));
      add_property_zval(event_object, "data", data_object);
      break;
    default: add_property_null(event_object, "data"); break;
  }
  grpc_event_finish(event);
  return event_object;
}
