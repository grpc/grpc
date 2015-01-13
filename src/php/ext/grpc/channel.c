#include "channel.h"

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
#include "grpc/support/log.h"
#include "grpc/grpc_security.h"

#include "completion_queue.h"
#include "server.h"
#include "credentials.h"

/* Frees and destroys an instance of wrapped_grpc_channel */
void free_wrapped_grpc_channel(void *object TSRMLS_DC) {
  wrapped_grpc_channel *channel = (wrapped_grpc_channel *)object;
  if (channel->wrapped != NULL) {
    grpc_channel_destroy(channel->wrapped);
  }
  efree(channel);
}

/* Initializes an instance of wrapped_grpc_channel to be associated with an
 * object of a class specified by class_type */
zend_object_value create_wrapped_grpc_channel(zend_class_entry *class_type
                                                  TSRMLS_DC) {
  zend_object_value retval;
  wrapped_grpc_channel *intern;
  intern = (wrapped_grpc_channel *)emalloc(sizeof(wrapped_grpc_channel));
  memset(intern, 0, sizeof(wrapped_grpc_channel));
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  retval.handle = zend_objects_store_put(
      intern, (zend_objects_store_dtor_t)zend_objects_destroy_object,
      free_wrapped_grpc_channel, NULL TSRMLS_CC);
  retval.handlers = zend_get_std_object_handlers();
  return retval;
}

void php_grpc_read_args_array(zval *args_array, grpc_channel_args *args) {
  HashTable *array_hash;
  HashPosition array_pointer;
  int args_index;
  zval **data;
  char *key;
  uint key_len;
  ulong index;
  array_hash = Z_ARRVAL_P(args_array);
  args->num_args = zend_hash_num_elements(array_hash);
  args->args = ecalloc(args->num_args, sizeof(grpc_arg));
  args_index = 0;
  for (zend_hash_internal_pointer_reset_ex(array_hash, &array_pointer);
       zend_hash_get_current_data_ex(array_hash, (void **)&data,
                                     &array_pointer) == SUCCESS;
       zend_hash_move_forward_ex(array_hash, &array_pointer)) {
    if (zend_hash_get_current_key_ex(array_hash, &key, &key_len, &index, 0,
                                     &array_pointer) != HASH_KEY_IS_STRING) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "args keys must be strings", 1 TSRMLS_CC);
      return;
    }
    args->args[args_index].key = key;
    switch (Z_TYPE_P(*data)) {
      case IS_LONG:
        args->args[args_index].value.integer = (int)Z_LVAL_P(*data);
        break;
      case IS_STRING:
        args->args[args_index].value.string = Z_STRVAL_P(*data);
        break;
      default:
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "args values must be int or string", 1 TSRMLS_CC);
        return;
    }
    args_index++;
  }
}

/**
 * Construct an instance of the Channel class. If the $args array contains a
 * "credentials" key mapping to a Credentials object, a secure channel will be
 * created with those credentials.
 * @param string $target The hostname to associate with this channel
 * @param array $args The arguments to pass to the Channel (optional)
 */
PHP_METHOD(Channel, __construct) {
  wrapped_grpc_channel *channel =
      (wrapped_grpc_channel *)zend_object_store_get_object(getThis() TSRMLS_CC);
  char *target;
  int target_length;
  zval *args_array = NULL;
  grpc_channel_args args;
  HashTable *array_hash;
  zval **creds_obj = NULL;
  wrapped_grpc_credentials *creds = NULL;
  /* "s|a" == 1 string, 1 optional array */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|a", &target,
                            &target_length, &args_array) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Channel expects a string and an array", 1 TSRMLS_CC);
    return;
  }
  if (args_array == NULL) {
    channel->wrapped = grpc_channel_create(target, NULL);
  } else {
    array_hash = Z_ARRVAL_P(args_array);
    if (zend_hash_find(array_hash, "credentials", sizeof("credentials"),
                       (void **)&creds_obj) == SUCCESS) {
      if (zend_get_class_entry(*creds_obj TSRMLS_CC) != grpc_ce_credentials) {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "credentials must be a Credentials object",
                             1 TSRMLS_CC);
        return;
      }
      creds = (wrapped_grpc_credentials *)zend_object_store_get_object(
          *creds_obj TSRMLS_CC);
      zend_hash_del(array_hash, "credentials", 12);
    }
    php_grpc_read_args_array(args_array, &args);
    if (creds == NULL) {
      channel->wrapped = grpc_channel_create(target, &args);
    } else {
      gpr_log(GPR_DEBUG, "Initialized secure channel");
      channel->wrapped =
          grpc_secure_channel_create(creds->wrapped, target, &args);
    }
    efree(args.args);
  }
  channel->target = ecalloc(target_length + 1, sizeof(char));
  memcpy(channel->target, target, target_length);
}

/**
 * Close the channel
 */
PHP_METHOD(Channel, close) {
  wrapped_grpc_channel *channel =
      (wrapped_grpc_channel *)zend_object_store_get_object(getThis() TSRMLS_CC);
  if (channel->wrapped != NULL) {
    grpc_channel_destroy(channel->wrapped);
    channel->wrapped = NULL;
  }
}

static zend_function_entry channel_methods[] = {
    PHP_ME(Channel, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
        PHP_ME(Channel, close, NULL, ZEND_ACC_PUBLIC) PHP_FE_END};

void grpc_init_channel(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Channel", channel_methods);
  ce.create_object = create_wrapped_grpc_channel;
  grpc_ce_channel = zend_register_internal_class(&ce TSRMLS_CC);
}
