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

#include "rb_metadata.h"

#include <ruby.h>
#include <string.h>

#include <grpc/grpc.h>
#include "rb_grpc.h"

/* grpc_rb_metadata wraps a grpc_metadata.  It provides a peer ruby object,
 * 'mark' to minimize copying when a metadata is created from ruby. */
typedef struct grpc_rb_metadata {
  /* Holder of ruby objects involved in constructing the metadata */
  VALUE mark;
  /* The actual metadata */
  grpc_metadata *wrapped;
} grpc_rb_metadata;

/* Destroys Metadata instances. */
static void grpc_rb_metadata_free(void *p) {
  if (p == NULL) {
    return;
  };

  /* Because metadata is only created during a call to grpc_call_add_metadata,
   * and the call takes ownership of the metadata, this does not free the
   * wrapped struct, only the wrapper */
  xfree(p);
}

/* Protects the mark object from GC */
static void grpc_rb_metadata_mark(void *p) {
  grpc_rb_metadata *md = NULL;
  if (p == NULL) {
    return;
  }

  md = (grpc_rb_metadata *)p;
  /* If it's not already cleaned up, mark the mark object */
  if (md->mark != Qnil && BUILTIN_TYPE(md->mark) != T_NONE) {
    rb_gc_mark(md->mark);
  }
}

/* Allocates Metadata instances.

   Provides safe default values for the Metadata fields. */
static VALUE grpc_rb_metadata_alloc(VALUE cls) {
  grpc_rb_metadata *wrapper = ALLOC(grpc_rb_metadata);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return Data_Wrap_Struct(cls, grpc_rb_metadata_mark, grpc_rb_metadata_free,
                          wrapper);
}

/* id_key and id_value are the names of the hidden ivars that preserve the
 * original byte_buffer source string */
static ID id_key;
static ID id_value;

/* Initializes Metadata instances. */
static VALUE grpc_rb_metadata_init(VALUE self, VALUE key, VALUE value) {
  grpc_rb_metadata *wrapper = NULL;
  grpc_metadata *md = ALLOC(grpc_metadata);

  /* Use direct pointers to the strings wrapped by the ruby object to avoid
   * copying */
  Data_Get_Struct(self, grpc_rb_metadata, wrapper);
  wrapper->wrapped = md;
  if (TYPE(key) == T_SYMBOL) {
    md->key = (char *)rb_id2name(SYM2ID(key));
  } else { /* StringValueCStr does all other type exclusions for us */
    md->key = StringValueCStr(key);
  }
  md->value = RSTRING_PTR(value);
  md->value_length = RSTRING_LEN(value);

  /* Save references to the original values on the mark object so that the
   * pointers used there are valid for the lifetime of the object. */
  wrapper->mark = rb_class_new_instance(0, NULL, rb_cObject);
  rb_ivar_set(wrapper->mark, id_key, key);
  rb_ivar_set(wrapper->mark, id_value, value);

  return self;
}

/* Clones Metadata instances.

   Gives Metadata a consistent implementation of Ruby's object copy/dup
   protocol. */
static VALUE grpc_rb_metadata_init_copy(VALUE copy, VALUE orig) {
  grpc_rb_metadata *orig_md = NULL;
  grpc_rb_metadata *copy_md = NULL;

  if (copy == orig) {
    return copy;
  }

  /* Raise an error if orig is not a metadata object or a subclass. */
  if (TYPE(orig) != T_DATA ||
      RDATA(orig)->dfree != (RUBY_DATA_FUNC)grpc_rb_metadata_free) {
    rb_raise(rb_eTypeError, "not a %s", rb_obj_classname(rb_cMetadata));
  }

  Data_Get_Struct(orig, grpc_rb_metadata, orig_md);
  Data_Get_Struct(copy, grpc_rb_metadata, copy_md);

  /* use ruby's MEMCPY to make a byte-for-byte copy of the metadata wrapper
   * object. */
  MEMCPY(copy_md, orig_md, grpc_rb_metadata, 1);
  return copy;
}

/* Gets the key from a metadata instance. */
static VALUE grpc_rb_metadata_key(VALUE self) {
  VALUE key = Qnil;
  grpc_rb_metadata *wrapper = NULL;
  grpc_metadata *md = NULL;

  Data_Get_Struct(self, grpc_rb_metadata, wrapper);
  if (wrapper->mark != Qnil) {
    key = rb_ivar_get(wrapper->mark, id_key);
    if (key != Qnil) {
      return key;
    }
  }

  md = wrapper->wrapped;
  if (md == NULL || md->key == NULL) {
    return Qnil;
  }
  return rb_str_new2(md->key);
}

/* Gets the value from a metadata instance. */
static VALUE grpc_rb_metadata_value(VALUE self) {
  VALUE val = Qnil;
  grpc_rb_metadata *wrapper = NULL;
  grpc_metadata *md = NULL;

  Data_Get_Struct(self, grpc_rb_metadata, wrapper);
  if (wrapper->mark != Qnil) {
    val = rb_ivar_get(wrapper->mark, id_value);
    if (val != Qnil) {
      return val;
    }
  }

  md = wrapper->wrapped;
  if (md == NULL || md->value == NULL) {
    return Qnil;
  }
  return rb_str_new2(md->value);
}

/* rb_cMetadata is the Metadata class whose instances proxy grpc_metadata. */
VALUE rb_cMetadata = Qnil;
void Init_grpc_metadata() {
  rb_cMetadata =
      rb_define_class_under(rb_mGrpcCore, "Metadata", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(rb_cMetadata, grpc_rb_metadata_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(rb_cMetadata, "initialize", grpc_rb_metadata_init, 2);
  rb_define_method(rb_cMetadata, "initialize_copy", grpc_rb_metadata_init_copy,
                   1);

  /* Provides accessors for the code and details. */
  rb_define_method(rb_cMetadata, "key", grpc_rb_metadata_key, 0);
  rb_define_method(rb_cMetadata, "value", grpc_rb_metadata_value, 0);

  id_key = rb_intern("__key");
  id_value = rb_intern("__value");
}

/* Gets the wrapped metadata from the ruby wrapper */
grpc_metadata *grpc_rb_get_wrapped_metadata(VALUE v) {
  grpc_rb_metadata *wrapper = NULL;
  Data_Get_Struct(v, grpc_rb_metadata, wrapper);
  return wrapper->wrapped;
}
