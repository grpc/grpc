/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "channel_credentials.h"
#include "call_credentials.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include <ext/standard/sha1.h>
#include <ext/spl/spl_exceptions.h>
#include "channel.h"
#include "php_grpc.h"

#include <zend_exceptions.h>
#include <zend_hash.h>

#include <grpc/support/alloc.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

zend_class_entry *grpc_ce_channel_credentials;
#if PHP_MAJOR_VERSION >= 7
static zend_object_handlers channel_credentials_ce_handlers;
#endif
static gpr_mu cc_persistent_list_mu;
int le_cc_plink;
const char *persistent_list_key = "default_root_certs";

static grpc_ssl_roots_override_result get_ssl_roots_override(
    char **pem_root_certs) {
  TSRMLS_FETCH();
  php_grpc_zend_resource *rsrc;
  php_grpc_int key_len = strlen(persistent_list_key);

  gpr_mu_lock(&cc_persistent_list_mu);
  if (PHP_GRPC_PERSISTENT_LIST_FIND(&EG(persistent_list), persistent_list_key,
                                    key_len, rsrc)) {
    channel_credentials_persistent_le_t *le =
      (channel_credentials_persistent_le_t *)rsrc->ptr;
    *pem_root_certs = le->default_root_certs;
    gpr_mu_unlock(&cc_persistent_list_mu);
    return GRPC_SSL_ROOTS_OVERRIDE_OK;
  } else {
    gpr_mu_unlock(&cc_persistent_list_mu);
    return GRPC_SSL_ROOTS_OVERRIDE_FAIL;
  }
}

/* Frees and destroys an instance of wrapped_grpc_channel_credentials */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_channel_credentials)
  if (p->wrapped != NULL) {
    grpc_channel_credentials_release(p->wrapped);
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
    Z_WRAPPED_GRPC_CHANNEL_CREDS_P(credentials_object);
  credentials->wrapped = wrapped;
  credentials->hashstr = hashstr;
  credentials->has_call_creds = has_call_creds;
  return credentials_object;
}

void update_root_certs_persistent_list(char *pem_roots,
                                       php_grpc_int pem_roots_length
                                       TSRMLS_DC) {

  php_grpc_zend_resource new_rsrc;
  channel_credentials_persistent_le_t *le;
  php_grpc_int key_len = strlen(persistent_list_key);
  new_rsrc.type = le_cc_plink;
  le = malloc(sizeof(channel_credentials_persistent_le_t));

  char *tmp = malloc(pem_roots_length+1);
  memcpy(tmp, pem_roots, pem_roots_length+1);
  le->default_root_certs = tmp;

  new_rsrc.ptr = le;
  gpr_mu_lock(&cc_persistent_list_mu);
  PHP_GRPC_PERSISTENT_LIST_UPDATE(&EG(persistent_list), persistent_list_key,
                                  key_len, (void *)&new_rsrc);
  gpr_mu_unlock(&cc_persistent_list_mu);
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
  php_grpc_zend_resource *rsrc;
  php_grpc_int key_len = strlen(persistent_list_key);
  if (!(PHP_GRPC_PERSISTENT_LIST_FIND(&EG(persistent_list),
                                      persistent_list_key,
                                      key_len, rsrc))) {
    update_root_certs_persistent_list(pem_roots,
                                      pem_roots_length TSRMLS_CC);
  } else {
    channel_credentials_persistent_le_t *le =
      (channel_credentials_persistent_le_t *)rsrc->ptr;
    if (strcmp(pem_roots, le->default_root_certs) != 0) {
      update_root_certs_persistent_list(pem_roots, pem_roots_length
                                        TSRMLS_CC);
    }
  }
}

/**
 * Create a default channel credentials object.
 * @return ChannelCredentials The new default channel credentials object
 */
PHP_METHOD(ChannelCredentials, createDefault) {
  grpc_channel_credentials *creds = grpc_google_default_credentials_create();
  zval *creds_object = grpc_php_wrap_channel_credentials(creds, NULL, false
                                                         TSRMLS_CC);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create SSL credentials.
 * @param string $pem_root_certs PEM encoding of the server root certificates
 * @param string $pem_key_cert_pair.private_key PEM encoding of the client's
 *                                              private key (optional)
 * @param string $pem_key_cert_pair.cert_chain PEM encoding of the client's
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
  char *hashkey = emalloc(hashkey_len);
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
      pem_key_cert_pair.private_key == NULL ? NULL : &pem_key_cert_pair, NULL);
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
    Z_WRAPPED_GRPC_CHANNEL_CREDS_P(cred1_obj);
  wrapped_grpc_call_credentials *cred2 =
    Z_WRAPPED_GRPC_CALL_CREDS_P(cred2_obj);
  grpc_channel_credentials *creds =
      grpc_composite_channel_credentials_create(cred1->wrapped, cred2->wrapped,
                                                NULL);
  zval *creds_object =
      grpc_php_wrap_channel_credentials(creds, cred1->hashstr, true
                                        TSRMLS_CC);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create insecure channel credentials
 * @return null
 */
PHP_METHOD(ChannelCredentials, createInsecure) {
  RETURN_NULL();
}

static void php_grpc_channel_credentials_plink_dtor(
    php_grpc_zend_resource *rsrc TSRMLS_DC) {
  channel_credentials_persistent_le_t *le =
    (channel_credentials_persistent_le_t *)rsrc->ptr;
  free(le->default_root_certs);
  free(le);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_setDefaultRootsPem, 0, 0, 1)
  ZEND_ARG_INFO(0, pem_roots)
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

static zend_function_entry channel_credentials_methods[] = {
  PHP_ME(ChannelCredentials, setDefaultRootsPem, arginfo_setDefaultRootsPem,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, createDefault, arginfo_createDefault,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, createSsl, arginfo_createSsl,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, createComposite, arginfo_createComposite,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(ChannelCredentials, createInsecure, arginfo_createInsecure,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_FE_END
};

GRPC_STARTUP_FUNCTION(channel_credentials) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\ChannelCredentials",
                   channel_credentials_methods);
  ce.create_object = create_wrapped_grpc_channel_credentials;
  grpc_ce_channel_credentials = zend_register_internal_class(&ce TSRMLS_CC);
  gpr_mu_init(&cc_persistent_list_mu);
  le_cc_plink = zend_register_list_destructors_ex(
      NULL, php_grpc_channel_credentials_plink_dtor,
      "Channel Credentials persistent default certs", module_number);
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_channel_credentials,
                        channel_credentials_ce_handlers);
  return SUCCESS;
}
