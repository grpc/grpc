/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "channel.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include <ext/standard/php_var.h>
#include <ext/standard/sha1.h>
#if PHP_MAJOR_VERSION < 7
#include <ext/standard/php_smart_str.h>
#else
#include <zend_smart_str.h>
#endif
#include <ext/spl/spl_exceptions.h>
#include "php_grpc.h"

#include <zend_exceptions.h>

#include <stdbool.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include "completion_queue.h"
#include "channel_credentials.h"
#include "server.h"
#include "timeval.h"

zend_class_entry *grpc_ce_channel;
#if PHP_MAJOR_VERSION >= 7
static zend_object_handlers channel_ce_handlers;
#endif
static gpr_mu global_persistent_list_mu;
int le_plink;

/* Frees and destroys an instance of wrapped_grpc_channel */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_channel)
  if (p->wrapper != NULL) {
    gpr_mu_lock(&p->wrapper->mu);
    if (p->wrapper->wrapped != NULL) {
      php_grpc_zend_resource *rsrc;
      php_grpc_int key_len = strlen(p->wrapper->key);
      // only destroy the channel here if not found in the persistent list
      gpr_mu_lock(&global_persistent_list_mu);
      if (!(PHP_GRPC_PERSISTENT_LIST_FIND(&EG(persistent_list), p->wrapper->key,
                                          key_len, rsrc))) {
        grpc_channel_destroy(p->wrapper->wrapped);
      }
      gpr_mu_unlock(&global_persistent_list_mu);
    }
    gpr_mu_unlock(&p->wrapper->mu);
  }
PHP_GRPC_FREE_WRAPPED_FUNC_END()

/* Initializes an instance of wrapped_grpc_channel to be associated with an
 * object of a class specified by class_type */
php_grpc_zend_object create_wrapped_grpc_channel(zend_class_entry *class_type
                                                 TSRMLS_DC) {
  PHP_GRPC_ALLOC_CLASS_OBJECT(wrapped_grpc_channel);
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  PHP_GRPC_FREE_CLASS_OBJECT(wrapped_grpc_channel, channel_ce_handlers);
}

int php_grpc_read_args_array(zval *args_array,
                             grpc_channel_args *args TSRMLS_DC) {
  HashTable *array_hash;
  int args_index;
  array_hash = Z_ARRVAL_P(args_array);
  if (!array_hash) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "array_hash is NULL", 1 TSRMLS_CC);
    return FAILURE;
  }
  args->num_args = zend_hash_num_elements(array_hash);
  args->args = ecalloc(args->num_args, sizeof(grpc_arg));
  args_index = 0;

  char *key = NULL;
  zval *data;
  int key_type;

  PHP_GRPC_HASH_FOREACH_STR_KEY_VAL_START(array_hash, key, key_type, data)
    if (key_type != HASH_KEY_IS_STRING) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "args keys must be strings", 1 TSRMLS_CC);
      return FAILURE;
    }
    args->args[args_index].key = key;
    switch (Z_TYPE_P(data)) {
    case IS_LONG:
      args->args[args_index].value.integer = (int)Z_LVAL_P(data);
      args->args[args_index].type = GRPC_ARG_INTEGER;
      break;
    case IS_STRING:
      args->args[args_index].value.string = Z_STRVAL_P(data);
      args->args[args_index].type = GRPC_ARG_STRING;
      break;
    default:
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "args values must be int or string", 1 TSRMLS_CC);
      return FAILURE;
    }
    args_index++;
  PHP_GRPC_HASH_FOREACH_END()
  return SUCCESS;
}

void generate_sha1_str(char *sha1str, char *str, php_grpc_int len) {
  PHP_SHA1_CTX context;
  unsigned char digest[20];
  sha1str[0] = '\0';
  PHP_SHA1Init(&context);
  PHP_GRPC_SHA1Update(&context, str, len);
  PHP_SHA1Final(digest, &context);
  make_sha1_digest(sha1str, digest);
}

void create_channel(
    wrapped_grpc_channel *channel,
    char *target,
    grpc_channel_args args,
    wrapped_grpc_channel_credentials *creds) {
  if (creds == NULL) {
    channel->wrapper->wrapped = grpc_insecure_channel_create(target, &args,
                                                             NULL);
  } else {
    channel->wrapper->wrapped =
        grpc_secure_channel_create(creds->wrapped, target, &args, NULL);
  }
  efree(args.args);
}

void create_and_add_channel_to_persistent_list(
    wrapped_grpc_channel *channel,
    char *target,
    grpc_channel_args args,
    wrapped_grpc_channel_credentials *creds,
    char *key,
    php_grpc_int key_len) {
  php_grpc_zend_resource new_rsrc;
  channel_persistent_le_t *le;
  // this links each persistent list entry to a destructor
  new_rsrc.type = le_plink;
  le = malloc(sizeof(channel_persistent_le_t));

  create_channel(channel, target, args, creds);

  le->channel = channel->wrapper;
  new_rsrc.ptr = le;
  gpr_mu_lock(&global_persistent_list_mu);
  PHP_GRPC_PERSISTENT_LIST_UPDATE(&EG(persistent_list), key, key_len,
                                  (void *)&new_rsrc);
  gpr_mu_unlock(&global_persistent_list_mu);
}

/**
 * Construct an instance of the Channel class.
 *
 * By default, the underlying grpc_channel is "persistent". That is, given
 * the same set of parameters passed to the constructor, the same underlying
 * grpc_channel will be returned.
 *
 * If the $args array contains a "credentials" key mapping to a
 * ChannelCredentials object, a secure channel will be created with those
 * credentials.
 *
 * If the $args array contains a "force_new" key mapping to a boolean value
 * of "true", a new underlying grpc_channel will be created regardless. If
 * there are any opened channels on the same hostname, user must manually
 * call close() on those dangling channels before the end of the PHP
 * script.
 *
 * @param string $target The hostname to associate with this channel
 * @param array $args_array The arguments to pass to the Channel
 */
PHP_METHOD(Channel, __construct) {
  wrapped_grpc_channel *channel = Z_WRAPPED_GRPC_CHANNEL_P(getThis());
  zval *creds_obj = NULL;
  char *target;
  php_grpc_int target_length;
  zval *args_array = NULL;
  grpc_channel_args args;
  HashTable *array_hash;
  wrapped_grpc_channel_credentials *creds = NULL;
  php_grpc_zend_resource *rsrc;
  bool force_new = false;
  zval *force_new_obj = NULL;

  /* "sa" == 1 string, 1 array */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sa", &target,
                            &target_length, &args_array) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Channel expects a string and an array", 1 TSRMLS_CC);
    return;
  }
  array_hash = Z_ARRVAL_P(args_array);
  if (php_grpc_zend_hash_find(array_hash, "credentials", sizeof("credentials"),
                              (void **)&creds_obj) == SUCCESS) {
    if (Z_TYPE_P(creds_obj) == IS_NULL) {
      creds = NULL;
      php_grpc_zend_hash_del(array_hash, "credentials", sizeof("credentials"));
    } else if (PHP_GRPC_GET_CLASS_ENTRY(creds_obj) !=
               grpc_ce_channel_credentials) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "credentials must be a ChannelCredentials object",
                           1 TSRMLS_CC);
      return;
    } else {
      creds = Z_WRAPPED_GRPC_CHANNEL_CREDS_P(creds_obj);
      php_grpc_zend_hash_del(array_hash, "credentials", sizeof("credentials"));
    }
  }
  if (php_grpc_zend_hash_find(array_hash, "force_new", sizeof("force_new"),
                              (void **)&force_new_obj) == SUCCESS) {
    if (PHP_GRPC_BVAL_IS_TRUE(force_new_obj)) {
      force_new = true;
    }
    php_grpc_zend_hash_del(array_hash, "force_new", sizeof("force_new"));
  }

  // parse the rest of the channel args array
  if (php_grpc_read_args_array(args_array, &args TSRMLS_CC) == FAILURE) {
    return;
  }

  // Construct a hashkey for the persistent channel
  // Currently, the hashkey contains 3 parts:
  // 1. hostname
  // 2. hash value of the channel args array (excluding "credentials"
  //    and "force_new")
  // 3. (optional) hash value of the ChannelCredentials object
  php_serialize_data_t var_hash;
  smart_str buf = {0};
  PHP_VAR_SERIALIZE_INIT(var_hash);
  PHP_GRPC_VAR_SERIALIZE(&buf, args_array, &var_hash);
  PHP_VAR_SERIALIZE_DESTROY(var_hash);

  char sha1str[41];
  generate_sha1_str(sha1str, PHP_GRPC_SERIALIZED_BUF_STR(buf),
                    PHP_GRPC_SERIALIZED_BUF_LEN(buf));

  php_grpc_int key_len = target_length + strlen(sha1str);
  if (creds != NULL && creds->hashstr != NULL) {
    key_len += strlen(creds->hashstr);
  }
  char *key = malloc(key_len + 1);
  strcpy(key, target);
  strcat(key, sha1str);
  if (creds != NULL && creds->hashstr != NULL) {
    strcat(key, creds->hashstr);
  }
  channel->wrapper = malloc(sizeof(grpc_channel_wrapper));
  channel->wrapper->key = key;
  channel->wrapper->target = target;
  channel->wrapper->args_hashstr = sha1str;
  if (creds != NULL && creds->hashstr != NULL) {
    channel->wrapper->creds_hashstr = creds->hashstr;
  }
  gpr_mu_init(&channel->wrapper->mu);
  smart_str_free(&buf);

  if (force_new) {
    php_grpc_delete_persistent_list_entry(key, key_len TSRMLS_CC);
  }

  if (creds != NULL && creds->has_call_creds) {
    // If the ChannelCredentials object was composed with a CallCredentials
    // object, there is no way we can tell them apart. Do NOT persist
    // them. They should be individually destroyed.
    create_channel(channel, target, args, creds);
  } else if (!(PHP_GRPC_PERSISTENT_LIST_FIND(&EG(persistent_list), key,
                                             key_len, rsrc))) {
    create_and_add_channel_to_persistent_list(
        channel, target, args, creds, key, key_len);
  } else {
    // Found a previously stored channel in the persistent list
    channel_persistent_le_t *le = (channel_persistent_le_t *)rsrc->ptr;
    if (strcmp(target, le->channel->target) != 0 ||
        strcmp(sha1str, le->channel->args_hashstr) != 0 ||
        (creds != NULL && creds->hashstr != NULL &&
         strcmp(creds->hashstr, le->channel->creds_hashstr) != 0)) {
      // somehow hash collision
      create_and_add_channel_to_persistent_list(
          channel, target, args, creds, key, key_len);
    } else {
      channel->wrapper = le->channel;
    }
  }
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
PHP_METHOD(Channel, getTarget) {
  wrapped_grpc_channel *channel = Z_WRAPPED_GRPC_CHANNEL_P(getThis());
  gpr_mu_lock(&channel->wrapper->mu);
  if (channel->wrapper->wrapped == NULL) {
    zend_throw_exception(spl_ce_RuntimeException,
                         "Channel already closed", 1 TSRMLS_CC);
    gpr_mu_unlock(&channel->wrapper->mu);
    return;
  }
  char *target = grpc_channel_get_target(channel->wrapper->wrapped);
  gpr_mu_unlock(&channel->wrapper->mu);
  PHP_GRPC_RETURN_STRING(target, 1);
}

/**
 * Get the connectivity state of the channel
 * @param bool $try_to_connect Try to connect on the channel (optional)
 * @return long The grpc connectivity state
 */
PHP_METHOD(Channel, getConnectivityState) {
  wrapped_grpc_channel *channel = Z_WRAPPED_GRPC_CHANNEL_P(getThis());
  gpr_mu_lock(&channel->wrapper->mu);
  if (channel->wrapper->wrapped == NULL) {
    zend_throw_exception(spl_ce_RuntimeException,
                         "Channel already closed", 1 TSRMLS_CC);
    gpr_mu_unlock(&channel->wrapper->mu);
    return;
  }

  bool try_to_connect = false;

  /* "|b" == 1 optional bool */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|b", &try_to_connect)
      == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "getConnectivityState expects a bool", 1 TSRMLS_CC);
    gpr_mu_unlock(&channel->wrapper->mu);
    return;
  }
  int state = grpc_channel_check_connectivity_state(channel->wrapper->wrapped,
                                                    (int)try_to_connect);
  // this can happen if another shared Channel object close the underlying
  // channel
  if (state == GRPC_CHANNEL_SHUTDOWN) {
    channel->wrapper->wrapped = NULL;
  }
  gpr_mu_unlock(&channel->wrapper->mu);
  RETURN_LONG(state);
}

/**
 * Watch the connectivity state of the channel until it changed
 * @param long $last_state The previous connectivity state of the channel
 * @param Timeval $deadline_obj The deadline this function should wait until
 * @return bool If the connectivity state changes from last_state
 *              before deadline
 */
PHP_METHOD(Channel, watchConnectivityState) {
  wrapped_grpc_channel *channel = Z_WRAPPED_GRPC_CHANNEL_P(getThis());
  gpr_mu_lock(&channel->wrapper->mu);
  if (channel->wrapper->wrapped == NULL) {
    zend_throw_exception(spl_ce_RuntimeException,
                         "Channel already closed", 1 TSRMLS_CC);
    gpr_mu_unlock(&channel->wrapper->mu);
    return;
  }

  php_grpc_long last_state;
  zval *deadline_obj;

  /* "lO" == 1 long 1 object */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "lO",
                            &last_state, &deadline_obj,
                            grpc_ce_timeval) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "watchConnectivityState expects 1 long 1 timeval",
                         1 TSRMLS_CC);
    gpr_mu_unlock(&channel->wrapper->mu);
    return;
  }

  wrapped_grpc_timeval *deadline = Z_WRAPPED_GRPC_TIMEVAL_P(deadline_obj);
  grpc_channel_watch_connectivity_state(channel->wrapper->wrapped,
                                        (grpc_connectivity_state)last_state,
                                        deadline->wrapped, completion_queue,
                                        NULL);
  grpc_event event =
      grpc_completion_queue_pluck(completion_queue, NULL,
                                  gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  gpr_mu_unlock(&channel->wrapper->mu);
  RETURN_BOOL(event.success);
}

/**
 * Close the channel
 * @return void
 */
PHP_METHOD(Channel, close) {
  wrapped_grpc_channel *channel = Z_WRAPPED_GRPC_CHANNEL_P(getThis());
  gpr_mu_lock(&channel->wrapper->mu);
  if (channel->wrapper->wrapped != NULL) {
    grpc_channel_destroy(channel->wrapper->wrapped);
    channel->wrapper->wrapped = NULL;
  }

  php_grpc_delete_persistent_list_entry(channel->wrapper->key,
                                        strlen(channel->wrapper->key)
                                        TSRMLS_CC);
  gpr_mu_unlock(&channel->wrapper->mu);
}

// Delete an entry from the persistent list
// Note: this does not destroy or close the underlying grpc_channel
void php_grpc_delete_persistent_list_entry(char *key, php_grpc_int key_len
                                           TSRMLS_DC) {
  php_grpc_zend_resource *rsrc;
  gpr_mu_lock(&global_persistent_list_mu);
  if (PHP_GRPC_PERSISTENT_LIST_FIND(&EG(persistent_list), key,
                                    key_len, rsrc)) {
    channel_persistent_le_t *le;
    le = (channel_persistent_le_t *)rsrc->ptr;
    le->channel = NULL;
    php_grpc_zend_hash_del(&EG(persistent_list), key, key_len+1);
  }
  gpr_mu_unlock(&global_persistent_list_mu);
}

// A destructor associated with each list entry from the persistent list
static void php_grpc_channel_plink_dtor(php_grpc_zend_resource *rsrc
                                        TSRMLS_DC) {
  channel_persistent_le_t *le = (channel_persistent_le_t *)rsrc->ptr;
  if (le->channel != NULL) {
    gpr_mu_lock(&le->channel->mu);
    if (le->channel->wrapped != NULL) {
      grpc_channel_destroy(le->channel->wrapped);
      free(le->channel->key);
      free(le->channel);
    }
    gpr_mu_unlock(&le->channel->mu);
  }
  free(le);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_construct, 0, 0, 2)
  ZEND_ARG_INFO(0, target)
  ZEND_ARG_INFO(0, args)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_getTarget, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_getConnectivityState, 0, 0, 0)
  ZEND_ARG_INFO(0, try_to_connect)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_watchConnectivityState, 0, 0, 2)
  ZEND_ARG_INFO(0, last_state)
  ZEND_ARG_INFO(0, deadline)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_close, 0, 0, 0)
ZEND_END_ARG_INFO()

static zend_function_entry channel_methods[] = {
  PHP_ME(Channel, __construct, arginfo_construct,
         ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
  PHP_ME(Channel, getTarget, arginfo_getTarget,
         ZEND_ACC_PUBLIC)
  PHP_ME(Channel, getConnectivityState, arginfo_getConnectivityState,
         ZEND_ACC_PUBLIC)
  PHP_ME(Channel, watchConnectivityState, arginfo_watchConnectivityState,
         ZEND_ACC_PUBLIC)
  PHP_ME(Channel, close, arginfo_close,
         ZEND_ACC_PUBLIC)
  PHP_FE_END
};

GRPC_STARTUP_FUNCTION(channel) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Channel", channel_methods);
  ce.create_object = create_wrapped_grpc_channel;
  grpc_ce_channel = zend_register_internal_class(&ce TSRMLS_CC);
  le_plink = zend_register_list_destructors_ex(
      NULL, php_grpc_channel_plink_dtor, "Persistent Channel", module_number);
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_channel, channel_ce_handlers);
  return SUCCESS;
}
