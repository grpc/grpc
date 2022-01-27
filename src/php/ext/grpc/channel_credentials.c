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
 * class ChannelCredentials
 * @see https://github.com/grpc/grpc/tree/master/src/php/ext/grpc/channel_credentials.c
 */

#include "channel_credentials.h"

#include <ext/standard/sha1.h>
#include <ext/spl/spl_exceptions.h>
#include <zend_exceptions.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "call_credentials.h"
#include "channel.h"

zend_class_entry *grpc_ce_channel_credentials;
PHP_GRPC_DECLARE_OBJECT_HANDLER(channel_credentials_ce_handlers)
static char *default_pem_root_certs = NULL;

static grpc_ssl_roots_override_result get_ssl_roots_override(
    char **pem_root_certs) {
  if (!default_pem_root_certs) {
    *pem_root_certs = NULL;
    return GRPC_SSL_ROOTS_OVERRIDE_FAIL;
  }
  *pem_root_certs = gpr_strdup(default_pem_root_certs);
  return GRPC_SSL_ROOTS_OVERRIDE_OK;
}

/* Frees and destroys an instance of wrapped_grpc_channel_credentials */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_channel_credentials)
  if (p->hashstr != NULL) {
    free(p->hashstr);
    p->hashstr = NULL;
  }
  if (p->wrapped != NULL) {
    grpc_channel_credentials_release(p->wrapped);
    p->wrapped = NULL;
  }
PHP_GRPC_FREE_WRAPPED_FUNC_END()

/* Initializes an instance of wrapped_grpc_channel_credentials to be
 * associated with an object of a class specified by class_type */
php_grpc_zend_object create_wrapped_grpc_channel_credentials(
    zend_class_entry *class_type TSRMLS_DC) {
  PHP_GRPC_ALLOC_CLASS_OBJECT(wrapped_grpc_channel_credentials);
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  PHP_GRPC_FREE_CLASS_OBJECT(wrapped_grpc_channel_credentials,
                             channel_credentials_ce_handlers);
}

zval *grpc_php_wrap_channel_credentials(grpc_channel_credentials *wrapped,
                                        char *hashstr,
                                        zend_bool has_call_creds TSRMLS_DC) {
  zval *credentials_object;
  PHP_GRPC_MAKE_STD_ZVAL(credentials_object);
  object_init_ex(credentials_object, grpc_ce_channel_credentials);
  wrapped_grpc_channel_credentials *credentials =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel_credentials,
                                credentials_object);
  credentials->wrapped = wrapped;
  credentials->hashstr = hashstr;
  credentials->has_call_creds = has_call_creds;
  return credentials_object;
}

/**
 * Set default roots pem.
 * @param string $pem_roots PEM encoding of the server root certificates
 * @return void
 */
PHP_METHOD(ChannelCredentials, setDefaultRootsPem) {
  char *pem_roots;
  php_grpc_int pem_roots_length;

  /* "s" == 1 string */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &pem_roots,
                            &pem_roots_length) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "setDefaultRootsPem expects 1 string", 1 TSRMLS_CC);
    return;
  }
  default_pem_root_certs = gpr_realloc(default_pem_root_certs, (pem_roots_length + 1) * sizeof(char));
  memcpy(default_pem_root_certs, pem_roots, pem_roots_length + 1);
}

/**
 * if default roots pem is set
 * @return TRUE/FALSE
 */
PHP_METHOD(ChannelCredentials, isDefaultRootsPemSet) {
  if (default_pem_root_certs) {
    RETURN_TRUE;
  }
  RETURN_FALSE;
}

/**
 * free default roots pem, if it is set
 */
PHP_METHOD(ChannelCredentials, invalidateDefaultRootsPem) {
  if (default_pem_root_certs) {
    gpr_free(default_pem_root_certs);
    default_pem_root_certs = NULL;
  }
}

/**
 * Create a default channel credentials object.
 * @return ChannelCredentials The new default channel credentials object
 */
PHP_METHOD(ChannelCredentials, createDefault) {
  grpc_channel_credentials *creds = grpc_google_default_credentials_create(NULL);
  zval *creds_object = grpc_php_wrap_channel_credentials(creds, NULL, false
                                                         TSRMLS_CC);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create SSL credentials.
 * @param string|null $pem_root_certs = null PEM encoding of the server root certificates (optional)
 * @param string|null $private_key = null PEM encoding of the client's
 *                                              private key (optional)
 * @param string|null $cert_chain = null PEM encoding of the client's
 *                                             certificate chain (optional)
 * @return ChannelCredentials The new SSL credentials object
 */
PHP_METHOD(ChannelCredentials, createSsl) {
  char *pem_root_certs = NULL;
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;

  php_grpc_int root_certs_length = 0;
  php_grpc_int private_key_length = 0;
  php_grpc_int cert_chain_length = 0;

  pem_key_cert_pair.private_key = pem_key_cert_pair.cert_chain = NULL;

  grpc_set_ssl_roots_override_callback(get_ssl_roots_override);

  /* "|s!s!s!" == 3 optional nullable strings */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s!s!s!",
                            &pem_root_certs, &root_certs_length,
                            &pem_key_cert_pair.private_key,
                            &private_key_length,
                            &pem_key_cert_pair.cert_chain,
                            &cert_chain_length) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "createSsl expects 3 optional strings", 1 TSRMLS_CC);
    return;
  }

  php_grpc_int hashkey_len = root_certs_length + cert_chain_length;
  char *hashkey = emalloc(hashkey_len + 1);
  if (root_certs_length > 0) {
    strcpy(hashkey, pem_root_certs);
  }
  if (cert_chain_length > 0) {
    strcpy(hashkey, pem_key_cert_pair.cert_chain);
  }

  char *hashstr = malloc(41);
  generate_sha1_str(hashstr, hashkey, hashkey_len);

  grpc_channel_credentials *creds = grpc_ssl_credentials_create(
      pem_root_certs,
      pem_key_cert_pair.private_key == NULL ? NULL : &pem_key_cert_pair, NULL, NULL);
  zval *creds_object = grpc_php_wrap_channel_credentials(creds, hashstr, false
                                                         TSRMLS_CC);
  efree(hashkey);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create composite credentials from two existing credentials.
 * @param ChannelCredentials $cred1_obj The first credential
 * @param CallCredentials $cred2_obj The second credential
 * @return ChannelCredentials The new composite credentials object
 */
PHP_METHOD(ChannelCredentials, createComposite) {
  zval *cred1_obj;
  zval *cred2_obj;

  grpc_set_ssl_roots_override_callback(get_ssl_roots_override);

  /* "OO" == 2 Objects */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "OO", &cred1_obj,
                            grpc_ce_channel_credentials, &cred2_obj,
                            grpc_ce_call_credentials) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "createComposite expects 2 Credentials", 1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_channel_credentials *cred1 =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel_credentials, cred1_obj);
  wrapped_grpc_call_credentials *cred2 =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call_credentials, cred2_obj);
  grpc_channel_credentials *creds =
    grpc_composite_channel_credentials_create(cred1->wrapped, cred2->wrapped,
                                              NULL);
  // wrapped_grpc_channel_credentials object should keeps it's own
  // allocation. Otherwise it conflicts free hashstr with call.c.
  php_grpc_int cred1_len = strlen(cred1->hashstr);
  char *cred1_hashstr = malloc(cred1_len+1);
  strcpy(cred1_hashstr, cred1->hashstr);
  zval *creds_object =
    grpc_php_wrap_channel_credentials(creds, cred1_hashstr, true TSRMLS_CC);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create insecure channel credentials
 * @return null
 */
PHP_METHOD(ChannelCredentials, createInsecure) {
  RETURN_NULL();
}

/**
 * Create XDS channel credentials
 * @param ChannelCredentials $fallback_creds  The fallback credentials used
 * if the channel target does not have the 'xds:///' scheme or if the xDS
 * control plane does not provide information on how to fetch credentials
 * dynamically.
 * @return ChannelCredentials The xDS channel credentials object
 */
PHP_METHOD(ChannelCredentials, createXds) {
  grpc_channel_credentials* xds_creds = NULL;
  zval* fallback_creds = NULL;
  if (zend_parse_parameters_ex(0,  // ZEND_PARSE_PARAMS_QUIET,
                               ZEND_NUM_ARGS() TSRMLS_CC, "O", &fallback_creds,
                               grpc_ce_channel_credentials) != SUCCESS) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "createXds expects a fallback credentials",
                         1 TSRMLS_CC);
    return;
  }

  wrapped_grpc_channel_credentials* wrapped_fallback_creds =
      PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_channel_credentials,
                                  fallback_creds);
  xds_creds = grpc_xds_credentials_create(wrapped_fallback_creds->wrapped);
  const char* fallback_creds_hash_str =
      wrapped_fallback_creds->hashstr ? wrapped_fallback_creds->hashstr : "";

  // prefix "XDS:" as the hash of the xDS channel
  char* hash_str = malloc(strlen(fallback_creds_hash_str) + strlen("XDS:") + 1);
  strcpy(hash_str, "XDS:");
  strcat(hash_str, fallback_creds_hash_str);
  zval* xds_creds_obj = grpc_php_wrap_channel_credentials(
      xds_creds, hash_str, false /* has_call_creds */ TSRMLS_CC);
  RETURN_DESTROY_ZVAL(xds_creds_obj);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_setDefaultRootsPem, 0, 0, 1)
  ZEND_ARG_INFO(0, pem_roots)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_isDefaultRootsPemSet, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_invalidateDefaultRootsPem, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_createDefault, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_createSsl, 0, 0, 0)
  ZEND_ARG_INFO(0, pem_root_certs)
  ZEND_ARG_INFO(0, pem_private_key)
  ZEND_ARG_INFO(0, pem_cert_chain)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_createComposite, 0, 0, 2)
  ZEND_ARG_INFO(0, channel_creds)
  ZEND_ARG_INFO(0, call_creds)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_createInsecure, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_createXds, 0, 0, 1)
  ZEND_ARG_OBJ_INFO(0, fallback_creds, Grpc\\ChannelCredentials, 1)
ZEND_END_ARG_INFO()

static zend_function_entry channel_credentials_methods[] = {
  PHP_ME(ChannelCredentials, setDefaultRootsPem, arginfo_setDefaultRootsPem,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, isDefaultRootsPemSet, arginfo_isDefaultRootsPemSet,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, invalidateDefaultRootsPem, arginfo_invalidateDefaultRootsPem,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, createDefault, arginfo_createDefault,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, createSsl, arginfo_createSsl,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, createComposite, arginfo_createComposite,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, createInsecure, arginfo_createInsecure,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, createXds, arginfo_createXds,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_FE_END
};

void grpc_init_channel_credentials(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\ChannelCredentials",
                   channel_credentials_methods);
  ce.create_object = create_wrapped_grpc_channel_credentials;
  grpc_ce_channel_credentials = zend_register_internal_class(&ce TSRMLS_CC);
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_channel_credentials,
                        channel_credentials_ce_handlers);
}
