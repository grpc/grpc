#include "credentials.h"

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

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

/* Frees and destroys an instance of wrapped_grpc_credentials */
void free_wrapped_grpc_credentials(void *object TSRMLS_DC) {
  wrapped_grpc_credentials *creds = (wrapped_grpc_credentials *)object;
  if (creds->wrapped != NULL) {
    grpc_credentials_release(creds->wrapped);
  }
  efree(creds);
}

/* Initializes an instance of wrapped_grpc_credentials to be associated with an
 * object of a class specified by class_type */
zend_object_value create_wrapped_grpc_credentials(zend_class_entry *class_type
                                                      TSRMLS_DC) {
  zend_object_value retval;
  wrapped_grpc_credentials *intern;

  intern =
      (wrapped_grpc_credentials *)emalloc(sizeof(wrapped_grpc_credentials));
  memset(intern, 0, sizeof(wrapped_grpc_credentials));

  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  retval.handle = zend_objects_store_put(
      intern, (zend_objects_store_dtor_t)zend_objects_destroy_object,
      free_wrapped_grpc_credentials, NULL TSRMLS_CC);
  retval.handlers = zend_get_std_object_handlers();
  return retval;
}

zval *grpc_php_wrap_credentials(grpc_credentials *wrapped) {
  zval *credentials_object;
  MAKE_STD_ZVAL(credentials_object);
  object_init_ex(credentials_object, grpc_ce_credentials);
  wrapped_grpc_credentials *credentials =
      (wrapped_grpc_credentials *)zend_object_store_get_object(
          credentials_object TSRMLS_CC);
  credentials->wrapped = wrapped;
  return credentials_object;
}

/**
 * Create a default credentials object.
 * @return Credentials The new default credentials object
 */
PHP_METHOD(Credentials, createDefault) {
  grpc_credentials *creds = grpc_default_credentials_create();
  zval *creds_object = grpc_php_wrap_credentials(creds);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create SSL credentials.
 * @param string pem_root_certs PEM encoding of the server root certificates
 * @param string pem_private_key PEM encoding of the client's private key
 *     (optional)
 * @param string pem_cert_chain PEM encoding of the client's certificate chain
 *     (optional)
 * @return Credentials The new SSL credentials object
 */
PHP_METHOD(Credentials, createSsl) {
  char *pem_root_certs;
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;

  int root_certs_length, private_key_length = 0, cert_chain_length = 0;

  pem_key_cert_pair.private_key = pem_key_cert_pair.cert_chain = NULL;

  /* "s|s!s! == 1 string, 2 optional nullable strings */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|s!s!",
                            &pem_root_certs, &root_certs_length,
                            &pem_key_cert_pair.private_key, &private_key_length,
                            &pem_key_cert_pair.cert_chain,
                            &cert_chain_length) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "createSsl expects 1 to 3 strings", 1 TSRMLS_CC);
    return;
  }
  grpc_credentials *creds = grpc_ssl_credentials_create(
      pem_root_certs,
      pem_key_cert_pair.private_key == NULL ? NULL : &pem_key_cert_pair);
  zval *creds_object = grpc_php_wrap_credentials(creds);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create composite credentials from two existing credentials.
 * @param Credentials cred1 The first credential
 * @param Credentials cred2 The second credential
 * @return Credentials The new composite credentials object
 */
PHP_METHOD(Credentials, createComposite) {
  zval *cred1_obj;
  zval *cred2_obj;

  /* "OO" == 3 Objects */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "OO", &cred1_obj,
                            grpc_ce_credentials, &cred2_obj,
                            grpc_ce_credentials) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "createComposite expects 2 Credentials", 1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_credentials *cred1 =
      (wrapped_grpc_credentials *)zend_object_store_get_object(
          cred1_obj TSRMLS_CC);
  wrapped_grpc_credentials *cred2 =
      (wrapped_grpc_credentials *)zend_object_store_get_object(
          cred2_obj TSRMLS_CC);
  grpc_credentials *creds =
      grpc_composite_credentials_create(cred1->wrapped, cred2->wrapped);
  zval *creds_object = grpc_php_wrap_credentials(creds);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create Google Compute Engine credentials
 * @return Credentials The new GCE credentials object
 */
PHP_METHOD(Credentials, createGce) {
  grpc_credentials *creds = grpc_compute_engine_credentials_create();
  zval *creds_object = grpc_php_wrap_credentials(creds);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create fake credentials. Only to be used for testing.
 * @return Credentials The new fake credentials object
 */
PHP_METHOD(Credentials, createFake) {
  grpc_credentials *creds = grpc_fake_transport_security_credentials_create();
  zval *creds_object = grpc_php_wrap_credentials(creds);
  RETURN_DESTROY_ZVAL(creds_object);
}

static zend_function_entry credentials_methods[] = {
    PHP_ME(Credentials, createDefault, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Credentials, createSsl, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Credentials, createComposite, NULL,
           ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Credentials, createGce, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Credentials, createFake, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_FE_END};

void grpc_init_credentials(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Credentials", credentials_methods);
  ce.create_object = create_wrapped_grpc_credentials;
  grpc_ce_credentials = zend_register_internal_class(&ce TSRMLS_CC);
}
