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

/**
 * class Channel
 * @see https://github.com/grpc/grpc/tree/master/src/php/ext/grpc/channel.c
 */

#include "channel.h"

#include <ext/standard/php_var.h>
#include <ext/standard/sha1.h>
#include <zend_smart_str.h>
#include <ext/spl/spl_exceptions.h>
#include <zend_exceptions.h>

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "completion_queue.h"
#include "channel_credentials.h"
#include "timeval.h"

zend_class_entry *grpc_ce_channel;
PHP_GRPC_DECLARE_OBJECT_HANDLER(channel_ce_handlers)
static gpr_mu global_persistent_list_mu;
int le_plink;
int le_bound;
extern HashTable grpc_persistent_list;
extern HashTable grpc_target_upper_bound_map;

void free_grpc_channel_wrapper(grpc_channel_wrapper* channel, bool free_channel) {
  if (free_channel && channel->wrapped) {
    grpc_channel_destroy(channel->wrapped);
    channel->wrapped = NULL;
  }
  free(channel->target);
  free(channel->args_hashstr);
  free(channel->creds_hashstr);
  free(channel->key);
  channel->target = NULL;
  channel->args_hashstr = NULL;
  channel->creds_hashstr = NULL;
  channel->key = NULL;
}

void php_grpc_channel_ref(grpc_channel_wrapper* wrapper) {
  gpr_mu_lock(&wrapper->mu);
  wrapper->ref_count += 1;
  gpr_mu_unlock(&wrapper->mu);
}

void php_grpc_channel_unref(grpc_channel_wrapper* wrapper) {
  gpr_mu_lock(&wrapper->mu);
  wrapper->ref_count -= 1;
  if (wrapper->ref_count == 0) {
    free_grpc_channel_wrapper(wrapper, true);
    gpr_mu_unlock(&wrapper->mu);
    free(wrapper);
    wrapper = NULL;
    return;
  }
  gpr_mu_unlock(&wrapper->mu);
}

/* Frees and destroys an instance of wrapped_grpc_channel */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_channel)
  // In_persistent_list is used when the user don't close the channel,
  // In this case, channels not in the list should be freed.
  if (p->wrapper != NULL) {
    php_grpc_channel_unref(p->wrapper);
    p->wrapper = NULL;
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

static bool php_grpc_not_channel_arg_key(const char* key) {
  static const char* ignoredKeys[] = {
    "credentials",
    "force_new",
    "grpc_target_persist_bound",
  };

  for (int i = 0; i < sizeof(ignoredKeys) / sizeof(ignoredKeys[0]); i++) {
    if (strcmp(key, ignoredKeys[i]) == 0) {
      return true;
    }
  }
  return false;
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

  args->args = ecalloc(zend_hash_num_elements(array_hash), sizeof(grpc_arg));
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

    if (php_grpc_not_channel_arg_key(key)) {
      continue;
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
  args->num_args = args_index;
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

bool php_grpc_persistent_list_delete_unused_channel(
    char* target,
    target_bound_le_t* target_bound_status TSRMLS_DC) {
  zval *data;
  PHP_GRPC_HASH_FOREACH_VAL_START(&grpc_persistent_list, data)
    php_grpc_zend_resource *rsrc  = (php_grpc_zend_resource*) PHP_GRPC_HASH_VALPTR_TO_VAL(data)
    if (rsrc == NULL) {
      break;
    }
    channel_persistent_le_t* le = rsrc->ptr;
    // Find the channel sharing the same target.
    if (strcmp(le->channel->target, target) == 0) {
      // ref_count=1 means that only the map holds the reference to the channel.
      if (le->channel->ref_count == 1) {
        php_grpc_delete_persistent_list_entry(le->channel->key,
                                              strlen(le->channel->key)
                                              TSRMLS_CC);
        target_bound_status->current_count -= 1;
        if (target_bound_status->current_count < target_bound_status->upper_bound) {
          return true;
        }
      }
    }
  PHP_GRPC_HASH_FOREACH_END()
  return false;
}

target_bound_le_t* update_and_get_target_upper_bound(char* target, int bound) {
  php_grpc_zend_resource *rsrc;
  target_bound_le_t* target_bound_status;
  php_grpc_int key_len = strlen(target);
  if (!(PHP_GRPC_PERSISTENT_LIST_FIND(&grpc_target_upper_bound_map, target,
      key_len, rsrc))) {
    // Target is not persisted.
    php_grpc_zend_resource new_rsrc;
    target_bound_status = malloc(sizeof(target_bound_le_t));
    if (bound == -1) {
      // If the bound is not set, use 1 as default.s
      bound = 1;
    }
    target_bound_status->upper_bound = bound;
    // Init current_count with 1. It should be add 1 when the channel is successfully
    // created and minus 1 when it is removed from the persistent list.
    target_bound_status->current_count = 0;
    new_rsrc.type = le_bound;
    new_rsrc.ptr = target_bound_status;
    gpr_mu_lock(&global_persistent_list_mu);
    PHP_GRPC_PERSISTENT_LIST_UPDATE(&grpc_target_upper_bound_map,
                                    target, key_len, (void *)&new_rsrc);
    gpr_mu_unlock(&global_persistent_list_mu);
  } else {
    // The target already in the map recording the upper bound.
    // If no newer bound set, use the original now.
    target_bound_status = (target_bound_le_t *)rsrc->ptr;
    if (bound != -1) {
      target_bound_status->upper_bound = bound;
    }
  }
  return target_bound_status;
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
  // There is an Grpc\Channel object refer to it.
  php_grpc_channel_ref(channel->wrapper);
  efree(args.args);
}

void create_and_add_channel_to_persistent_list(
    wrapped_grpc_channel *channel,
    char *target,
    grpc_channel_args args,
    wrapped_grpc_channel_credentials *creds,
    char *key,
    php_grpc_int key_len,
    int target_upper_bound TSRMLS_DC) {
  target_bound_le_t* target_bound_status =
    update_and_get_target_upper_bound(target, target_upper_bound);
  // Check the upper bound status before inserting to the persistent map.
  if (target_bound_status->current_count >=
      target_bound_status->upper_bound) {
    if (!php_grpc_persistent_list_delete_unused_channel(
          target, target_bound_status TSRMLS_CC)) {
      // If no channel can be deleted from the persistent map,
      // do not persist this one.
      create_channel(channel, target, args, creds);
      gpr_log(GPR_INFO, "[Warning] The number of channel for the"
                 " target %s is maxed out bounded.\n", target);
      gpr_log(GPR_INFO, "[Warning] Target upper bound: %d. Current size: %d.\n",
                 target_bound_status->upper_bound,
                 target_bound_status->current_count);
      gpr_log(GPR_INFO, "[Warning] Target %s will not be persisted.\n", target);
      return;
    }
  }
  // There is space in the persistent map.
  php_grpc_zend_resource new_rsrc;
  channel_persistent_le_t *le;
  // this links each persistent list entry to a destructor
  new_rsrc.type = le_plink;
  le = malloc(sizeof(channel_persistent_le_t));

  create_channel(channel, target, args, creds);
  target_bound_status->current_count += 1;

  le->channel = channel->wrapper;
  new_rsrc.ptr = le;
  gpr_mu_lock(&global_persistent_list_mu);
  PHP_GRPC_PERSISTENT_LIST_UPDATE(&grpc_persistent_list, key, key_len,
                                  (void *)&new_rsrc);
  // Persistent map refer to it.
  php_grpc_channel_ref(channel->wrapper);
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
 * of "true", a new and separate underlying grpc_channel will be created
 * and returned. This will not affect existing channels.
 *
 * @param string $target The hostname to associate with this channel
 * @param array $args_array The arguments to pass to the Channel
 */
PHP_METHOD(Channel, __construct) {
  wrapped_grpc_channel *channel =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel, getThis());
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
  int target_upper_bound = -1;

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
    } else if (PHP_GRPC_GET_CLASS_ENTRY(creds_obj) !=
               grpc_ce_channel_credentials) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "credentials must be a ChannelCredentials object",
                           1 TSRMLS_CC);
      return;
    } else {
      creds = PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel_credentials,
                                          creds_obj);
    }
  }
  if (php_grpc_zend_hash_find(array_hash, "force_new", sizeof("force_new"),
                              (void **)&force_new_obj) == SUCCESS) {
    if (PHP_GRPC_BVAL_IS_TRUE(force_new_obj)) {
      force_new = true;
    }
  }

  if (php_grpc_zend_hash_find(array_hash, "grpc_target_persist_bound",
                              sizeof("grpc_target_persist_bound"),
                              (void **)&force_new_obj) == SUCCESS) {
    if (Z_TYPE_P(force_new_obj) != IS_LONG) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "plist_bound must be a number",
                           1 TSRMLS_CC);
    }
    target_upper_bound = (int)Z_LVAL_P(force_new_obj);
  }

  // parse the rest of the channel args array
  if (php_grpc_read_args_array(args_array, &args TSRMLS_CC) == FAILURE) {
    efree(args.args);
    return;
  }

  // Construct a hashkey for the persistent channel
  // Currently, the hashkey contains 3 parts:
  // 1. hostname
  // 2. hash value of the channel args (args_array excluding "credentials",
  //    "force_new" and "grpc_target_persist_bound")
  // 3. (optional) hash value of the ChannelCredentials object

  char sha1str[41] = { 0 };
  unsigned char digest[20] = { 0 };
  PHP_SHA1_CTX context;
  PHP_SHA1Init(&context);
  for (int i = 0; i < args.num_args; i++) {
    PHP_GRPC_SHA1Update(&context, args.args[i].key, strlen(args.args[i].key) + 1);
    switch (args.args[i].type) {
    case GRPC_ARG_INTEGER:
      PHP_GRPC_SHA1Update(&context, &args.args[i].value.integer, 4);
      break;
    case GRPC_ARG_STRING:
      PHP_GRPC_SHA1Update(&context, args.args[i].value.string, strlen(args.args[i].value.string) + 1);
      break;
    default:
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "args values must be int or string", 1 TSRMLS_CC);
      return;
    }
  };
  PHP_SHA1Final(digest, &context);
  make_sha1_digest(sha1str, digest);

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
  channel->wrapper->ref_count = 0;
  channel->wrapper->key = key;
  channel->wrapper->target = strdup(target);
  channel->wrapper->args_hashstr = strdup(sha1str);
  channel->wrapper->creds_hashstr = NULL;
  channel->wrapper->creds = creds;
  channel->wrapper->args = args;
  if (creds != NULL && creds->hashstr != NULL) {
    php_grpc_int creds_hashstr_len = strlen(creds->hashstr);
    char *channel_creds_hashstr = malloc(creds_hashstr_len + 1);
    strcpy(channel_creds_hashstr, creds->hashstr);
    channel->wrapper->creds_hashstr = channel_creds_hashstr;
  }

  gpr_mu_init(&channel->wrapper->mu);
  if (force_new || (creds != NULL && creds->has_call_creds)) {
    // If the ChannelCredentials object was composed with a CallCredentials
    // object, there is no way we can tell them apart. Do NOT persist
    // them. They should be individually destroyed.
    create_channel(channel, target, args, creds);
  } else if (!(PHP_GRPC_PERSISTENT_LIST_FIND(&grpc_persistent_list, key,
                                             key_len, rsrc))) {
    create_and_add_channel_to_persistent_list(
        channel, target, args, creds, key, key_len, target_upper_bound TSRMLS_CC);
  } else {
    // Found a previously stored channel in the persistent list
    channel_persistent_le_t *le = (channel_persistent_le_t *)rsrc->ptr;
    if (strcmp(target, le->channel->target) != 0 ||
        strcmp(sha1str, le->channel->args_hashstr) != 0 ||
        (creds != NULL && creds->hashstr != NULL &&
         strcmp(creds->hashstr, le->channel->creds_hashstr) != 0)) {
      // somehow hash collision
      create_and_add_channel_to_persistent_list(
          channel, target, args, creds, key, key_len, target_upper_bound TSRMLS_CC);
    } else {
      efree(args.args);
      free_grpc_channel_wrapper(channel->wrapper, false);
      gpr_mu_destroy(&channel->wrapper->mu);
      free(channel->wrapper);
      channel->wrapper = NULL;
      channel->wrapper = le->channel;
      // One more Grpc\Channel object refer to it.
      php_grpc_channel_ref(channel->wrapper);
      update_and_get_target_upper_bound(target, target_upper_bound);
    }
  }
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
PHP_METHOD(Channel, getTarget) {
  wrapped_grpc_channel *channel =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel, getThis());
  if (channel->wrapper == NULL) {
    zend_throw_exception(spl_ce_RuntimeException,
                         "getTarget error."
                         "Channel is already closed.", 1 TSRMLS_CC);
    return;
  }
  gpr_mu_lock(&channel->wrapper->mu);
  char *target = grpc_channel_get_target(channel->wrapper->wrapped);
  gpr_mu_unlock(&channel->wrapper->mu);
  PHP_GRPC_RETVAL_STRING(target, 1);
  gpr_free(target);
}

/**
 * Get the connectivity state of the channel
 * @param bool $try_to_connect Try to connect on the channel (optional)
 * @return long The grpc connectivity state
 */
PHP_METHOD(Channel, getConnectivityState) {
  wrapped_grpc_channel *channel =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel, getThis());
  if (channel->wrapper == NULL) {
    zend_throw_exception(spl_ce_RuntimeException,
                         "getConnectivityState error."
                         "Channel is already closed.", 1 TSRMLS_CC);
    return;
  }
  gpr_mu_lock(&channel->wrapper->mu);
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
  wrapped_grpc_channel *channel =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel, getThis());
  if (channel->wrapper == NULL) {
    zend_throw_exception(spl_ce_RuntimeException,
                         "watchConnectivityState error"
                         "Channel is already closed.", 1 TSRMLS_CC);
    return;
  }
  gpr_mu_lock(&channel->wrapper->mu);
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

  wrapped_grpc_timeval *deadline =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_timeval, deadline_obj);
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
  wrapped_grpc_channel *channel =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel, getThis());
  if (channel->wrapper != NULL) {
    php_grpc_channel_unref(channel->wrapper);
    channel->wrapper = NULL;
  }
}

// Delete an entry from the persistent list
// Note: this does not destroy or close the underlying grpc_channel
void php_grpc_delete_persistent_list_entry(char *key, php_grpc_int key_len
                                           TSRMLS_DC) {
  php_grpc_zend_resource *rsrc;
  gpr_mu_lock(&global_persistent_list_mu);
  if (PHP_GRPC_PERSISTENT_LIST_FIND(&grpc_persistent_list, key,
                                    key_len, rsrc)) {
    php_grpc_zend_hash_del(&grpc_persistent_list, key, key_len+1);
  }
  gpr_mu_unlock(&global_persistent_list_mu);
}

// A destructor associated with each list entry from the persistent list
static void php_grpc_channel_plink_dtor(php_grpc_zend_resource *rsrc
                                        TSRMLS_DC) {
  channel_persistent_le_t *le = (channel_persistent_le_t *)rsrc->ptr;
  if (le == NULL) {
    return;
  }
  if (le->channel != NULL) {
    php_grpc_channel_unref(le->channel);
    le->channel = NULL;
  }
  free(le);
  le = NULL;
}

// A destructor associated with each list entry from the target_bound map
static void php_grpc_target_bound_dtor(php_grpc_zend_resource *rsrc
                                        TSRMLS_DC) {
  target_bound_le_t *le = (target_bound_le_t *) rsrc->ptr;
  if (le == NULL) {
    return;
  }
  free(le);
  le = NULL;
}

#ifdef GRPC_PHP_DEBUG

/**
* Clean all channels in the persistent. Test only.
* @return void
*/
PHP_METHOD(Channel, cleanPersistentList) {
  zend_hash_clean(&grpc_persistent_list);
  zend_hash_clean(&grpc_target_upper_bound_map);
}

char *grpc_connectivity_state_name(grpc_connectivity_state state) {
 switch (state) {
   case GRPC_CHANNEL_IDLE:
     return "IDLE";
   case GRPC_CHANNEL_CONNECTING:
     return "CONNECTING";
   case GRPC_CHANNEL_READY:
     return "READY";
   case GRPC_CHANNEL_TRANSIENT_FAILURE:
     return "TRANSIENT_FAILURE";
   case GRPC_CHANNEL_SHUTDOWN:
     return "SHUTDOWN";
 }
 return "UNKNOWN";
}

/**
* Return the info about the current channel. Test only.
* @return array
*/
PHP_METHOD(Channel, getChannelInfo) {
  wrapped_grpc_channel *channel =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel, getThis());
  array_init(return_value);
   // Info about the target
  PHP_GRPC_ADD_STRING_TO_ARRAY(return_value, "target",
              sizeof("target"), channel->wrapper->target, true);
  // Info about the upper bound for the target
  target_bound_le_t* target_bound_status =
    update_and_get_target_upper_bound(channel->wrapper->target, -1);
  PHP_GRPC_ADD_LONG_TO_ARRAY(return_value, "target_upper_bound",
    sizeof("target_upper_bound"), target_bound_status->upper_bound);
  PHP_GRPC_ADD_LONG_TO_ARRAY(return_value, "target_current_size",
    sizeof("target_current_size"), target_bound_status->current_count);
  // Info about key
  PHP_GRPC_ADD_STRING_TO_ARRAY(return_value, "key",
              sizeof("key"), channel->wrapper->key, true);
  // Info about persistent channel ref_count
  PHP_GRPC_ADD_LONG_TO_ARRAY(return_value, "ref_count",
              sizeof("ref_count"), channel->wrapper->ref_count);
  // Info about connectivity status
  int state =
      grpc_channel_check_connectivity_state(channel->wrapper->wrapped, (int)0);
  // It should be set to 'true' in PHP 5.6.33
  PHP_GRPC_ADD_LONG_TO_ARRAY(return_value, "connectivity_status",
              sizeof("connectivity_status"), state);
  PHP_GRPC_ADD_STRING_TO_ARRAY(return_value, "ob",
              sizeof("ob"),
              grpc_connectivity_state_name(state), true);
  // Info about the channel is closed or not
  PHP_GRPC_ADD_BOOL_TO_ARRAY(return_value, "is_valid",
              sizeof("is_valid"), (channel->wrapper == NULL));
}

/**
* Return an array of all channels in the persistent list. Test only.
* @return array
*/
PHP_METHOD(Channel, getPersistentList) {
  array_init(return_value);
  zval *data;
  PHP_GRPC_HASH_FOREACH_VAL_START(&grpc_persistent_list, data)
    php_grpc_zend_resource *rsrc  =
                (php_grpc_zend_resource*) PHP_GRPC_HASH_VALPTR_TO_VAL(data)
    if (rsrc == NULL) {
      break;
    }
    channel_persistent_le_t* le = rsrc->ptr;
    zval* ret_arr;
    PHP_GRPC_MAKE_STD_ZVAL(ret_arr);
    array_init(ret_arr);
    // Info about the target
    PHP_GRPC_ADD_STRING_TO_ARRAY(ret_arr, "target",
                sizeof("target"), le->channel->target, true);
    // Info about the upper bound for the target
    target_bound_le_t* target_bound_status =
      update_and_get_target_upper_bound(le->channel->target, -1);
    PHP_GRPC_ADD_LONG_TO_ARRAY(ret_arr, "target_upper_bound",
      sizeof("target_upper_bound"), target_bound_status->upper_bound);
    PHP_GRPC_ADD_LONG_TO_ARRAY(ret_arr, "target_current_size",
      sizeof("target_current_size"), target_bound_status->current_count);
    // Info about key
    PHP_GRPC_ADD_STRING_TO_ARRAY(ret_arr, "key",
                sizeof("key"), le->channel->key, true);
    // Info about persistent channel ref_count
    PHP_GRPC_ADD_LONG_TO_ARRAY(ret_arr, "ref_count",
                sizeof("ref_count"), le->channel->ref_count);
    // Info about connectivity status
    int state =
        grpc_channel_check_connectivity_state(le->channel->wrapped, (int)0);
    // It should be set to 'true' in PHP 5.6.33
    PHP_GRPC_ADD_LONG_TO_ARRAY(ret_arr, "connectivity_status",
                sizeof("connectivity_status"), state);
    PHP_GRPC_ADD_STRING_TO_ARRAY(ret_arr, "ob",
                sizeof("ob"),
                grpc_connectivity_state_name(state), true);
    add_assoc_zval(return_value, le->channel->key, ret_arr);
    PHP_GRPC_FREE_STD_ZVAL(ret_arr);
  PHP_GRPC_HASH_FOREACH_END()
}
#endif


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

#ifdef GRPC_PHP_DEBUG
ZEND_BEGIN_ARG_INFO_EX(arginfo_getChannelInfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cleanPersistentList, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_getPersistentList, 0, 0, 0)
ZEND_END_ARG_INFO()
#endif


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
  #ifdef GRPC_PHP_DEBUG
  PHP_ME(Channel, getChannelInfo, arginfo_getChannelInfo,
         ZEND_ACC_PUBLIC)
  PHP_ME(Channel, cleanPersistentList, arginfo_cleanPersistentList,
         ZEND_ACC_PUBLIC)
  PHP_ME(Channel, getPersistentList, arginfo_getPersistentList,
         ZEND_ACC_PUBLIC)
  #endif
  PHP_FE_END
};

GRPC_STARTUP_FUNCTION(channel) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Channel", channel_methods);
  ce.create_object = create_wrapped_grpc_channel;
  grpc_ce_channel = zend_register_internal_class(&ce TSRMLS_CC);
  gpr_mu_init(&global_persistent_list_mu);
  le_plink = zend_register_list_destructors_ex(
      NULL, php_grpc_channel_plink_dtor, "Persistent Channel", module_number);
  ZEND_HASH_INIT(&grpc_persistent_list, 20, EG(persistent_list).pDestructor, 1);
  // Register the target->upper_bound map.
  le_bound = zend_register_list_destructors_ex(
      NULL, php_grpc_target_bound_dtor, "Target Bound", module_number);
  ZEND_HASH_INIT(&grpc_target_upper_bound_map, 20, EG(persistent_list).pDestructor, 1);
  
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_channel, channel_ce_handlers);
  return SUCCESS;
}
