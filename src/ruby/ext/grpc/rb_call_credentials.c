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

#include "rb_call_credentials.h"
#include "rb_grpc_imports.generated.h"

#include <ruby/thread.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "rb_call.h"
#include "rb_event_thread.h"
#include "rb_grpc.h"

/* grpc_rb_cCallCredentials is the ruby class that proxies
 * grpc_call_credentials */
static VALUE grpc_rb_cCallCredentials = Qnil;

/* grpc_rb_call_credentials wraps a grpc_call_credentials. It provides a mark
 * object that is used to hold references to any objects used to create the
 * credentials. */
typedef struct grpc_rb_call_credentials {
  /* Holder of ruby objects involved in contructing the credentials */
  VALUE mark;

  /* The actual credentials */
  grpc_call_credentials *wrapped;
} grpc_rb_call_credentials;

typedef struct callback_params {
  VALUE get_metadata;
  grpc_auth_metadata_context context;
  void *user_data;
  grpc_credentials_plugin_metadata_cb callback;
} callback_params;

static VALUE grpc_rb_call_credentials_callback(VALUE callback_args) {
  VALUE result = rb_hash_new();
  VALUE metadata = rb_funcall(rb_ary_entry(callback_args, 0), rb_intern("call"),
                              1, rb_ary_entry(callback_args, 1));
  rb_hash_aset(result, rb_str_new2("metadata"), metadata);
  rb_hash_aset(result, rb_str_new2("status"), INT2NUM(GRPC_STATUS_OK));
  rb_hash_aset(result, rb_str_new2("details"), rb_str_new2(""));
  return result;
}

static VALUE grpc_rb_call_credentials_callback_rescue(VALUE args,
                                                      VALUE exception_object) {
  VALUE result = rb_hash_new();
  VALUE backtrace =
      rb_funcall(rb_funcall(exception_object, rb_intern("backtrace"), 0),
                 rb_intern("join"), 1, rb_str_new2("\n\tfrom "));
  VALUE rb_exception_info =
      rb_funcall(exception_object, rb_intern("inspect"), 0);
  (void)args;
  gpr_log(GPR_INFO, "Call credentials callback failed: %s\n%s",
          StringValueCStr(rb_exception_info), StringValueCStr(backtrace));
  rb_hash_aset(result, rb_str_new2("metadata"), Qnil);
  rb_hash_aset(result, rb_str_new2("status"),
               INT2NUM(GRPC_STATUS_UNAUTHENTICATED));
  rb_hash_aset(result, rb_str_new2("details"), rb_exception_info);
  return result;
}

static void grpc_rb_call_credentials_callback_with_gil(void *param) {
  callback_params *const params = (callback_params *)param;
  VALUE auth_uri = rb_str_new_cstr(params->context.service_url);
  /* Pass the arguments to the proc in a hash, which currently only has they key
     'auth_uri' */
  VALUE callback_args = rb_ary_new();
  VALUE args = rb_hash_new();
  VALUE result;
  grpc_metadata_array md_ary;
  grpc_status_code status;
  VALUE details;
  char *error_details;
  grpc_metadata_array_init(&md_ary);
  rb_hash_aset(args, ID2SYM(rb_intern("jwt_aud_uri")), auth_uri);
  rb_ary_push(callback_args, params->get_metadata);
  rb_ary_push(callback_args, args);
  result = rb_rescue(grpc_rb_call_credentials_callback, callback_args,
                     grpc_rb_call_credentials_callback_rescue, Qnil);
  // Both callbacks return a hash, so result should be a hash
  grpc_rb_md_ary_convert(rb_hash_aref(result, rb_str_new2("metadata")),
                         &md_ary);
  status = NUM2INT(rb_hash_aref(result, rb_str_new2("status")));
  details = rb_hash_aref(result, rb_str_new2("details"));
  error_details = StringValueCStr(details);
  params->callback(params->user_data, md_ary.metadata, md_ary.count, status,
                   error_details);
  grpc_metadata_array_destroy(&md_ary);
  gpr_free(params);
}

static void grpc_rb_call_credentials_plugin_get_metadata(
    void *state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data) {
  callback_params *params = gpr_malloc(sizeof(callback_params));
  params->get_metadata = (VALUE)state;
  params->context = context;
  params->user_data = user_data;
  params->callback = cb;

  grpc_rb_event_queue_enqueue(grpc_rb_call_credentials_callback_with_gil,
                              (void *)(params));
}

static void grpc_rb_call_credentials_plugin_destroy(void *state) {
  (void)state;
  // Not sure what needs to be done here
}

/* Destroys the credentials instances. */
static void grpc_rb_call_credentials_free(void *p) {
  grpc_rb_call_credentials *wrapper;
  if (p == NULL) {
    return;
  }
  wrapper = (grpc_rb_call_credentials *)p;
  grpc_call_credentials_release(wrapper->wrapped);
  wrapper->wrapped = NULL;

  xfree(p);
}

/* Protects the mark object from GC */
static void grpc_rb_call_credentials_mark(void *p) {
  grpc_rb_call_credentials *wrapper = NULL;
  if (p == NULL) {
    return;
  }
  wrapper = (grpc_rb_call_credentials *)p;
  if (wrapper->mark != Qnil) {
    rb_gc_mark(wrapper->mark);
  }
}

static rb_data_type_t grpc_rb_call_credentials_data_type = {
    "grpc_call_credentials",
    {grpc_rb_call_credentials_mark,
     grpc_rb_call_credentials_free,
     GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocates CallCredentials instances.
   Provides safe initial defaults for the instance fields. */
static VALUE grpc_rb_call_credentials_alloc(VALUE cls) {
  grpc_rb_call_credentials *wrapper = ALLOC(grpc_rb_call_credentials);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return TypedData_Wrap_Struct(cls, &grpc_rb_call_credentials_data_type,
                               wrapper);
}

/* Creates a wrapping object for a given call credentials. This should only be
 * called with grpc_call_credentials objects that are not already associated
 * with any Ruby object */
VALUE grpc_rb_wrap_call_credentials(grpc_call_credentials *c, VALUE mark) {
  VALUE rb_wrapper;
  grpc_rb_call_credentials *wrapper;
  if (c == NULL) {
    return Qnil;
  }
  rb_wrapper = grpc_rb_call_credentials_alloc(grpc_rb_cCallCredentials);
  TypedData_Get_Struct(rb_wrapper, grpc_rb_call_credentials,
                       &grpc_rb_call_credentials_data_type, wrapper);
  wrapper->wrapped = c;
  wrapper->mark = mark;
  return rb_wrapper;
}

/* The attribute used on the mark object to hold the callback */
static ID id_callback;

/*
  call-seq:
    creds = Credentials.new auth_proc
  proc: (required) Proc that generates auth metadata
  Initializes CallCredential instances. */
static VALUE grpc_rb_call_credentials_init(VALUE self, VALUE proc) {
  grpc_rb_call_credentials *wrapper = NULL;
  grpc_call_credentials *creds = NULL;
  grpc_metadata_credentials_plugin plugin;

  grpc_ruby_once_init();

  TypedData_Get_Struct(self, grpc_rb_call_credentials,
                       &grpc_rb_call_credentials_data_type, wrapper);

  plugin.get_metadata = grpc_rb_call_credentials_plugin_get_metadata;
  plugin.destroy = grpc_rb_call_credentials_plugin_destroy;
  if (!rb_obj_is_proc(proc)) {
    rb_raise(rb_eTypeError, "Argument to CallCredentials#new must be a proc");
    return Qnil;
  }
  plugin.state = (void *)proc;
  plugin.type = "";

  creds = grpc_metadata_credentials_create_from_plugin(plugin, NULL);
  if (creds == NULL) {
    rb_raise(rb_eRuntimeError, "could not create a credentials, not sure why");
    return Qnil;
  }

  wrapper->mark = proc;
  wrapper->wrapped = creds;
  rb_ivar_set(self, id_callback, proc);

  return self;
}

static VALUE grpc_rb_call_credentials_compose(int argc, VALUE *argv,
                                              VALUE self) {
  grpc_call_credentials *creds;
  grpc_call_credentials *other;
  VALUE mark;
  if (argc == 0) {
    return self;
  }
  mark = rb_ary_new();
  creds = grpc_rb_get_wrapped_call_credentials(self);
  for (int i = 0; i < argc; i++) {
    rb_ary_push(mark, argv[i]);
    other = grpc_rb_get_wrapped_call_credentials(argv[i]);
    creds = grpc_composite_call_credentials_create(creds, other, NULL);
  }
  return grpc_rb_wrap_call_credentials(creds, mark);
}

void Init_grpc_call_credentials() {
  grpc_rb_cCallCredentials =
      rb_define_class_under(grpc_rb_mGrpcCore, "CallCredentials", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(grpc_rb_cCallCredentials,
                       grpc_rb_call_credentials_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(grpc_rb_cCallCredentials, "initialize",
                   grpc_rb_call_credentials_init, 1);
  rb_define_method(grpc_rb_cCallCredentials, "initialize_copy",
                   grpc_rb_cannot_init_copy, 1);
  rb_define_method(grpc_rb_cCallCredentials, "compose",
                   grpc_rb_call_credentials_compose, -1);

  id_callback = rb_intern("__callback");
}

/* Gets the wrapped grpc_call_credentials from the ruby wrapper */
grpc_call_credentials *grpc_rb_get_wrapped_call_credentials(VALUE v) {
  grpc_rb_call_credentials *wrapper = NULL;
  TypedData_Get_Struct(v, grpc_rb_call_credentials,
                       &grpc_rb_call_credentials_data_type, wrapper);
  return wrapper->wrapped;
}
