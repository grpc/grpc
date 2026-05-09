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
 * class CallCredentials
 * @see https://github.com/grpc/grpc/tree/master/src/php/ext/grpc/call_credentials.c
 */

#include "call_credentials.h"

#include <ext/spl/spl_exceptions.h>
#include <zend_exceptions.h>

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "call.h"

zend_class_entry *grpc_ce_call_credentials;
PHP_GRPC_DECLARE_OBJECT_HANDLER(call_credentials_ce_handlers)

static gpr_mu grpc_php_call_credentials_registry_mu;
static plugin_state *grpc_php_call_credentials_registry_head = NULL;
static int grpc_php_call_credentials_registry_initialized = 0;

void grpc_php_call_credentials_module_init(void) {
  if (!grpc_php_call_credentials_registry_initialized) {
    gpr_mu_init(&grpc_php_call_credentials_registry_mu);
    grpc_php_call_credentials_registry_head = NULL;
    grpc_php_call_credentials_registry_initialized = 1;
  }
}

void grpc_php_call_credentials_module_shutdown(void) {
  if (grpc_php_call_credentials_registry_initialized) {
    gpr_mu_destroy(&grpc_php_call_credentials_registry_mu);
    grpc_php_call_credentials_registry_head = NULL;
    grpc_php_call_credentials_registry_initialized = 0;
  }
}

static void plugin_state_register(plugin_state *state) {
  gpr_mu_lock(&grpc_php_call_credentials_registry_mu);
  state->prev = NULL;
  state->next = grpc_php_call_credentials_registry_head;
  if (grpc_php_call_credentials_registry_head != NULL) {
    grpc_php_call_credentials_registry_head->prev = state;
  }
  grpc_php_call_credentials_registry_head = state;
  gpr_mu_unlock(&grpc_php_call_credentials_registry_mu);
}

static void plugin_state_unregister(plugin_state *state) {
  gpr_mu_lock(&grpc_php_call_credentials_registry_mu);
  if (state->prev != NULL) {
    state->prev->next = state->next;
  } else if (grpc_php_call_credentials_registry_head == state) {
    grpc_php_call_credentials_registry_head = state->next;
  }
  if (state->next != NULL) {
    state->next->prev = state->prev;
  }
  state->prev = NULL;
  state->next = NULL;
  gpr_mu_unlock(&grpc_php_call_credentials_registry_mu);
}

static int plugin_state_detach_locked(plugin_state *state,
                                      zend_fcall_info **out_fci,
                                      zend_fcall_info_cache **out_fci_cache,
                                      zval *out_function_name,
                                      zend_bool *out_function_name_addref) {
  *out_fci = state->fci;
  *out_fci_cache = state->fci_cache;
  *out_function_name_addref = state->function_name_added_ref;
  if (state->function_name_added_ref && state->fci != NULL) {
    *out_function_name = state->fci->function_name;
  }
  state->fci = NULL;
  state->fci_cache = NULL;
  state->function_name_added_ref = 0;
  return *out_fci != NULL || *out_fci_cache != NULL;
}

static void plugin_state_release_detached(zend_fcall_info *fci,
                                          zend_fcall_info_cache *fci_cache,
                                          zval *function_name,
                                          zend_bool function_name_addref) {
  if (function_name_addref) {
    zval_ptr_dtor(function_name);
  }
  if (fci != NULL) free(fci);
  if (fci_cache != NULL) free(fci_cache);
}

void grpc_php_call_credentials_request_shutdown(void) {
  if (!grpc_php_call_credentials_registry_initialized) {
    return;
  }

  while (1) {
    zend_fcall_info *fci = NULL;
    zend_fcall_info_cache *fci_cache = NULL;
    zval function_name;
    zend_bool addref = 0;
    int found = 0;

    gpr_mu_lock(&grpc_php_call_credentials_registry_mu);
    for (plugin_state *s = grpc_php_call_credentials_registry_head; s != NULL;
         s = s->next) {
      gpr_mu_lock(&s->mu);
      if (!s->invalidated) {
        plugin_state_detach_locked(s, &fci, &fci_cache, &function_name, &addref);
        s->invalidated = 1;
        gpr_mu_unlock(&s->mu);
        found = 1;
        break;
      }
      gpr_mu_unlock(&s->mu);
    }
    gpr_mu_unlock(&grpc_php_call_credentials_registry_mu);

    if (!found) break;
    plugin_state_release_detached(fci, fci_cache, &function_name, addref);
  }
}

/* Frees and destroys an instance of wrapped_grpc_call_credentials */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_call_credentials)
  if (p->wrapped != NULL) {
    grpc_call_credentials_release(p->wrapped);
  }
PHP_GRPC_FREE_WRAPPED_FUNC_END()

/* Initializes an instance of wrapped_grpc_call_credentials to be
 * associated with an object of a class specified by class_type */
php_grpc_zend_object create_wrapped_grpc_call_credentials(
    zend_class_entry *class_type TSRMLS_DC) {
  PHP_GRPC_ALLOC_CLASS_OBJECT(wrapped_grpc_call_credentials);
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  PHP_GRPC_FREE_CLASS_OBJECT(wrapped_grpc_call_credentials,
                             call_credentials_ce_handlers);
}

zval *grpc_php_wrap_call_credentials(grpc_call_credentials
                                     *wrapped TSRMLS_DC) {
  zval *credentials_object;
  PHP_GRPC_MAKE_STD_ZVAL(credentials_object);
  object_init_ex(credentials_object, grpc_ce_call_credentials);
  wrapped_grpc_call_credentials *credentials =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call_credentials,
                                credentials_object);
  credentials->wrapped = wrapped;
  return credentials_object;
}

/**
 * Create composite credentials from two existing credentials.
 * @param CallCredentials $cred1_obj The first credential
 * @param CallCredentials $cred2_obj The second credential
 * @return CallCredentials The new composite credentials object
 */
PHP_METHOD(CallCredentials, createComposite) {
  zval *cred1_obj;
  zval *cred2_obj;

  /* "OO" == 2 Objects */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "OO", &cred1_obj,
                            grpc_ce_call_credentials, &cred2_obj,
                            grpc_ce_call_credentials) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "createComposite expects 2 CallCredentials",
                         1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_call_credentials *cred1 =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call_credentials, cred1_obj);
  wrapped_grpc_call_credentials *cred2 =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call_credentials, cred2_obj);
  grpc_call_credentials *creds =
    grpc_composite_call_credentials_create(cred1->wrapped, cred2->wrapped,
                                           NULL);
  zval *creds_object = grpc_php_wrap_call_credentials(creds TSRMLS_CC);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create a call credentials object from the plugin API
 * @param function $fci The callback function
 * @return CallCredentials The new call credentials object
 */
PHP_METHOD(CallCredentials, createFromPlugin) {
  zend_fcall_info *fci;
  zend_fcall_info_cache *fci_cache;

  fci = (zend_fcall_info *)malloc(sizeof(zend_fcall_info));
  fci_cache = (zend_fcall_info_cache *)malloc(sizeof(zend_fcall_info_cache));
  memset(fci, 0, sizeof(zend_fcall_info));
  memset(fci_cache, 0, sizeof(zend_fcall_info_cache));

  /* "f" == 1 function */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "f*", fci, fci_cache,
                            fci->params, fci->param_count) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "createFromPlugin expects 1 callback", 1 TSRMLS_CC);
    free(fci);
    free(fci_cache);
    return;
  }

  plugin_state *state;
  state = (plugin_state *)malloc(sizeof(plugin_state));
  memset(state, 0, sizeof(plugin_state));
  gpr_mu_init(&state->mu);

  /* save the user provided PHP callback function. Addref the callable so it
   * stays alive while we reference it; the matching dtor runs in
   * plugin_destroy_state or in the PHP_RSHUTDOWN sweep, whichever comes first. */
  state->fci = fci;
  state->fci_cache = fci_cache;
  Z_TRY_ADDREF(fci->function_name);
  state->function_name_added_ref = 1;
  plugin_state_register(state);

  grpc_metadata_credentials_plugin plugin;
  plugin.get_metadata = plugin_get_metadata;
  plugin.destroy = plugin_destroy_state;
  plugin.state = (void *)state;
  plugin.type = "";
  // TODO(yihuazhang): Expose min_security_level via the PHP API so that
  // applications can decide what minimum security level their plugins require.
  grpc_call_credentials *creds =
    grpc_metadata_credentials_create_from_plugin(plugin, GRPC_PRIVACY_AND_INTEGRITY, NULL);
  zval *creds_object = grpc_php_wrap_call_credentials(creds TSRMLS_CC);
  RETURN_DESTROY_ZVAL(creds_object);
}

/* Callback function for plugin creds API*/
int plugin_get_metadata(
    void *ptr, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t *num_creds_md, grpc_status_code *status,
    const char **error_details) {
  TSRMLS_FETCH();

  plugin_state *state = (plugin_state *)ptr;

  *num_creds_md = 0;
  *status = GRPC_STATUS_OK;
  *error_details = NULL;

  gpr_mu_lock(&state->mu);
  if (state->invalidated || state->fci == NULL || state->fci_cache == NULL) {
    *status = GRPC_STATUS_UNAVAILABLE;
    *error_details = gpr_strdup(
        "PHP plugin credentials callback was invoked after its owning PHP "
        "request had ended");
    gpr_mu_unlock(&state->mu);
    return true;
  }

  /* prepare to call the user callback function with info from the
   * grpc_auth_metadata_context */
  zval *arg;
  PHP_GRPC_MAKE_STD_ZVAL(arg);
  object_init(arg);
  php_grpc_add_property_string(arg, "service_url", context.service_url, true);
  php_grpc_add_property_string(arg, "method_name", context.method_name, true);
  zval *retval = NULL;
  PHP_GRPC_MAKE_STD_ZVAL(retval);
  state->fci->params = arg;
  state->fci->retval = retval;
  state->fci->param_count = 1;

  PHP_GRPC_DELREF(arg);

  /* call the user callback function */
  PHP_GRPC_CALL_FUNCTION(state->fci, state->fci_cache);

  bool should_return = false;
  grpc_metadata_array metadata;

  if (retval == NULL || Z_TYPE_P(retval) != IS_ARRAY) {
    *status = GRPC_STATUS_INVALID_ARGUMENT;
    should_return = true;  // Synchronous return.
  }
  if (!create_metadata_array(retval, &metadata)) {
    *status = GRPC_STATUS_INVALID_ARGUMENT;
    should_return = true;  // Synchronous return.
    grpc_php_metadata_array_destroy_including_entries(&metadata);
  }

  if (retval != NULL) {
    zval_ptr_dtor(arg);
    zval_ptr_dtor(retval);
    PHP_GRPC_FREE_STD_ZVAL(arg);
    PHP_GRPC_FREE_STD_ZVAL(retval);
  }
  if (should_return) {
    gpr_mu_unlock(&state->mu);
    return true;
  }

  if (metadata.count > GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX) {
    *status = GRPC_STATUS_INTERNAL;
    *error_details = gpr_strdup(
        "PHP plugin credentials returned too many metadata entries");
    for (size_t i = 0; i < metadata.count; i++) {
      // TODO(stanleycheung): Why don't we need to unref the key here?
      grpc_slice_unref(metadata.metadata[i].value);
    }
  } else {
    // Return data to core.
    *num_creds_md = metadata.count;
    for (size_t i = 0; i < metadata.count; ++i) {
      creds_md[i] = metadata.metadata[i];
    }
  }

  grpc_metadata_array_destroy(&metadata);
  gpr_mu_unlock(&state->mu);
  return true;  // Synchronous return.
}

/* Cleanup function for plugin creds API*/
void plugin_destroy_state(void *ptr) {
  plugin_state *state = (plugin_state *)ptr;
  plugin_state_unregister(state);

  zend_fcall_info *fci = NULL;
  zend_fcall_info_cache *fci_cache = NULL;
  zval function_name;
  zend_bool addref = 0;

  gpr_mu_lock(&state->mu);
  if (!state->invalidated) {
    plugin_state_detach_locked(state, &fci, &fci_cache, &function_name, &addref);
    state->invalidated = 1;
  }
  gpr_mu_unlock(&state->mu);
  gpr_mu_destroy(&state->mu);
  free(state);

  plugin_state_release_detached(fci, fci_cache, &function_name, addref);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_createComposite, 0, 0, 2)
  ZEND_ARG_INFO(0, creds1)
  ZEND_ARG_INFO(0, creds2)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_createFromPlugin, 0, 0, 1)
  ZEND_ARG_INFO(0, callback)
ZEND_END_ARG_INFO()

static zend_function_entry call_credentials_methods[] = {
  PHP_ME(CallCredentials, createComposite, arginfo_createComposite,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(CallCredentials, createFromPlugin, arginfo_createFromPlugin,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_FE_END
};

void grpc_init_call_credentials(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\CallCredentials", call_credentials_methods);
  ce.create_object = create_wrapped_grpc_call_credentials;
  grpc_ce_call_credentials = zend_register_internal_class(&ce TSRMLS_CC);
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_call_credentials,
                        call_credentials_ce_handlers);
}
