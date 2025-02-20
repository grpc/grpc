/*
 *
 * Copyright 2021 gRPC authors.
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

#include "rb_xds_server_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "rb_grpc.h"
#include "rb_grpc_imports.generated.h"
#include "rb_server_credentials.h"

/* grpc_rb_cXdsServerCredentials is the ruby class that proxies
   grpc_server_credentials. */
static VALUE grpc_rb_cXdsServerCredentials = Qnil;

/* grpc_rb_xds_server_credentials wraps a grpc_server_credentials.  It provides
   a peer ruby object, 'mark' to hold references to objects involved in
   constructing the server credentials. */
typedef struct grpc_rb_xds_server_credentials {
  /* Holder of ruby objects involved in constructing the server credentials */
  VALUE mark;
  /* The actual server credentials */
  grpc_server_credentials* wrapped;
} grpc_rb_xds_server_credentials;

/* Destroys the server credentials instances. */
static void grpc_rb_xds_server_credentials_free_internal(void* p) {
  grpc_rb_xds_server_credentials* wrapper = NULL;
  if (p == NULL) {
    return;
  };
  wrapper = (grpc_rb_xds_server_credentials*)p;

  /* Delete the wrapped object if the mark object is Qnil, which indicates that
     no other object is the actual owner. */
  if (wrapper->wrapped != NULL && wrapper->mark == Qnil) {
    grpc_server_credentials_release(wrapper->wrapped);
    wrapper->wrapped = NULL;
  }

  xfree(p);
}

/* Destroys the server credentials instances. */
static void grpc_rb_xds_server_credentials_free(void* p) {
  grpc_rb_xds_server_credentials_free_internal(p);
}

/* Protects the mark object from GC */
static void grpc_rb_xds_server_credentials_mark(void* p) {
  if (p == NULL) {
    return;
  }
  grpc_rb_xds_server_credentials* wrapper = (grpc_rb_xds_server_credentials*)p;

  /* If it's not already cleaned up, mark the mark object */
  if (wrapper->mark != Qnil) {
    rb_gc_mark(wrapper->mark);
  }
}

static const rb_data_type_t grpc_rb_xds_server_credentials_data_type = {
    "grpc_xds_server_credentials",
    {grpc_rb_xds_server_credentials_mark, grpc_rb_xds_server_credentials_free,
     GRPC_RB_MEMSIZE_UNAVAILABLE, NULL},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocates ServerCredential instances.
   Provides safe initial defaults for the instance fields. */
static VALUE grpc_rb_xds_server_credentials_alloc(VALUE cls) {
  grpc_ruby_init();
  grpc_rb_xds_server_credentials* wrapper =
      ALLOC(grpc_rb_xds_server_credentials);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return TypedData_Wrap_Struct(cls, &grpc_rb_xds_server_credentials_data_type,
                               wrapper);
}

/* The attribute used on the mark object to preserve the fallback_creds. */
static ID id_fallback_creds;

/*
  call-seq:
    creds = ServerCredentials.new(fallback_creds)
    fallback_creds: (ServerCredentials) fallback credentials to create
                    XDS credentials.
    Initializes ServerCredential instances. */
static VALUE grpc_rb_xds_server_credentials_init(VALUE self,
                                                 VALUE fallback_creds) {
  grpc_rb_xds_server_credentials* wrapper = NULL;
  grpc_server_credentials* creds = NULL;

  grpc_server_credentials* grpc_fallback_creds =
      grpc_rb_get_wrapped_server_credentials(fallback_creds);
  creds = grpc_xds_server_credentials_create(grpc_fallback_creds);

  if (creds == NULL) {
    rb_raise(rb_eRuntimeError,
             "the call to grpc_xds_server_credentials_create() failed, could "
             "not create a credentials, see "
             "https://github.com/grpc/grpc/blob/master/TROUBLESHOOTING.md for "
             "debugging tips");
    return Qnil;
  }
  TypedData_Get_Struct(self, grpc_rb_xds_server_credentials,
                       &grpc_rb_xds_server_credentials_data_type, wrapper);
  wrapper->wrapped = creds;

  /* Add the input objects as hidden fields to preserve them. */
  rb_ivar_set(self, id_fallback_creds, fallback_creds);

  return self;
}

void Init_grpc_xds_server_credentials() {
  grpc_rb_cXdsServerCredentials = rb_define_class_under(
      grpc_rb_mGrpcCore, "XdsServerCredentials", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(grpc_rb_cXdsServerCredentials,
                       grpc_rb_xds_server_credentials_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(grpc_rb_cXdsServerCredentials, "initialize",
                   grpc_rb_xds_server_credentials_init, 1);
  rb_define_method(grpc_rb_cXdsServerCredentials, "initialize_copy",
                   grpc_rb_cannot_init_copy, 1);

  id_fallback_creds = rb_intern("__fallback_creds");
}

/* Gets the wrapped grpc_server_credentials from the ruby wrapper */
grpc_server_credentials* grpc_rb_get_wrapped_xds_server_credentials(VALUE v) {
  grpc_rb_xds_server_credentials* wrapper = NULL;
  Check_TypedStruct(v, &grpc_rb_xds_server_credentials_data_type);
  TypedData_Get_Struct(v, grpc_rb_xds_server_credentials,
                       &grpc_rb_xds_server_credentials_data_type, wrapper);
  return wrapper->wrapped;
}

/* Check if v is kind of ServerCredentials */
bool grpc_rb_is_xds_server_credentials(VALUE v) {
  return rb_typeddata_is_kind_of(v, &grpc_rb_xds_server_credentials_data_type);
}
