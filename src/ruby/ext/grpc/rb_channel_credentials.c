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

#include "rb_channel_credentials.h"

#include <ruby/ruby.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "rb_call_credentials.h"
#include "rb_grpc.h"

/* grpc_rb_cChannelCredentials is the ruby class that proxies
   grpc_channel_credentials. */
static VALUE grpc_rb_cChannelCredentials = Qnil;

/* grpc_rb_channel_credentials wraps a grpc_channel_credentials.  It provides a
 * peer ruby object, 'mark' to minimize copying when a credential is
 * created from ruby. */
typedef struct grpc_rb_channel_credentials {
  /* Holder of ruby objects involved in constructing the credentials */
  VALUE mark;

  /* The actual credentials */
  grpc_channel_credentials *wrapped;
} grpc_rb_channel_credentials;

/* Destroys the credentials instances. */
static void grpc_rb_channel_credentials_free(void *p) {
  grpc_rb_channel_credentials *wrapper = NULL;
  if (p == NULL) {
    return;
  };
  wrapper = (grpc_rb_channel_credentials *)p;

  /* Delete the wrapped object if the mark object is Qnil, which indicates that
   * no other object is the actual owner. */
  if (wrapper->wrapped != NULL && wrapper->mark == Qnil) {
    grpc_channel_credentials_release(wrapper->wrapped);
    wrapper->wrapped = NULL;
  }

  xfree(p);
}

/* Protects the mark object from GC */
static void grpc_rb_channel_credentials_mark(void *p) {
  grpc_rb_channel_credentials *wrapper = NULL;
  if (p == NULL) {
    return;
  }
  wrapper = (grpc_rb_channel_credentials *)p;

  /* If it's not already cleaned up, mark the mark object */
  if (wrapper->mark != Qnil) {
    rb_gc_mark(wrapper->mark);
  }
}

static rb_data_type_t grpc_rb_channel_credentials_data_type = {
    "grpc_channel_credentials",
    {grpc_rb_channel_credentials_mark, grpc_rb_channel_credentials_free,
     GRPC_RB_MEMSIZE_UNAVAILABLE, {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocates ChannelCredential instances.
   Provides safe initial defaults for the instance fields. */
static VALUE grpc_rb_channel_credentials_alloc(VALUE cls) {
  grpc_rb_channel_credentials *wrapper = ALLOC(grpc_rb_channel_credentials);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return TypedData_Wrap_Struct(cls, &grpc_rb_channel_credentials_data_type, wrapper);
}

/* Creates a wrapping object for a given channel credentials. This should only
 * be called with grpc_channel_credentials objects that are not already
 * associated with any Ruby object. */
VALUE grpc_rb_wrap_channel_credentials(grpc_channel_credentials *c) {
  VALUE rb_wrapper;
  grpc_rb_channel_credentials *wrapper;
  if (c == NULL) {
    return Qnil;
  }
  rb_wrapper = grpc_rb_channel_credentials_alloc(grpc_rb_cChannelCredentials);
  TypedData_Get_Struct(rb_wrapper, grpc_rb_channel_credentials,
                       &grpc_rb_channel_credentials_data_type, wrapper);
  wrapper->wrapped = c;
  return rb_wrapper;
}

/* Clones ChannelCredentials instances.
   Gives ChannelCredentials a consistent implementation of Ruby's object copy/dup
   protocol. */
static VALUE grpc_rb_channel_credentials_init_copy(VALUE copy, VALUE orig) {
  grpc_rb_channel_credentials *orig_cred = NULL;
  grpc_rb_channel_credentials *copy_cred = NULL;

  if (copy == orig) {
    return copy;
  }

  /* Raise an error if orig is not a credentials object or a subclass. */
  if (TYPE(orig) != T_DATA ||
      RDATA(orig)->dfree != (RUBY_DATA_FUNC)grpc_rb_channel_credentials_free) {
    rb_raise(rb_eTypeError, "not a %s",
             rb_obj_classname(grpc_rb_cChannelCredentials));
  }

  TypedData_Get_Struct(orig, grpc_rb_channel_credentials,
                       &grpc_rb_channel_credentials_data_type, orig_cred);
  TypedData_Get_Struct(copy, grpc_rb_channel_credentials,
                       &grpc_rb_channel_credentials_data_type, copy_cred);

  /* use ruby's MEMCPY to make a byte-for-byte copy of the credentials
   * wrapper object. */
  MEMCPY(copy_cred, orig_cred, grpc_rb_channel_credentials, 1);
  return copy;
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
static VALUE grpc_rb_channel_credentials_init(int argc, VALUE *argv, VALUE self) {
  VALUE pem_root_certs = Qnil;
  VALUE pem_private_key = Qnil;
  VALUE pem_cert_chain = Qnil;
  grpc_rb_channel_credentials *wrapper = NULL;
  grpc_channel_credentials *creds = NULL;
  grpc_ssl_pem_key_cert_pair key_cert_pair;
  const char *pem_root_certs_cstr = NULL;
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
    creds = grpc_ssl_credentials_create(pem_root_certs_cstr, NULL, NULL);
  } else {
    key_cert_pair.private_key = RSTRING_PTR(pem_private_key);
    key_cert_pair.cert_chain = RSTRING_PTR(pem_cert_chain);
    creds = grpc_ssl_credentials_create(pem_root_certs_cstr,
                                        &key_cert_pair, NULL);
  }
  if (creds == NULL) {
    rb_raise(rb_eRuntimeError, "could not create a credentials, not sure why");
    return Qnil;
  }
  wrapper->wrapped = creds;

  /* Add the input objects as hidden fields to preserve them. */
  rb_ivar_set(self, id_pem_cert_chain, pem_cert_chain);
  rb_ivar_set(self, id_pem_private_key, pem_private_key);
  rb_ivar_set(self, id_pem_root_certs, pem_root_certs);

  return self;
}

static VALUE grpc_rb_channel_credentials_compose(int argc, VALUE *argv,
                                                 VALUE self) {
  grpc_channel_credentials *creds;
  grpc_call_credentials *other;
  if (argc == 0) {
    return self;
  }
  creds = grpc_rb_get_wrapped_channel_credentials(self);
  for (int i = 0; i < argc; i++) {
    other = grpc_rb_get_wrapped_call_credentials(argv[i]);
    creds = grpc_composite_channel_credentials_create(creds, other, NULL);
    if (creds == NULL) {
      rb_raise(rb_eRuntimeError,
               "Failed to compose channel and call credentials");
    }
  }
  return grpc_rb_wrap_channel_credentials(creds);
}

void Init_grpc_channel_credentials() {
  grpc_rb_cChannelCredentials =
      rb_define_class_under(grpc_rb_mGrpcCore, "ChannelCredentials", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(grpc_rb_cChannelCredentials,
                       grpc_rb_channel_credentials_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(grpc_rb_cChannelCredentials, "initialize",
                   grpc_rb_channel_credentials_init, -1);
  rb_define_method(grpc_rb_cChannelCredentials, "initialize_copy",
                   grpc_rb_channel_credentials_init_copy, 1);
  rb_define_method(grpc_rb_cChannelCredentials, "compose",
                   grpc_rb_channel_credentials_compose, -1);

  id_pem_cert_chain = rb_intern("__pem_cert_chain");
  id_pem_private_key = rb_intern("__pem_private_key");
  id_pem_root_certs = rb_intern("__pem_root_certs");
}

/* Gets the wrapped grpc_channel_credentials from the ruby wrapper */
grpc_channel_credentials *grpc_rb_get_wrapped_channel_credentials(VALUE v) {
  grpc_rb_channel_credentials *wrapper = NULL;
  TypedData_Get_Struct(v, grpc_rb_channel_credentials,
                       &grpc_rb_channel_credentials_data_type,
                       wrapper);
  return wrapper->wrapped;
}
