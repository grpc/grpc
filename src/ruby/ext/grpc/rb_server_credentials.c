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

#include "rb_server_credentials.h"

#include <ruby/ruby.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include "rb_grpc.h"

/* grpc_rb_cServerCredentials is the ruby class that proxies
   grpc_server_credentials. */
static VALUE grpc_rb_cServerCredentials = Qnil;

/* grpc_rb_server_credentials wraps a grpc_server_credentials.  It provides a
   peer ruby object, 'mark' to minimize copying when a server credential is
   created from ruby. */
typedef struct grpc_rb_server_credentials {
  /* Holder of ruby objects involved in constructing the server credentials */
  VALUE mark;
  /* The actual server credentials */
  grpc_server_credentials *wrapped;
} grpc_rb_server_credentials;

/* Destroys the server credentials instances. */
static void grpc_rb_server_credentials_free(void *p) {
  grpc_rb_server_credentials *wrapper = NULL;
  if (p == NULL) {
    return;
  };
  wrapper = (grpc_rb_server_credentials *)p;

  /* Delete the wrapped object if the mark object is Qnil, which indicates that
     no other object is the actual owner. */
  if (wrapper->wrapped != NULL && wrapper->mark == Qnil) {
    grpc_server_credentials_release(wrapper->wrapped);
    wrapper->wrapped = NULL;
  }

  xfree(p);
}

/* Protects the mark object from GC */
static void grpc_rb_server_credentials_mark(void *p) {
  grpc_rb_server_credentials *wrapper = NULL;
  if (p == NULL) {
    return;
  }
  wrapper = (grpc_rb_server_credentials *)p;

  /* If it's not already cleaned up, mark the mark object */
  if (wrapper->mark != Qnil) {
    rb_gc_mark(wrapper->mark);
  }
}

static const rb_data_type_t grpc_rb_server_credentials_data_type = {
    "grpc_server_credentials",
    {grpc_rb_server_credentials_mark, grpc_rb_server_credentials_free,
     GRPC_RB_MEMSIZE_UNAVAILABLE, {NULL, NULL}},
    NULL, NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocates ServerCredential instances.

   Provides safe initial defaults for the instance fields. */
static VALUE grpc_rb_server_credentials_alloc(VALUE cls) {
  grpc_rb_server_credentials *wrapper = ALLOC(grpc_rb_server_credentials);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return TypedData_Wrap_Struct(cls, &grpc_rb_server_credentials_data_type,
                               wrapper);
}

/* Clones ServerCredentials instances.

   Gives ServerCredentials a consistent implementation of Ruby's object copy/dup
   protocol. */
static VALUE grpc_rb_server_credentials_init_copy(VALUE copy, VALUE orig) {
  grpc_rb_server_credentials *orig_ch = NULL;
  grpc_rb_server_credentials *copy_ch = NULL;

  if (copy == orig) {
    return copy;
  }

  /* Raise an error if orig is not a server_credentials object or a subclass. */
  if (TYPE(orig) != T_DATA ||
      RDATA(orig)->dfree != (RUBY_DATA_FUNC)grpc_rb_server_credentials_free) {
    rb_raise(rb_eTypeError, "not a %s",
             rb_obj_classname(grpc_rb_cServerCredentials));
  }

  TypedData_Get_Struct(orig, grpc_rb_server_credentials,
                       &grpc_rb_server_credentials_data_type, orig_ch);
  TypedData_Get_Struct(copy, grpc_rb_server_credentials,
                       &grpc_rb_server_credentials_data_type, copy_ch);

  /* use ruby's MEMCPY to make a byte-for-byte copy of the server_credentials
     wrapper object. */
  MEMCPY(copy_ch, orig_ch, grpc_rb_server_credentials, 1);
  return copy;
}

/* The attribute used on the mark object to preserve the pem_root_certs. */
static ID id_pem_root_certs;

/* The attribute used on the mark object to preserve the pem_key_certs */
static ID id_pem_key_certs;

/* The key used to access the pem cert in a key_cert pair hash */
static VALUE sym_cert_chain;

/* The key used to access the pem private key in a key_cert pair hash */
static VALUE sym_private_key;

/*
  call-seq:
    creds = ServerCredentials.new(nil,
                                  [{private_key: <pem_private_key1>,
                                   {cert_chain: <pem_cert_chain1>}],
                                  force_client_auth)
    creds = ServerCredentials.new(pem_root_certs,
                                  [{private_key: <pem_private_key1>,
                                   {cert_chain: <pem_cert_chain1>}],
                                  force_client_auth)

    pem_root_certs: (optional) PEM encoding of the server root certificate
    pem_private_key: (required) PEM encoding of the server's private keys
    force_client_auth: indicatees

    Initializes ServerCredential instances. */
static VALUE grpc_rb_server_credentials_init(VALUE self, VALUE pem_root_certs,
                                             VALUE pem_key_certs,
                                             VALUE force_client_auth) {
  grpc_rb_server_credentials *wrapper = NULL;
  grpc_server_credentials *creds = NULL;
  grpc_ssl_pem_key_cert_pair *key_cert_pairs = NULL;
  VALUE cert = Qnil;
  VALUE key = Qnil;
  VALUE key_cert = Qnil;
  int auth_client = 0;
  int num_key_certs = 0;
  int i;

  if (NIL_P(force_client_auth) ||
      !(force_client_auth == Qfalse || force_client_auth == Qtrue)) {
    rb_raise(rb_eTypeError,
             "bad force_client_auth: got:<%s> want: <True|False|nil>",
             rb_obj_classname(force_client_auth));
    return Qnil;
  }
  if (NIL_P(pem_key_certs) || TYPE(pem_key_certs) != T_ARRAY) {
    rb_raise(rb_eTypeError, "bad pem_key_certs: got:<%s> want: <Array>",
             rb_obj_classname(pem_key_certs));
    return Qnil;
  }
  num_key_certs = RARRAY_LEN(pem_key_certs);
  if (num_key_certs == 0) {
    rb_raise(rb_eTypeError, "bad pem_key_certs: it had no elements");
    return Qnil;
  }
  for (i = 0; i < num_key_certs; i++) {
    key_cert = rb_ary_entry(pem_key_certs, i);
    if (key_cert == Qnil) {
      rb_raise(rb_eTypeError,
               "could not create a server credential: nil key_cert");
      return Qnil;
    } else if (TYPE(key_cert) != T_HASH) {
      rb_raise(rb_eTypeError,
               "could not create a server credential: want <Hash>, got <%s>",
               rb_obj_classname(key_cert));
      return Qnil;
    } else if (rb_hash_aref(key_cert, sym_private_key) == Qnil) {
      rb_raise(rb_eTypeError,
               "could not create a server credential: want nil private key");
      return Qnil;
    } else if (rb_hash_aref(key_cert, sym_cert_chain) == Qnil) {
      rb_raise(rb_eTypeError,
               "could not create a server credential: want nil cert chain");
      return Qnil;
    }
  }

  auth_client = TYPE(force_client_auth) == T_TRUE;
  key_cert_pairs = ALLOC_N(grpc_ssl_pem_key_cert_pair, num_key_certs);
  for (i = 0; i < num_key_certs; i++) {
    key_cert = rb_ary_entry(pem_key_certs, i);
    key = rb_hash_aref(key_cert, sym_private_key);
    cert = rb_hash_aref(key_cert, sym_cert_chain);
    key_cert_pairs[i].private_key = RSTRING_PTR(key);
    key_cert_pairs[i].cert_chain = RSTRING_PTR(cert);
  }

  TypedData_Get_Struct(self, grpc_rb_server_credentials,
                       &grpc_rb_server_credentials_data_type, wrapper);

  if (pem_root_certs == Qnil) {
    creds = grpc_ssl_server_credentials_create(NULL, key_cert_pairs,
                                               num_key_certs,
                                               auth_client, NULL);
  } else {
    creds = grpc_ssl_server_credentials_create(RSTRING_PTR(pem_root_certs),
                                               key_cert_pairs, num_key_certs,
                                               auth_client, NULL);
  }
  xfree(key_cert_pairs);
  if (creds == NULL) {
    rb_raise(rb_eRuntimeError, "could not create a credentials, not sure why");
    return Qnil;
  }
  wrapper->wrapped = creds;

  /* Add the input objects as hidden fields to preserve them. */
  rb_ivar_set(self, id_pem_key_certs, pem_key_certs);
  rb_ivar_set(self, id_pem_root_certs, pem_root_certs);

  return self;
}

void Init_grpc_server_credentials() {
  grpc_rb_cServerCredentials =
      rb_define_class_under(grpc_rb_mGrpcCore, "ServerCredentials", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(grpc_rb_cServerCredentials,
                       grpc_rb_server_credentials_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(grpc_rb_cServerCredentials, "initialize",
                   grpc_rb_server_credentials_init, 3);
  rb_define_method(grpc_rb_cServerCredentials, "initialize_copy",
                   grpc_rb_server_credentials_init_copy, 1);

  id_pem_key_certs = rb_intern("__pem_key_certs");
  id_pem_root_certs = rb_intern("__pem_root_certs");
  sym_private_key = ID2SYM(rb_intern("private_key"));
  sym_cert_chain = ID2SYM(rb_intern("cert_chain"));
}

/* Gets the wrapped grpc_server_credentials from the ruby wrapper */
grpc_server_credentials *grpc_rb_get_wrapped_server_credentials(VALUE v) {
  grpc_rb_server_credentials *wrapper = NULL;
  TypedData_Get_Struct(v, grpc_rb_server_credentials,
                       &grpc_rb_server_credentials_data_type, wrapper);
  return wrapper->wrapped;
}
