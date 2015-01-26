#include "call.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/spl/spl_exceptions.h"
#include "php_grpc.h"

#include "zend_exceptions.h"
#include "zend_hash.h"

#include <stdbool.h>

#include "grpc/support/log.h"
#include "grpc/grpc.h"

#include "timeval.h"
#include "channel.h"
#include "completion_queue.h"
#include "byte_buffer.h"

/* Frees and destroys an instance of wrapped_grpc_call */
void free_wrapped_grpc_call(void *object TSRMLS_DC) {
  wrapped_grpc_call *call = (wrapped_grpc_call *)object;
  if (call->owned && call->wrapped != NULL) {
    grpc_call_destroy(call->wrapped);
  }
  efree(call);
}

/* Initializes an instance of wrapped_grpc_call to be associated with an object
 * of a class specified by class_type */
zend_object_value create_wrapped_grpc_call(zend_class_entry *class_type
                                               TSRMLS_DC) {
  zend_object_value retval;
  wrapped_grpc_call *intern;

  intern = (wrapped_grpc_call *)emalloc(sizeof(wrapped_grpc_call));
  memset(intern, 0, sizeof(wrapped_grpc_call));

  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  retval.handle = zend_objects_store_put(
      intern, (zend_objects_store_dtor_t)zend_objects_destroy_object,
      free_wrapped_grpc_call, NULL TSRMLS_CC);
  retval.handlers = zend_get_std_object_handlers();
  return retval;
}

/* Wraps a grpc_call struct in a PHP object. Owned indicates whether the struct
   should be destroyed at the end of the object's lifecycle */
zval *grpc_php_wrap_call(grpc_call *wrapped, bool owned) {
  zval *call_object;
  MAKE_STD_ZVAL(call_object);
  object_init_ex(call_object, grpc_ce_call);
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(call_object TSRMLS_CC);
  call->wrapped = wrapped;
  return call_object;
}

zval *grpc_call_create_metadata_array(int count, grpc_metadata *elements) {
  int i;
  zval *array;
  zval **data = NULL;
  HashTable *array_hash;
  zval *inner_array;
  char *str_key;
  char *str_val;
  size_t key_len;
  MAKE_STD_ZVAL(array);
  array_init(array);
  array_hash = Z_ARRVAL_P(array);
  grpc_metadata *elem;
  for (i = 0; i < count; i++) {
    elem = &elements[i];
    key_len = strlen(elem->key);
    str_key = ecalloc(key_len + 1, sizeof(char));
    memcpy(str_key, elem->key, key_len);
    str_val = ecalloc(elem->value_length + 1, sizeof(char));
    memcpy(str_val, elem->value, elem->value_length);
    if (zend_hash_find(array_hash, str_key, key_len, (void **)data) ==
        SUCCESS) {
      switch (Z_TYPE_P(*data)) {
        case IS_STRING:
          MAKE_STD_ZVAL(inner_array);
          array_init(inner_array);
          add_next_index_zval(inner_array, *data);
          add_assoc_zval(array, str_key, inner_array);
          break;
        case IS_ARRAY:
          inner_array = *data;
          break;
        default:
          zend_throw_exception(zend_exception_get_default(),
                               "Metadata hash somehow contains wrong types.",
                               1 TSRMLS_CC);
          efree(str_key);
          efree(str_val);
          return NULL;
      }
      add_next_index_stringl(inner_array, str_val, elem->value_length, false);
    } else {
      add_assoc_stringl(array, str_key, str_val, elem->value_length, false);
    }
  }
  return array;
}

int php_grpc_call_add_metadata_array_walk(void *elem TSRMLS_DC, int num_args,
                                          va_list args,
                                          zend_hash_key *hash_key) {
  grpc_call_error error_code;
  zval **data = (zval **)elem;
  grpc_metadata metadata;
  grpc_call *call = va_arg(args, grpc_call *);
  gpr_uint32 flags = va_arg(args, gpr_uint32);
  const char *key;
  HashTable *inner_hash;
  /* We assume that either two args were passed, and we are in the recursive
     case (and the second argument is the key), or one arg was passed and
     hash_key is the string key. */
  if (num_args > 2) {
    key = va_arg(args, const char *);
  } else {
    /* TODO(mlumish): If possible, check that hash_key is a string */
    key = hash_key->arKey;
  }
  switch (Z_TYPE_P(*data)) {
    case IS_STRING:
      metadata.key = (char *)key;
      metadata.value = Z_STRVAL_P(*data);
      metadata.value_length = Z_STRLEN_P(*data);
      error_code = grpc_call_add_metadata(call, &metadata, 0u);
      MAYBE_THROW_CALL_ERROR(add_metadata, error_code);
      break;
    case IS_ARRAY:
      inner_hash = Z_ARRVAL_P(*data);
      zend_hash_apply_with_arguments(inner_hash TSRMLS_CC,
                                     php_grpc_call_add_metadata_array_walk, 3,
                                     call, flags, key);
      break;
    default:
      zend_throw_exception(zend_exception_get_default(),
                           "Metadata hash somehow contains wrong types.",
                           1 TSRMLS_CC);
  }
  return ZEND_HASH_APPLY_KEEP;
}

/**
 * Constructs a new instance of the Call class.
 * @param Channel $channel The channel to associate the call with. Must not be
 *     closed.
 * @param string $method The method to call
 * @param Timeval $absolute_deadline The deadline for completing the call
 */
PHP_METHOD(Call, __construct) {
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  zval *channel_obj;
  char *method;
  int method_len;
  zval *deadline_obj;
  /* "OsO" == 1 Object, 1 string, 1 Object */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "OsO", &channel_obj,
                            grpc_ce_channel, &method, &method_len,
                            &deadline_obj, grpc_ce_timeval) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Call expects a Channel, a String, and a Timeval",
                         1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_channel *channel =
      (wrapped_grpc_channel *)zend_object_store_get_object(
          channel_obj TSRMLS_CC);
  if (channel->wrapped == NULL) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Call cannot be constructed from a closed Channel",
                         1 TSRMLS_CC);
    return;
  }
  add_property_zval(getThis(), "channel", channel_obj);
  wrapped_grpc_timeval *deadline =
      (wrapped_grpc_timeval *)zend_object_store_get_object(
          deadline_obj TSRMLS_CC);
  call->wrapped = grpc_channel_create_call(channel->wrapped, method,
                                           channel->target, deadline->wrapped);
}

/**
 * Add metadata to the call. All array keys must be strings. If the value is a
 * string, it is added as a key/value pair. If it is an array, each value is
 * added paired with the same string
 * @param array $metadata The metadata to add
 * @param long $flags A bitwise combination of the Grpc\WRITE_* constants
 * (optional)
 * @return Void
 */
PHP_METHOD(Call, add_metadata) {
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  zval *array;
  HashTable *array_hash;
  long flags = 0;
  /* "a|l" == 1 array, 1 optional long */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a|l", &array, &flags) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "add_metadata expects an array and an optional long",
                         1 TSRMLS_CC);
    return;
  }
  array_hash = Z_ARRVAL_P(array);
  zend_hash_apply_with_arguments(array_hash TSRMLS_CC,
                                 php_grpc_call_add_metadata_array_walk, 2,
                                 call->wrapped, (gpr_uint32)flags);
}

/**
 * Invoke the RPC. Starts sending metadata and request headers over the wire
 * @param CompletionQueue $queue The completion queue to use with this call
 * @param long $metadata_tag The tag to associate with returned metadata
 * @param long $finished_tag The tag to associate with the finished event
 * @param long $flags A bitwise combination of the Grpc\WRITE_* constants
 * (optional)
 * @return Void
 */
PHP_METHOD(Call, invoke) {
  grpc_call_error error_code;
  long tag1;
  long tag2;
  zval *queue_obj;
  long flags = 0;
  /* "Oll|l" == 1 Object, 3 mandatory longs, 1 optional long */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oll|l", &queue_obj,
                            grpc_ce_completion_queue, &tag1, &tag2,
                            &flags) == FAILURE) {
    zend_throw_exception(
        spl_ce_InvalidArgumentException,
        "invoke needs a CompletionQueue, 2 longs, and an optional long",
        1 TSRMLS_CC);
    return;
  }
  add_property_zval(getThis(), "completion_queue", queue_obj);
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  wrapped_grpc_completion_queue *queue =
      (wrapped_grpc_completion_queue *)zend_object_store_get_object(
          queue_obj TSRMLS_CC);
  error_code = grpc_call_invoke(call->wrapped, queue->wrapped, (void *)tag1,
                                (void *)tag2, (gpr_uint32)flags);
  MAYBE_THROW_CALL_ERROR(invoke, error_code);
}

/**
 * Accept an incoming RPC, binding a completion queue to it. To be called after
 * adding metadata to the call, but before sending messages. Can only be called
 * on the server
 * @param CompletionQueue $queue The completion queue to use with this call
 * @param long $finished_tag The tag to associate with the finished event
 * @param long $flags A bitwise combination of the Grpc\WRITE_* constants
 * (optional)
 * @return Void
 */
PHP_METHOD(Call, server_accept) {
  long tag;
  zval *queue_obj;
  grpc_call_error error_code;
  /* "Ol|l" == 1 Object, 1 long */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Ol", &queue_obj,
                            grpc_ce_completion_queue, &tag) == FAILURE) {
    zend_throw_exception(
        spl_ce_InvalidArgumentException,
        "server_accept expects a CompletionQueue, a long, and an optional long",
        1 TSRMLS_CC);
    return;
  }
  add_property_zval(getThis(), "completion_queue", queue_obj);
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  wrapped_grpc_completion_queue *queue =
      (wrapped_grpc_completion_queue *)zend_object_store_get_object(
          queue_obj TSRMLS_CC);
  error_code =
      grpc_call_server_accept(call->wrapped, queue->wrapped, (void *)tag);
  MAYBE_THROW_CALL_ERROR(server_accept, error_code);
}

PHP_METHOD(Call, server_end_initial_metadata) {
  grpc_call_error error_code;
  long flags = 0;
  /* "|l" == 1 optional long */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &flags) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "server_end_initial_metadata expects an optional long",
                         1 TSRMLS_CC);
  }
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  error_code = grpc_call_server_end_initial_metadata(call->wrapped, flags);
  MAYBE_THROW_CALL_ERROR(server_end_initial_metadata, error_code);
}

/**
 * Called by clients to cancel an RPC on the server.
 * @return Void
 */
PHP_METHOD(Call, cancel) {
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  grpc_call_error error_code = grpc_call_cancel(call->wrapped);
  MAYBE_THROW_CALL_ERROR(cancel, error_code);
}

/**
 * Queue a byte buffer for writing
 * @param string $buffer The buffer to queue for writing
 * @param long $tag The tag to associate with this write
 * @param long $flags A bitwise combination of the Grpc\WRITE_* constants
 * (optional)
 * @return Void
 */
PHP_METHOD(Call, start_write) {
  grpc_call_error error_code;
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  char *buffer;
  int buffer_len;
  long tag;
  long flags = 0;
  /* "Ol|l" == 1 Object, 1 mandatory long, 1 optional long */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl|l", &buffer,
                            &buffer_len, &tag, &flags) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "start_write expects a string and an optional long",
                         1 TSRMLS_CC);
    return;
  }
  error_code = grpc_call_start_write(call->wrapped,
                                     string_to_byte_buffer(buffer, buffer_len),
                                     (void *)tag, (gpr_uint32)flags);
  MAYBE_THROW_CALL_ERROR(start_write, error_code);
}

/**
 * Queue a status for writing
 * @param long $status_code The status code to send
 * @param string $status_details The status details to send
 * @param long $tag The tag to associate with this status
 * @return Void
 */
PHP_METHOD(Call, start_write_status) {
  grpc_call_error error_code;
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  long status_code;
  int status_details_length;
  long tag;
  char *status_details;
  /* "lsl" == 1 long, 1 string, 1 long */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "lsl", &status_code,
                            &status_details, &status_details_length,
                            &tag) == FAILURE) {
    zend_throw_exception(
        spl_ce_InvalidArgumentException,
        "start_write_status expects a long, a string, and a long", 1 TSRMLS_CC);
    return;
  }
  error_code =
      grpc_call_start_write_status(call->wrapped, (grpc_status_code)status_code,
                                   status_details, (void *)tag);
  MAYBE_THROW_CALL_ERROR(start_write_status, error_code);
}

/**
 * Indicate that there are no more messages to send
 * @return Void
 */
PHP_METHOD(Call, writes_done) {
  grpc_call_error error_code;
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  long tag;
  /* "l" == 1 long */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &tag) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "writes_done expects a long", 1 TSRMLS_CC);
    return;
  }
  error_code = grpc_call_writes_done(call->wrapped, (void *)tag);
  MAYBE_THROW_CALL_ERROR(writes_done, error_code);
}

/**
 * Initiate a read on a call. Output event contains a byte buffer with the
 * result of the read
 * @param long $tag The tag to associate with this read
 * @return Void
 */
PHP_METHOD(Call, start_read) {
  grpc_call_error error_code;
  wrapped_grpc_call *call =
      (wrapped_grpc_call *)zend_object_store_get_object(getThis() TSRMLS_CC);
  long tag;
  /* "l" == 1 long */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &tag) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "start_read expects a long", 1 TSRMLS_CC);
    return;
  }
  error_code = grpc_call_start_read(call->wrapped, (void *)tag);
  MAYBE_THROW_CALL_ERROR(start_read, error_code);
}

static zend_function_entry call_methods[] = {
    PHP_ME(Call, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    PHP_ME(Call, server_accept, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, server_end_initial_metadata, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, add_metadata, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, cancel, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, invoke, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, start_read, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, start_write, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, start_write_status, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Call, writes_done, NULL, ZEND_ACC_PUBLIC) PHP_FE_END};

void grpc_init_call(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Call", call_methods);
  ce.create_object = create_wrapped_grpc_call;
  grpc_ce_call = zend_register_internal_class(&ce TSRMLS_CC);
}
