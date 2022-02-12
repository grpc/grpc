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

#include <ruby/ruby.h>

#include "rb_channel_credentials.h"

#include <string.h>

#include "rb_call_credentials.h"
#include "rb_grpc.h"
#include "rb_grpc_imports.generated.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

/* grpc_rb_cChannelCredentials is the ruby class that proxies
   grpc_channel_credentials. */
static VALUE grpc_rb_cChannelCredentials = Qnil;

static char* pem_root_certs = NULL;

/* grpc_rb_channel_credentials wraps a grpc_channel_credentials.  It provides a
 * mark object that is used to hold references to any objects used to create
 * the credentials. */
typedef struct grpc_rb_channel_credentials {
  /* Holder of ruby objects involved in constructing the credentials */
  VALUE mark;

  /* The actual credentials */
  grpc_channel_credentials* wrapped;
} grpc_rb_channel_credentials;

static void grpc_rb_channel_credentials_free_internal(void* p) {
  grpc_rb_channel_credentials* wrapper = NULL;
  if (p == NULL) {
    return;
  };
  wrapper = (grpc_rb_channel_credentials*)p;
  grpc_channel_credentials_release(wrapper->wrapped);
  wrapper->wrapped = NULL;

  xfree(p);
}

/* Destroys the credentials instances. */
static void grpc_rb_channel_credentials_free(void* p) {
  grpc_rb_channel_credentials_free_internal(p);
  grpc_ruby_shutdown();
}

/* Protects the mark object from GC */
static void grpc_rb_channel_credentials_mark(void* p) {
  grpc_rb_channel_credentials* wrapper = NULL;
  if (p == NULL) {
    return;
  }
  wrapper = (grpc_rb_channel_credentials*)p;

  if (wrapper->mark != Qnil) {
    rb_gc_mark(wrapper->mark);
  }
}

static rb_data_type_t grpc_rb_channel_credentials_data_type = {
    "grpc_channel_credentials",
    {grpc_rb_channel_credentials_mark,
     grpc_rb_channel_credentials_free,
     GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocates ChannelCredential instances.
   Provides safe initial defaults for the instance fields. */
static VALUE grpc_rb_channel_credentials_alloc(VALUE cls) {
  grpc_ruby_init();
  grpc_rb_channel_credentials* wrapper = ALLOC(grpc_rb_channel_credentials);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return TypedData_Wrap_Struct(cls, &grpc_rb_channel_credentials_data_type,
                               wrapper);
}

/* Creates a wrapping object for a given channel credentials. This should only
 * be called with grpc_channel_credentials objects that are not already
 * associated with any Ruby object. */
VALUE grpc_rb_wrap_channel_credentials(grpc_channel_credentials* c,
                                       VALUE mark) {
  VALUE rb_wrapper;
  grpc_rb_channel_credentials* wrapper;
  if (c == NULL) {
    return Qnil;
  }
  rb_wrapper = grpc_rb_channel_credentials_alloc(grpc_rb_cChannelCredentials);
  TypedData_Get_Struct(rb_wrapper, grpc_rb_channel_credentials,
                       &grpc_rb_channel_credentials_data_type, wrapper);
  wrapper->wrapped = c;
  wrapper->mark = mark;
  return rb_wrapper;
}

/* The attribute used on the mark object to hold the pem_root_certs. */
static ID id_pem_root_certs;

/* The attribute used on the mark object to hold the pem_private_key. */
static ID id_pem_private_key;

/* The attribute used on the mark object to hold the pem_private_key. */
static ID id_pem_cert_chain;

/*
  call-seq:
    creds1 = Credentials.new()
    ...
    creds2 = Credentials.new(pem_root_certs)
    ...
    creds3 = Credentials.new(pem_root_certs, pem_private_key,
                             pem_cert_chain)
    pem_root_certs: (optional) PEM encoding of the server root certificate
    pem_private_key: (optional) PEM encoding of the client's private key
    pem_cert_chain: (optional) PEM encoding of the client's cert chain
    Initializes Credential instances. */
static VALUE grpc_rb_channel_credentials_init(int argc, VALUE* argv,
                                              VALUE self) {
  VALUE pem_root_certs = Qnil;
  VALUE pem_private_key = Qnil;
  VALUE pem_cert_chain = Qnil;
  grpc_rb_channel_credentials* wrapper = NULL;
  grpc_channel_credentials* creds = NULL;
  grpc_ssl_pem_key_cert_pair key_cert_pair;
  const char* pem_root_certs_cstr = NULL;
  MEMZERO(&key_cert_pair, grpc_ssl_pem_key_cert_pair, 1);

  /* "03" == no mandatory arg, 3 optional */
  rb_scan_args(argc, argv, "03", &pem_root_certs, &pem_private_key,
               &pem_cert_chain);

  TypedData_Get_Struct(self, grpc_rb_channel_credentials,
                       &grpc_rb_channel_credentials_data_type, wrapper);
  if (pem_root_certs != Qnil) {
    pem_root_certs_cstr = RSTRING_PTR(pem_root_certs);
  }
  if (pem_private_key == Qnil && pem_cert_chain == Qnil) {
    creds = grpc_ssl_credentials_create(pem_root_certs_cstr, NULL, NULL, NULL);
  } else {
    if (pem_private_key == Qnil) {
      rb_raise(
          rb_eRuntimeError,
          "could not create a credentials because pem_private_key is NULL");
    }
    if (pem_cert_chain == Qnil) {
      rb_raise(rb_eRuntimeError,
               "could not create a credentials because pem_cert_chain is NULL");
    }
    key_cert_pair.private_key = RSTRING_PTR(pem_private_key);
    key_cert_pair.cert_chain = RSTRING_PTR(pem_cert_chain);
    creds = grpc_ssl_credentials_create(pem_root_certs_cstr, &key_cert_pair,
                                        NULL, NULL);
  }
  if (creds == NULL) {
    rb_raise(rb_eRuntimeError,
             "the call to grpc_ssl_credentials_create() failed, could not "
             "create a credentials, see "
             "https://github.com/grpc/grpc/blob/master/TROUBLESHOOTING.md for "
             "debugging tips");
    return Qnil;
  }
  wrapper->wrapped = creds;

  /* Add the input objects as hidden fields to preserve them. */
  rb_ivar_set(self, id_pem_cert_chain, pem_cert_chain);
  rb_ivar_set(self, id_pem_private_key, pem_private_key);
  rb_ivar_set(self, id_pem_root_certs, pem_root_certs);

  return self;
}

static VALUE grpc_rb_channel_credentials_compose(int argc, VALUE* argv,
                                                 VALUE self) {
  grpc_channel_credentials* creds;
  grpc_call_credentials* other;
  grpc_channel_credentials* prev = NULL;
  VALUE mark;
  if (argc == 0) {
    return self;
  }
  mark = rb_ary_new();
  rb_ary_push(mark, self);
  creds = grpc_rb_get_wrapped_channel_credentials(self);
  for (int i = 0; i < argc; i++) {
    rb_ary_push(mark, argv[i]);
    other = grpc_rb_get_wrapped_call_credentials(argv[i]);
    creds = grpc_composite_channel_credentials_create(creds, other, NULL);
    if (prev != NULL) {
      grpc_channel_credentials_release(prev);
    }
    prev = creds;

    if (creds == NULL) {
      rb_raise(rb_eRuntimeError,
               "Failed to compose channel and call credentials");
    }
  }
  return grpc_rb_wrap_channel_credentials(creds, mark);
}

static grpc_ssl_roots_override_result get_ssl_roots_override(
    char** pem_root_certs_ptr) {
  *pem_root_certs_ptr = pem_root_certs;
  if (pem_root_certs == NULL) {
    return GRPC_SSL_ROOTS_OVERRIDE_FAIL;
  } else {
    return GRPC_SSL_ROOTS_OVERRIDE_OK;
  }
}

static VALUE grpc_rb_set_default_roots_pem(VALUE self, VALUE roots) {
  char* roots_ptr = StringValueCStr(roots);
  size_t length = strlen(roots_ptr);
  (void)self;
  pem_root_certs = gpr_malloc((length + 1) * sizeof(char));
  memcpy(pem_root_certs, roots_ptr, length + 1);
  return Qnil;
}

void Init_grpc_channel_credentials() {
  grpc_rb_cChannelCredentials = rb_define_class_under(
      grpc_rb_mGrpcCore, "ChannelCredentials", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(grpc_rb_cChannelCredentials,
                       grpc_rb_channel_credentials_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(grpc_rb_cChannelCredentials, "initialize",
                   grpc_rb_channel_credentials_init, -1);
  rb_define_method(grpc_rb_cChannelCredentials, "initialize_copy",
                   grpc_rb_cannot_init_copy, 1);
  rb_define_method(grpc_rb_cChannelCredentials, "compose",
                   grpc_rb_channel_credentials_compose, -1);
  rb_define_module_function(grpc_rb_cChannelCredentials,
                            "set_default_roots_pem",
                            grpc_rb_set_default_roots_pem, 1);

  grpc_set_ssl_roots_override_callback(get_ssl_roots_override);

  id_pem_cert_chain = rb_intern("__pem_cert_chain");
  id_pem_private_key = rb_intern("__pem_private_key");
  id_pem_root_certs = rb_intern("__pem_root_certs");
}

/* Gets the wrapped grpc_channel_credentials from the ruby wrapper */
grpc_channel_credentials* grpc_rb_get_wrapped_channel_credentials(VALUE v) {
  grpc_rb_channel_credentials* wrapper = NULL;
  Check_TypedStruct(v, &grpc_rb_channel_credentials_data_type);
  TypedData_Get_Struct(v, grpc_rb_channel_credentials,
                       &grpc_rb_channel_credentials_data_type, wrapper);
  return wrapper->wrapped;
}

/* Check if v is kind of ChannelCredentials */
bool grpc_rb_is_channel_credentials(VALUE v) {
  return rb_typeddata_is_kind_of(v, &grpc_rb_channel_credentials_data_type);
}
