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

#include <ruby/ruby.h>

#include "rb_compression_options.h"
#include "rb_grpc_imports.generated.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/impl/codegen/alloc.h>
#include <grpc/impl/codegen/compression_types.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <string.h>

#include "rb_grpc.h"

static VALUE grpc_rb_cCompressionOptions = Qnil;

/* grpc_rb_compression_options wraps a grpc_compression_options.
 * Note that ruby objects of this type don't carry any state in other
 * Ruby objects and don't have a mark for GC. */
typedef struct grpc_rb_compression_options {
  /* The actual compression options that's being wrapped */
  grpc_compression_options *wrapped;
} grpc_rb_compression_options;

/* Destroys the compression options instances and free the
 * wrapped grpc compression options. */
static void grpc_rb_compression_options_free(void *p) {
  grpc_rb_compression_options *wrapper = NULL;
  if (p == NULL) {
    return;
  };
  wrapper = (grpc_rb_compression_options *)p;

  if (wrapper->wrapped != NULL) {
    gpr_free(wrapper->wrapped);
    wrapper->wrapped = NULL;
  }

  xfree(p);
}

/* Ruby recognized data type for the CompressionOptions class. */
static rb_data_type_t grpc_rb_compression_options_data_type = {
    "grpc_compression_options",
    {NULL,
     grpc_rb_compression_options_free,
     GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocates CompressionOptions instances.
   Allocate the wrapped grpc compression options and
   initialize it here too. */
static VALUE grpc_rb_compression_options_alloc(VALUE cls) {
  grpc_rb_compression_options *wrapper =
      gpr_malloc(sizeof(grpc_rb_compression_options));
  wrapper->wrapped = NULL;
  wrapper->wrapped = gpr_malloc(sizeof(grpc_compression_options));
  grpc_compression_options_init(wrapper->wrapped);

  return TypedData_Wrap_Struct(cls, &grpc_rb_compression_options_data_type,
                               wrapper);
}

/* Disables a compression algorithm, given the GRPC core internal number of a
 * compression algorithm. */
VALUE grpc_rb_compression_options_disable_compression_algorithm_internal(
    VALUE self, VALUE algorithm_to_disable) {
  grpc_compression_algorithm compression_algorithm = 0;
  grpc_rb_compression_options *wrapper = NULL;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);
  compression_algorithm =
      (grpc_compression_algorithm)NUM2INT(algorithm_to_disable);

  grpc_compression_options_disable_algorithm(wrapper->wrapped,
                                             compression_algorithm);

  return Qnil;
}

/* Provides a bitset as a ruby number that is suitable to pass to
 * the GRPC core as a channel argument to enable compression algorithms. */
VALUE grpc_rb_compression_options_get_enabled_algorithms_bitset(VALUE self) {
  grpc_rb_compression_options *wrapper = NULL;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);
  return INT2NUM((int)wrapper->wrapped->enabled_algorithms_bitset);
}

void grpc_rb_compression_options_set_default_level_helper(
    grpc_compression_options *compression_options,
    grpc_compression_level level) {
  compression_options->default_level.is_set |= 1;
  compression_options->default_level.level = level;
}

/* Sets the default compression level, given the name of a compression level.
 * Throws an error if no algorithm matched. */
VALUE grpc_rb_compression_options_set_default_level(VALUE self,
                                                    VALUE new_level) {
  char *level_name = NULL;
  grpc_rb_compression_options *wrapper = NULL;
  long name_len = 0;
  VALUE ruby_str = Qnil;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);

  /* Take both string and symbol parameters */
  ruby_str = rb_funcall(new_level, rb_intern("to_s"), 0);

  level_name = RSTRING_PTR(ruby_str);
  name_len = RSTRING_LEN(ruby_str);

  /* Check the compression level of the name passed in, and see which macro
   * from the GRPC core header files match. */
  if (strncmp(level_name, "none", name_len) == 0) {
    grpc_rb_compression_options_set_default_level_helper(
        wrapper->wrapped, GRPC_COMPRESS_LEVEL_NONE);
  } else if (strncmp(level_name, "low", name_len) == 0) {
    grpc_rb_compression_options_set_default_level_helper(
        wrapper->wrapped, GRPC_COMPRESS_LEVEL_LOW);
  } else if (strncmp(level_name, "medium", name_len) == 0) {
    grpc_rb_compression_options_set_default_level_helper(
        wrapper->wrapped, GRPC_COMPRESS_LEVEL_MED);
  } else if (strncmp(level_name, "high", name_len) == 0) {
    grpc_rb_compression_options_set_default_level_helper(
        wrapper->wrapped, GRPC_COMPRESS_LEVEL_HIGH);
  } else {
    rb_raise(rb_eNameError,
             "Invalid compression level name. Supported levels: none, low, "
             "medium, high");
  }

  return Qnil;
}

/* Gets the internal value of a compression algorithm suitable as the value
 * in a GRPC core channel arguments hash.
 * Raises an error if the name of the algorithm passed in is invalid. */
void grpc_rb_compression_options_get_internal_value_of_algorithm(
    VALUE algorithm_name, grpc_compression_algorithm *compression_algorithm) {
  VALUE ruby_str = Qnil;
  char *name_str = NULL;
  long name_len = 0;

  /* Accept ruby symbol and string parameters. */
  ruby_str = rb_funcall(algorithm_name, rb_intern("to_s"), 0);
  name_str = RSTRING_PTR(ruby_str);
  name_len = RSTRING_LEN(ruby_str);

  /* Raise an error if the name isn't recognized as a compression algorithm by
   * the algorithm parse function
   * in GRPC core. */
  if (!grpc_compression_algorithm_parse(name_str, name_len,
                                        compression_algorithm)) {
    rb_raise(rb_eNameError,
             "Invalid compression algorithm name.");
  }
}

/* Sets the default algorithm to the name of the algorithm passed in.
 * Raises an error if the name is not a valid compression algorithm name. */
VALUE grpc_rb_compression_options_set_default_algorithm(VALUE self,
                                                        VALUE algorithm_name) {
  grpc_rb_compression_options *wrapper = NULL;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);

  grpc_rb_compression_options_get_internal_value_of_algorithm(
      algorithm_name, &wrapper->wrapped->default_algorithm.algorithm);
  wrapper->wrapped->default_algorithm.is_set |= 1;

  return Qnil;
}

/* Gets the internal value of the default compression level that is to be passed
 * to the
 * the GRPC core as a channel argument value.
 * A nil return value means that it hasn't been set. */
VALUE grpc_rb_compression_options_default_algorithm_internal_value(VALUE self) {
  grpc_rb_compression_options *wrapper = NULL;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);

  if (wrapper->wrapped->default_algorithm.is_set) {
    return INT2NUM(wrapper->wrapped->default_algorithm.algorithm);
  } else {
    return Qnil;
  }
}

/* Gets the internal value of the default compression level that is to be passed
 * to the GRPC core as a channel argument value.
 * A nil return value means that it hasn't been set. */
VALUE grpc_rb_compression_options_default_level_internal_value(VALUE self) {
  grpc_rb_compression_options *wrapper = NULL;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);

  if (wrapper->wrapped->default_level.is_set) {
    return INT2NUM((int)wrapper->wrapped->default_level.level);
  } else {
    return Qnil;
  }
}

/* Disables compression algorithms by their names. Raises an error if an unkown
 * name was passed. */
VALUE grpc_rb_compression_options_disable_algorithms(int argc, VALUE *argv,
                                                     VALUE self) {
  VALUE algorithm_names = Qnil;
  VALUE ruby_str = Qnil;
  grpc_compression_algorithm internal_algorithm_value;

  /* read variadic argument list of names into the algorithm_name ruby array. */
  rb_scan_args(argc, argv, "0*", &algorithm_names);

  for (int i = 0; i < RARRAY_LEN(algorithm_names); i++) {
    ruby_str =
        rb_funcall(rb_ary_entry(algorithm_names, i), rb_intern("to_s"), 0);
    grpc_rb_compression_options_get_internal_value_of_algorithm(
        ruby_str, &internal_algorithm_value);
    rb_funcall(self, rb_intern("disable_algorithm_internal"), 1,
               LONG2NUM((long)internal_algorithm_value));
  }

  return Qnil;
}

/* Provides a ruby hash of GRPC core channel argument key-values that
 * correspond to the compression settings on this instance. */
VALUE grpc_rb_compression_options_to_hash(VALUE self) {
  grpc_rb_compression_options *wrapper = NULL;
  grpc_compression_options *compression_options = NULL;
  VALUE channel_arg_hash = rb_funcall(rb_cHash, rb_intern("new"), 0);

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);
  compression_options = wrapper->wrapped;

  /* Add key-value pairs to the new Ruby hash. It can be used
   * as GRPC core channel arguments. */
  if (compression_options->default_level.is_set) {
    rb_funcall(channel_arg_hash, rb_intern("[]="), 2,
               rb_str_new2(GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL),
               INT2NUM((int)compression_options->default_level.level));
  }

  if (compression_options->default_algorithm.is_set) {
    rb_funcall(channel_arg_hash, rb_intern("[]="), 2,
               rb_str_new2(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM),
               INT2NUM((int)compression_options->default_algorithm.algorithm));
  }

  rb_funcall(channel_arg_hash, rb_intern("[]="), 2,
             rb_str_new2(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET),
             INT2NUM((int)compression_options->enabled_algorithms_bitset));

  return channel_arg_hash;
}

void Init_grpc_compression_options() {
  grpc_rb_cCompressionOptions = rb_define_class_under(
      grpc_rb_mGrpcCore, "CompressionOptions", rb_cObject);

  /* Allocates an object managed by the ruby runtime. */
  rb_define_alloc_func(grpc_rb_cCompressionOptions,
                       grpc_rb_compression_options_alloc);

  /* Private method for disabling algorithms by a variadic list of names. */
  rb_define_private_method(grpc_rb_cCompressionOptions, "disable_algorithms",
                           grpc_rb_compression_options_disable_algorithms, -1);
  /* Private method for disabling an algorithm by its enum value. */
  rb_define_private_method(
      grpc_rb_cCompressionOptions, "disable_algorithm_internal",
      grpc_rb_compression_options_disable_compression_algorithm_internal, 1);

  /* Private method for getting the bitset of enabled algorithms. */
  rb_define_private_method(
      grpc_rb_cCompressionOptions, "enabled_algorithms_bitset",
      grpc_rb_compression_options_get_enabled_algorithms_bitset, 0);

  /* Private method for setting the default algorithm by name. */
  rb_define_private_method(grpc_rb_cCompressionOptions, "set_default_algorithm",
                           grpc_rb_compression_options_set_default_algorithm,
                           1);
  /* Private method for getting the internal enum value of the default
   * algorithm. */
  rb_define_private_method(
      grpc_rb_cCompressionOptions, "default_algorithm_internal_value",
      grpc_rb_compression_options_default_algorithm_internal_value, 0);

  /* Private method for setting the default compression level by name. */
  rb_define_private_method(grpc_rb_cCompressionOptions, "set_default_level",
                           grpc_rb_compression_options_set_default_level, 1);

  /* Private method for getting the internal enum value of the default level. */
  rb_define_private_method(
      grpc_rb_cCompressionOptions, "default_level_internal_value",
      grpc_rb_compression_options_default_level_internal_value, 0);

  /* Public method for returning a hash of the compression settings suitable
   * for passing to server or channel args. */
  rb_define_method(grpc_rb_cCompressionOptions, "to_hash",
                   grpc_rb_compression_options_to_hash, 0);
}
