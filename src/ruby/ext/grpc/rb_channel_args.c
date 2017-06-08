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

#include "rb_channel_args.h"
#include "rb_grpc_imports.generated.h"

#include <grpc/grpc.h>

#include "rb_grpc.h"

static rb_data_type_t grpc_rb_channel_args_data_type = {
    "grpc_channel_args",
    {GRPC_RB_GC_NOT_MARKED,
     GRPC_RB_GC_DONT_FREE,
     GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* A callback the processes the hash key values in channel_args hash */
static int grpc_rb_channel_create_in_process_add_args_hash_cb(VALUE key,
                                                              VALUE val,
                                                              VALUE args_obj) {
  const char* the_key;
  grpc_channel_args* args;

  switch (TYPE(key)) {
    case T_STRING:
      the_key = StringValuePtr(key);
      break;

    case T_SYMBOL:
      the_key = rb_id2name(SYM2ID(key));
      break;

    default:
      rb_raise(rb_eTypeError, "bad chan arg: got <%s>, want <String|Symbol>",
               rb_obj_classname(key));
      return ST_STOP;
  }

  TypedData_Get_Struct(args_obj, grpc_channel_args,
                       &grpc_rb_channel_args_data_type, args);
  if (args->num_args <= 0) {
    rb_raise(rb_eRuntimeError, "hash_cb bug: num_args is %lu for key:%s",
             args->num_args, StringValueCStr(key));
    return ST_STOP;
  }

  args->args[args->num_args - 1].key = (char*)the_key;
  switch (TYPE(val)) {
    case T_SYMBOL:
      args->args[args->num_args - 1].type = GRPC_ARG_STRING;
      args->args[args->num_args - 1].value.string =
          (char*)rb_id2name(SYM2ID(val));
      --args->num_args;
      return ST_CONTINUE;

    case T_STRING:
      args->args[args->num_args - 1].type = GRPC_ARG_STRING;
      args->args[args->num_args - 1].value.string = StringValueCStr(val);
      --args->num_args;
      return ST_CONTINUE;

    case T_FIXNUM:
      args->args[args->num_args - 1].type = GRPC_ARG_INTEGER;
      args->args[args->num_args - 1].value.integer = NUM2INT(val);
      --args->num_args;
      return ST_CONTINUE;

    default:
      rb_raise(rb_eTypeError, "%s: bad value: got <%s>, want <String|Fixnum>",
               StringValueCStr(key), rb_obj_classname(val));
      return ST_STOP;
  }
  rb_raise(rb_eRuntimeError, "impl bug: hash_cb reached to far while on key:%s",
           StringValueCStr(key));
  return ST_STOP;
}

/* channel_convert_params allows the call to
   grpc_rb_hash_convert_to_channel_args to be made within an rb_protect
   exception-handler.  This allows any allocated memory to be freed before
   propagating any exception that occurs */
typedef struct channel_convert_params {
  VALUE src_hash;
  grpc_channel_args* dst;
} channel_convert_params;

static VALUE grpc_rb_hash_convert_to_channel_args0(VALUE as_value) {
  ID id_size = rb_intern("size");
  VALUE grpc_rb_cChannelArgs = rb_define_class("TmpChannelArgs", rb_cObject);
  channel_convert_params* params = (channel_convert_params*)as_value;
  size_t num_args = 0;

  if (!NIL_P(params->src_hash) && TYPE(params->src_hash) != T_HASH) {
    rb_raise(rb_eTypeError, "bad channel args: got:<%s> want: a hash or nil",
             rb_obj_classname(params->src_hash));
    return Qnil;
  }

  if (TYPE(params->src_hash) == T_HASH) {
    num_args = NUM2INT(rb_funcall(params->src_hash, id_size, 0));
    params->dst->num_args = num_args;
    params->dst->args = ALLOC_N(grpc_arg, num_args);
    MEMZERO(params->dst->args, grpc_arg, num_args);
    rb_hash_foreach(
        params->src_hash, grpc_rb_channel_create_in_process_add_args_hash_cb,
        TypedData_Wrap_Struct(grpc_rb_cChannelArgs,
                              &grpc_rb_channel_args_data_type, params->dst));
    /* reset num_args as grpc_rb_channel_create_in_process_add_args_hash_cb
     * decrements it during has processing */
    params->dst->num_args = num_args;
  }
  return Qnil;
}

void grpc_rb_hash_convert_to_channel_args(VALUE src_hash,
                                          grpc_channel_args* dst) {
  channel_convert_params params;
  int status = 0;

  /* Make a protected call to grpc_rb_hash_convert_channel_args */
  params.src_hash = src_hash;
  params.dst = dst;
  rb_protect(grpc_rb_hash_convert_to_channel_args0, (VALUE)&params, &status);
  if (status != 0) {
    if (dst->args != NULL) {
      /* Free any allocated memory before propagating the error */
      xfree(dst->args);
    }
    rb_jump_tag(status);
  }
}
