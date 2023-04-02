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

#include "rb_xds_channel_credentials.h"

#include <string.h>

#include "rb_call_credentials.h"
#include "rb_channel_credentials.h"
#include "rb_grpc.h"
#include "rb_grpc_imports.generated.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

/* grpc_rb_cXdsChannelCredentials is the ruby class that proxies
   grpc_channel_credentials. */
static VALUE grpc_rb_cXdsChannelCredentials = Qnil;

/* grpc_rb_xds_channel_credentials wraps a grpc_channel_credentials.  It
 * provides a mark object that is used to hold references to any objects used to
 * create the credentials. */
typedef struct grpc_rb_xds_channel_credentials {
  /* Holder of ruby objects involved in constructing the credentials */
  VALUE mark;

  /* The actual credentials */
  grpc_channel_credentials* wrapped;
} grpc_rb_xds_channel_credentials;

static void grpc_rb_xds_channel_credentials_free_internal(void* p) {
  grpc_rb_xds_channel_credentials* wrapper = NULL;
  if (p == NULL) {
    return;
  };
  wrapper = (grpc_rb_xds_channel_credentials*)p;
  grpc_channel_credentials_release(wrapper->wrapped);
  wrapper->wrapped = NULL;

  xfree(p);
}

/* Destroys the credentials instances. */
static void grpc_rb_xds_channel_credentials_free(void* p) {
  grpc_rb_xds_channel_credentials_free_internal(p);
  grpc_ruby_shutdown();
}

/* Protects the mark object from GC */
static void grpc_rb_xds_channel_credentials_mark(void* p) {
  grpc_rb_xds_channel_credentials* wrapper = NULL;
  if (p == NULL) {
    return;
  }
  wrapper = (grpc_rb_xds_channel_credentials*)p;

  if (wrapper->mark != Qnil) {
    rb_gc_mark(wrapper->mark);
  }
}

static rb_data_type_t grpc_rb_xds_channel_credentials_data_type = {
    "grpc_xds_channel_credentials",
    {grpc_rb_xds_channel_credentials_mark, grpc_rb_xds_channel_credentials_free,
     GRPC_RB_MEMSIZE_UNAVAILABLE, NULL},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocates ChannelCredential instances.
   Provides safe initial defaults for the instance fields. */
static VALUE grpc_rb_xds_channel_credentials_alloc(VALUE cls) {
  grpc_ruby_init();
  grpc_rb_xds_channel_credentials* wrapper =
      ALLOC(grpc_rb_xds_channel_credentials);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return TypedData_Wrap_Struct(cls, &grpc_rb_xds_channel_credentials_data_type,
                               wrapper);
}

/* Creates a wrapping object for a given channel credentials. This should only
 * be called with grpc_channel_credentials objects that are not already
 * associated with any Ruby object. */
VALUE grpc_rb_xds_wrap_channel_credentials(grpc_channel_credentials* c,
                                           VALUE mark) {
  grpc_rb_xds_channel_credentials* wrapper;
  if (c == NULL) {
    return Qnil;
  }
  VALUE rb_wrapper =
      grpc_rb_xds_channel_credentials_alloc(grpc_rb_cXdsChannelCredentials);
  TypedData_Get_Struct(rb_wrapper, grpc_rb_xds_channel_credentials,
                       &grpc_rb_xds_channel_credentials_data_type, wrapper);
  wrapper->wrapped = c;
  wrapper->mark = mark;
  return rb_wrapper;
}

/* The attribute used on the mark object to hold the fallback creds. */
static ID id_fallback_creds;

/*
  call-seq:
    fallback_creds: (ChannelCredentials) fallback credentials to create
    XDS credentials
    Initializes Credential instances. */
static VALUE grpc_rb_xds_channel_credentials_init(VALUE self,
                                                  VALUE fallback_creds) {
  grpc_rb_xds_channel_credentials* wrapper = NULL;
  grpc_channel_credentials* grpc_fallback_creds =
      grpc_rb_get_wrapped_channel_credentials(fallback_creds);
  grpc_channel_credentials* creds =
      grpc_xds_credentials_create(grpc_fallback_creds);
  if (creds == NULL) {
    rb_raise(rb_eRuntimeError,
             "the call to grpc_xds_credentials_create() failed, could not "
             "create a credentials, , see "
             "https://github.com/grpc/grpc/blob/master/TROUBLESHOOTING.md for "
             "debugging tips");
    return Qnil;
  }

  TypedData_Get_Struct(self, grpc_rb_xds_channel_credentials,
                       &grpc_rb_xds_channel_credentials_data_type, wrapper);
  wrapper->wrapped = creds;

  /* Add the input objects as hidden fields to preserve them. */
  rb_ivar_set(self, id_fallback_creds, fallback_creds);

  return self;
}

// TODO: de-duplicate this code with the similar method in
// rb_channel_credentials.c, after putting ChannelCredentials and
// XdsChannelCredentials under a common parent class
static VALUE grpc_rb_xds_channel_credentials_compose(int argc, VALUE* argv,
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
  creds = grpc_rb_get_wrapped_xds_channel_credentials(self);
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
  return grpc_rb_xds_wrap_channel_credentials(creds, mark);
}

void Init_grpc_xds_channel_credentials() {
  grpc_rb_cXdsChannelCredentials = rb_define_class_under(
      grpc_rb_mGrpcCore, "XdsChannelCredentials", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(grpc_rb_cXdsChannelCredentials,
                       grpc_rb_xds_channel_credentials_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(grpc_rb_cXdsChannelCredentials, "initialize",
                   grpc_rb_xds_channel_credentials_init, 1);
  rb_define_method(grpc_rb_cXdsChannelCredentials, "initialize_copy",
                   grpc_rb_cannot_init_copy, 1);
  rb_define_method(grpc_rb_cXdsChannelCredentials, "compose",
                   grpc_rb_xds_channel_credentials_compose, -1);

  id_fallback_creds = rb_intern("__fallback_creds");
}

/* Gets the wrapped grpc_channel_credentials from the ruby wrapper */
grpc_channel_credentials* grpc_rb_get_wrapped_xds_channel_credentials(VALUE v) {
  grpc_rb_xds_channel_credentials* wrapper = NULL;
  Check_TypedStruct(v, &grpc_rb_xds_channel_credentials_data_type);
  TypedData_Get_Struct(v, grpc_rb_xds_channel_credentials,
                       &grpc_rb_xds_channel_credentials_data_type, wrapper);
  return wrapper->wrapped;
}

bool grpc_rb_is_xds_channel_credentials(VALUE v) {
  return rb_typeddata_is_kind_of(v, &grpc_rb_xds_channel_credentials_data_type);
}
