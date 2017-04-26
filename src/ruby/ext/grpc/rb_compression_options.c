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
#include "rb_byte_buffer.h"
#include "rb_grpc_imports.generated.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/impl/codegen/compression_types.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <string.h>

#include "rb_grpc.h"

static VALUE grpc_rb_cCompressionOptions = Qnil;

/* Ruby Ids for the names of valid compression levels. */
static VALUE id_compress_level_none = Qnil;
static VALUE id_compress_level_low = Qnil;
static VALUE id_compress_level_medium = Qnil;
static VALUE id_compress_level_high = Qnil;

/* grpc_rb_compression_options wraps a grpc_compression_options.
 * It can be used to get the channel argument key-values for specific
 * compression settings. */

/* Note that ruby objects of this type don't carry any state in other
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
  grpc_rb_compression_options *wrapper = NULL;

  grpc_ruby_once_init();

  wrapper = gpr_malloc(sizeof(grpc_rb_compression_options));
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

/* Gets the compression internal enum value of a compression level given its
 * name. */
grpc_compression_level grpc_rb_compression_options_level_name_to_value_internal(
    VALUE level_name) {
  Check_Type(level_name, T_SYMBOL);

  /* Check the compression level of the name passed in, and see which macro
   * from the GRPC core header files match. */
  if (id_compress_level_none == SYM2ID(level_name)) {
    return GRPC_COMPRESS_LEVEL_NONE;
  } else if (id_compress_level_low == SYM2ID(level_name)) {
    return GRPC_COMPRESS_LEVEL_LOW;
  } else if (id_compress_level_medium == SYM2ID(level_name)) {
    return GRPC_COMPRESS_LEVEL_MED;
  } else if (id_compress_level_high == SYM2ID(level_name)) {
    return GRPC_COMPRESS_LEVEL_HIGH;
  }

  rb_raise(rb_eArgError,
           "Unrecognized compression level name."
           "Valid compression level names are none, low, medium, and high.");

  /* Dummy return statement. */
  return GRPC_COMPRESS_LEVEL_NONE;
}

/* Sets the default compression level, given the name of a compression level.
 * Throws an error if no algorithm matched. */
void grpc_rb_compression_options_set_default_level(
    grpc_compression_options *options, VALUE new_level_name) {
  options->default_level.level =
      grpc_rb_compression_options_level_name_to_value_internal(new_level_name);
  options->default_level.is_set = 1;
}

/* Gets the internal value of a compression algorithm suitable as the value
 * in a GRPC core channel arguments hash.
 * algorithm_value is an out parameter.
 * Raises an error if the name of the algorithm passed in is invalid. */
void grpc_rb_compression_options_algorithm_name_to_value_internal(
    grpc_compression_algorithm *algorithm_value, VALUE algorithm_name) {
  grpc_slice name_slice;
  VALUE algorithm_name_as_string = Qnil;
  char *tmp_str = NULL;

  Check_Type(algorithm_name, T_SYMBOL);

  /* Convert the algorithm symbol to a ruby string, so that we can get the
   * correct C string out of it. */
  algorithm_name_as_string = rb_funcall(algorithm_name, rb_intern("to_s"), 0);

  name_slice = grpc_slice_from_copied_buffer(RSTRING_PTR(algorithm_name_as_string), RSTRING_LEN(algorithm_name_as_string));

  /* Raise an error if the name isn't recognized as a compression algorithm by
   * the algorithm parse function
   * in GRPC core. */
  if(!grpc_compression_algorithm_parse(name_slice, algorithm_value)) {
    tmp_str = grpc_slice_to_c_string(name_slice);
    rb_raise(rb_eNameError, "Invalid compression algorithm name: %s",
             tmp_str);
  }

  grpc_slice_unref(name_slice);
}

/* Indicates whether a given algorithm is enabled on this instance, given the
 * readable algorithm name. */
VALUE grpc_rb_compression_options_is_algorithm_enabled(VALUE self,
                                                       VALUE algorithm_name) {
  grpc_rb_compression_options *wrapper = NULL;
  grpc_compression_algorithm internal_algorithm_value;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);
  grpc_rb_compression_options_algorithm_name_to_value_internal(
      &internal_algorithm_value, algorithm_name);

  if (grpc_compression_options_is_algorithm_enabled(wrapper->wrapped,
                                                    internal_algorithm_value)) {
    return Qtrue;
  }
  return Qfalse;
}

/* Sets the default algorithm to the name of the algorithm passed in.
 * Raises an error if the name is not a valid compression algorithm name. */
void grpc_rb_compression_options_set_default_algorithm(
    grpc_compression_options *options, VALUE algorithm_name) {
  grpc_rb_compression_options_algorithm_name_to_value_internal(
      &options->default_algorithm.algorithm, algorithm_name);
  options->default_algorithm.is_set = 1;
}

/* Disables an algorithm on the current instance, given the name of an
 * algorithm.
 * Fails if the algorithm name is invalid. */
void grpc_rb_compression_options_disable_algorithm(
    grpc_compression_options *compression_options, VALUE algorithm_name) {
  grpc_compression_algorithm internal_algorithm_value;

  grpc_rb_compression_options_algorithm_name_to_value_internal(
      &internal_algorithm_value, algorithm_name);
  grpc_compression_options_disable_algorithm(compression_options,
                                             internal_algorithm_value);
}

/* Provides a ruby hash of GRPC core channel argument key-values that
 * correspond to the compression settings on this instance. */
VALUE grpc_rb_compression_options_to_hash(VALUE self) {
  grpc_rb_compression_options *wrapper = NULL;
  grpc_compression_options *compression_options = NULL;
  VALUE channel_arg_hash = rb_hash_new();
  VALUE key = Qnil;
  VALUE value = Qnil;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);
  compression_options = wrapper->wrapped;

  /* Add key-value pairs to the new Ruby hash. It can be used
   * as GRPC core channel arguments. */
  if (compression_options->default_level.is_set) {
    key = rb_str_new2(GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL);
    value = INT2NUM((int)compression_options->default_level.level);
    rb_hash_aset(channel_arg_hash, key, value);
  }

  if (compression_options->default_algorithm.is_set) {
    key = rb_str_new2(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM);
    value = INT2NUM((int)compression_options->default_algorithm.algorithm);
    rb_hash_aset(channel_arg_hash, key, value);
  }

  key = rb_str_new2(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET);
  value = INT2NUM((int)compression_options->enabled_algorithms_bitset);
  rb_hash_aset(channel_arg_hash, key, value);

  return channel_arg_hash;
}

/* Converts an internal enum level value to a readable level name.
 * Fails if the level value is invalid. */
VALUE grpc_rb_compression_options_level_value_to_name_internal(
    grpc_compression_level compression_value) {
  switch (compression_value) {
    case GRPC_COMPRESS_LEVEL_NONE:
      return ID2SYM(id_compress_level_none);
    case GRPC_COMPRESS_LEVEL_LOW:
      return ID2SYM(id_compress_level_low);
    case GRPC_COMPRESS_LEVEL_MED:
      return ID2SYM(id_compress_level_medium);
    case GRPC_COMPRESS_LEVEL_HIGH:
      return ID2SYM(id_compress_level_high);
    default:
      rb_raise(
          rb_eArgError,
          "Failed to convert compression level value to name for value: %d",
          (int)compression_value);
      /* return something to avoid compiler error about no return */
      return Qnil;
  }
}

/* Converts an algorithm internal enum value to a readable name.
 * Fails if the enum value is invalid. */
VALUE grpc_rb_compression_options_algorithm_value_to_name_internal(
    grpc_compression_algorithm internal_value) {
  char *algorithm_name = NULL;

  if (!grpc_compression_algorithm_name(internal_value, &algorithm_name)) {
    rb_raise(rb_eArgError, "Failed to convert algorithm value to name");
  }

  return ID2SYM(rb_intern(algorithm_name));
}

/* Gets the readable name of the default algorithm if one has been set.
 * Returns nil if no algorithm has been set. */
VALUE grpc_rb_compression_options_get_default_algorithm(VALUE self) {
  grpc_compression_algorithm internal_value;
  grpc_rb_compression_options *wrapper = NULL;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);

  if (wrapper->wrapped->default_algorithm.is_set) {
    internal_value = wrapper->wrapped->default_algorithm.algorithm;
    return grpc_rb_compression_options_algorithm_value_to_name_internal(
        internal_value);
  }

  return Qnil;
}

/* Gets the internal value of the default compression level that is to be passed
 * to the GRPC core as a channel argument value.
 * A nil return value means that it hasn't been set. */
VALUE grpc_rb_compression_options_get_default_level(VALUE self) {
  grpc_compression_level internal_value;
  grpc_rb_compression_options *wrapper = NULL;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);

  if (wrapper->wrapped->default_level.is_set) {
    internal_value = wrapper->wrapped->default_level.level;
    return grpc_rb_compression_options_level_value_to_name_internal(
        internal_value);
  }

  return Qnil;
}

/* Gets a list of the disabled algorithms as readable names.
 * Returns an empty list if no algorithms have been disabled. */
VALUE grpc_rb_compression_options_get_disabled_algorithms(VALUE self) {
  VALUE disabled_algorithms = rb_ary_new();
  grpc_compression_algorithm internal_value;
  grpc_rb_compression_options *wrapper = NULL;

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);

  for (internal_value = GRPC_COMPRESS_NONE;
       internal_value < GRPC_COMPRESS_ALGORITHMS_COUNT; internal_value++) {
    if (!grpc_compression_options_is_algorithm_enabled(wrapper->wrapped,
                                                       internal_value)) {
      rb_ary_push(disabled_algorithms,
                  grpc_rb_compression_options_algorithm_value_to_name_internal(
                      internal_value));
    }
  }
  return disabled_algorithms;
}

/* Initializes the compression options wrapper.
 * Takes an optional hash parameter.
 *
 * Example call-seq:
 *   options = CompressionOptions.new(
 *     default_level: :none,
 *     disabled_algorithms: [:gzip]
 *   )
 *   channel_arg hash = Hash.new[...]
 *   channel_arg_hash_with_compression_options = channel_arg_hash.merge(options)
 */
VALUE grpc_rb_compression_options_init(int argc, VALUE *argv, VALUE self) {
  grpc_rb_compression_options *wrapper = NULL;
  VALUE default_algorithm = Qnil;
  VALUE default_level = Qnil;
  VALUE disabled_algorithms = Qnil;
  VALUE algorithm_name = Qnil;
  VALUE hash_arg = Qnil;

  rb_scan_args(argc, argv, "01", &hash_arg);

  /* Check if the hash parameter was passed, or if invalid arguments were
   * passed. */
  if (hash_arg == Qnil) {
    return self;
  } else if (TYPE(hash_arg) != T_HASH || argc > 1) {
    rb_raise(rb_eArgError,
             "Invalid arguments. Expecting optional hash parameter");
  }

  TypedData_Get_Struct(self, grpc_rb_compression_options,
                       &grpc_rb_compression_options_data_type, wrapper);

  /* Set the default algorithm if one was chosen. */
  default_algorithm =
      rb_hash_aref(hash_arg, ID2SYM(rb_intern("default_algorithm")));
  if (default_algorithm != Qnil) {
    grpc_rb_compression_options_set_default_algorithm(wrapper->wrapped,
                                                      default_algorithm);
  }

  /* Set the default level if one was chosen. */
  default_level = rb_hash_aref(hash_arg, ID2SYM(rb_intern("default_level")));
  if (default_level != Qnil) {
    grpc_rb_compression_options_set_default_level(wrapper->wrapped,
                                                  default_level);
  }

  /* Set the disabled algorithms if any were chosen. */
  disabled_algorithms =
      rb_hash_aref(hash_arg, ID2SYM(rb_intern("disabled_algorithms")));
  if (disabled_algorithms != Qnil) {
    Check_Type(disabled_algorithms, T_ARRAY);

    for (int i = 0; i < RARRAY_LEN(disabled_algorithms); i++) {
      algorithm_name = rb_ary_entry(disabled_algorithms, i);
      grpc_rb_compression_options_disable_algorithm(wrapper->wrapped,
                                                    algorithm_name);
    }
  }

  return self;
}

void Init_grpc_compression_options() {
  grpc_rb_cCompressionOptions = rb_define_class_under(
      grpc_rb_mGrpcCore, "CompressionOptions", rb_cObject);

  /* Allocates an object managed by the ruby runtime. */
  rb_define_alloc_func(grpc_rb_cCompressionOptions,
                       grpc_rb_compression_options_alloc);

  /* Initializes the ruby wrapper. #new method takes an optional hash argument.
   */
  rb_define_method(grpc_rb_cCompressionOptions, "initialize",
                   grpc_rb_compression_options_init, -1);

  /* Methods for getting the default algorithm, default level, and disabled
   * algorithms as readable names. */
  rb_define_method(grpc_rb_cCompressionOptions, "default_algorithm",
                   grpc_rb_compression_options_get_default_algorithm, 0);
  rb_define_method(grpc_rb_cCompressionOptions, "default_level",
                   grpc_rb_compression_options_get_default_level, 0);
  rb_define_method(grpc_rb_cCompressionOptions, "disabled_algorithms",
                   grpc_rb_compression_options_get_disabled_algorithms, 0);

  /* Determines whether or not an algorithm is enabled, given a readable
   * algorithm name.*/
  rb_define_method(grpc_rb_cCompressionOptions, "algorithm_enabled?",
                   grpc_rb_compression_options_is_algorithm_enabled, 1);

  /* Provides a hash of the compression settings suitable
   * for passing to server or channel args. */
  rb_define_method(grpc_rb_cCompressionOptions, "to_hash",
                   grpc_rb_compression_options_to_hash, 0);
  rb_define_alias(grpc_rb_cCompressionOptions, "to_channel_arg_hash",
                  "to_hash");

  /* Ruby ids for the names of the different compression levels. */
  id_compress_level_none = rb_intern("none");
  id_compress_level_low = rb_intern("low");
  id_compress_level_medium = rb_intern("medium");
  id_compress_level_high = rb_intern("high");
}
