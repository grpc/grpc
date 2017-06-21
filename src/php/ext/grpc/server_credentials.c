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

#include "server_credentials.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include <ext/spl/spl_exceptions.h>
#include "php_grpc.h"

#include <zend_exceptions.h>
#include <zend_hash.h>

#include <grpc/support/alloc.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

zend_class_entry *grpc_ce_server_credentials;
#if PHP_MAJOR_VERSION >= 7
static zend_object_handlers server_credentials_ce_handlers;
#endif

/* Frees and destroys an instace of wrapped_grpc_server_credentials */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_server_credentials)
  if (p->wrapped != NULL) {
    grpc_server_credentials_release(p->wrapped);
  }
PHP_GRPC_FREE_WRAPPED_FUNC_END()

/* Initializes an instace of wrapped_grpc_server_credentials to be associated
 * with an object of a class specified by class_type */
php_grpc_zend_object create_wrapped_grpc_server_credentials(
    zend_class_entry *class_type TSRMLS_DC) {
  PHP_GRPC_ALLOC_CLASS_OBJECT(wrapped_grpc_server_credentials);
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  PHP_GRPC_FREE_CLASS_OBJECT(wrapped_grpc_server_credentials,
                             server_credentials_ce_handlers);
}

zval *grpc_php_wrap_server_credentials(grpc_server_credentials
                                       *wrapped TSRMLS_DC) {
  zval *server_credentials_object;
  PHP_GRPC_MAKE_STD_ZVAL(server_credentials_object);
  object_init_ex(server_credentials_object, grpc_ce_server_credentials);
  wrapped_grpc_server_credentials *server_credentials =
    Z_WRAPPED_GRPC_SERVER_CREDS_P(server_credentials_object);
  server_credentials->wrapped = wrapped;
  return server_credentials_object;
}

/**
 * Create SSL credentials.
 * @param string $pem_root_certs PEM encoding of the server root certificates
 * @param array $array The array of PEM encoding of the client's certificates
 * @param bool $force_client_auth If request the client certificate (optional)
 * @return Credentials The new SSL credentials object
 */
PHP_METHOD(ServerCredentials, createSsl) {
  char *pem_root_certs = 0;
  php_grpc_int root_certs_length = 0;
  zend_bool force_client_auth = 0;
  grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs;
  int key_cert_pair_count;
  zval *array;
  HashTable *array_hash;
  HashTable *inner_hash;
  php_grpc_ulong index;

  zval *value;
  zval *private_key_value;
  zval *cert_chain_value;

  /* "s!a|b" == 1 nullable string, 1 array, 1 optional bool */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!a|b",
                            &pem_root_certs, &root_certs_length, &array,
                            &force_client_auth) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "createSsl expects 1 string, 1 array and "
                         "1 optional bool", 1 TSRMLS_CC);
    return;
  }

  array_hash = Z_ARRVAL_P(array);
  key_cert_pair_count = zend_hash_num_elements(array_hash);
  if (key_cert_pair_count == 0) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "expects 1 array with 1 elem at least",
                         1 TSRMLS_CC);
  }

  /* Default to not requesting the client certificate */
  grpc_ssl_client_certificate_request_type client_certificate_request =
    GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
  if (force_client_auth) {
    client_certificate_request =
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
  }
  pem_key_cert_pairs = gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair) *
                                  key_cert_pair_count);

  char *key = NULL;
  int key_type;
  PHP_GRPC_HASH_FOREACH_LONG_KEY_VAL_START(array_hash, key, key_type, index,
                                           value)
    if (key_type != HASH_KEY_IS_LONG || key != NULL) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "keys must be integers", 1 TSRMLS_CC);
      gpr_free(pem_key_cert_pairs);
      return;
    }
    if (Z_TYPE_P(value) != IS_ARRAY) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                          "expected an array", 1 TSRMLS_CC);
      gpr_free(pem_key_cert_pairs);
      return;
    }

    inner_hash = Z_ARRVAL_P(value);
    if (php_grpc_zend_hash_find(inner_hash, "private_key", sizeof("private_key"),
                       (void**)&private_key_value) != SUCCESS ||
        Z_TYPE_P(private_key_value) != IS_STRING) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "expected a string", 1 TSRMLS_CC);
      gpr_free(pem_key_cert_pairs);
      return;
    }
    if (php_grpc_zend_hash_find(inner_hash, "cert_chain", sizeof("cert_chain"),
                       (void**)&cert_chain_value) != SUCCESS ||
        Z_TYPE_P(cert_chain_value) != IS_STRING) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "expected a string", 1 TSRMLS_CC);
      gpr_free(pem_key_cert_pairs);
      return;
    }

    pem_key_cert_pairs[index].private_key = Z_STRVAL_P(private_key_value);
    pem_key_cert_pairs[index].cert_chain = Z_STRVAL_P(cert_chain_value);
  PHP_GRPC_HASH_FOREACH_END()

  grpc_server_credentials *creds = grpc_ssl_server_credentials_create_ex(
      pem_root_certs, pem_key_cert_pairs, key_cert_pair_count,
      client_certificate_request, NULL);
  gpr_free(pem_key_cert_pairs);

  zval *creds_object = grpc_php_wrap_server_credentials(creds TSRMLS_CC);
  RETURN_DESTROY_ZVAL(creds_object);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_createSsl, 0, 0, 3)
  ZEND_ARG_INFO(0, pem_root_certs)
  ZEND_ARG_INFO(0, pem_private_key)
  ZEND_ARG_INFO(0, pem_cert_chain)
ZEND_END_ARG_INFO()

static zend_function_entry server_credentials_methods[] = {
  PHP_ME(ServerCredentials, createSsl, arginfo_createSsl,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_FE_END
 };

void grpc_init_server_credentials(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\ServerCredentials", server_credentials_methods);
  ce.create_object = create_wrapped_grpc_server_credentials;
  grpc_ce_server_credentials = zend_register_internal_class(&ce TSRMLS_CC);
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_server_credentials,
                        server_credentials_ce_handlers);
}
