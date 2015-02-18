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

#include "rb_byte_buffer.h"

#include <ruby.h>

#include <grpc/grpc.h>
#include <grpc/support/slice.h>
#include "rb_grpc.h"

/* grpc_rb_byte_buffer wraps a grpc_byte_buffer.  It provides a peer ruby
 * object, 'mark' to minimize copying when a byte_buffer is created from
 * ruby. */
typedef struct grpc_rb_byte_buffer {
  /* Holder of ruby objects involved in constructing the status */
  VALUE mark;
  /* The actual status */
  grpc_byte_buffer *wrapped;
} grpc_rb_byte_buffer;

/* Destroys ByteBuffer instances. */
static void grpc_rb_byte_buffer_free(void *p) {
  grpc_rb_byte_buffer *bb = NULL;
  if (p == NULL) {
    return;
  };
  bb = (grpc_rb_byte_buffer *)p;

  /* Deletes the wrapped object if the mark object is Qnil, which indicates
   * that no other object is the actual owner. */
  if (bb->wrapped != NULL && bb->mark == Qnil) {
    grpc_byte_buffer_destroy(bb->wrapped);
  }

  xfree(p);
}

/* Protects the mark object from GC */
static void grpc_rb_byte_buffer_mark(void *p) {
  grpc_rb_byte_buffer *bb = NULL;
  if (p == NULL) {
    return;
  }
  bb = (grpc_rb_byte_buffer *)p;

  /* If it's not already cleaned up, mark the mark object */
  if (bb->mark != Qnil && BUILTIN_TYPE(bb->mark) != T_NONE) {
    rb_gc_mark(bb->mark);
  }
}

/* id_source is the name of the hidden ivar the preserves the original
 * byte_buffer source string */
static ID id_source;

/* Allocates ByteBuffer instances.

   Provides safe default values for the byte_buffer fields. */
static VALUE grpc_rb_byte_buffer_alloc(VALUE cls) {
  grpc_rb_byte_buffer *wrapper = ALLOC(grpc_rb_byte_buffer);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return Data_Wrap_Struct(cls, grpc_rb_byte_buffer_mark,
                          grpc_rb_byte_buffer_free, wrapper);
}

/* Clones ByteBuffer instances.

   Gives ByteBuffer a consistent implementation of Ruby's object copy/dup
   protocol. */
static VALUE grpc_rb_byte_buffer_init_copy(VALUE copy, VALUE orig) {
  grpc_rb_byte_buffer *orig_bb = NULL;
  grpc_rb_byte_buffer *copy_bb = NULL;

  if (copy == orig) {
    return copy;
  }

  /* Raise an error if orig is not a metadata object or a subclass. */
  if (TYPE(orig) != T_DATA ||
      RDATA(orig)->dfree != (RUBY_DATA_FUNC)grpc_rb_byte_buffer_free) {
    rb_raise(rb_eTypeError, "not a %s", rb_obj_classname(rb_cByteBuffer));
  }

  Data_Get_Struct(orig, grpc_rb_byte_buffer, orig_bb);
  Data_Get_Struct(copy, grpc_rb_byte_buffer, copy_bb);

  /* use ruby's MEMCPY to make a byte-for-byte copy of the metadata wrapper
   * object. */
  MEMCPY(copy_bb, orig_bb, grpc_rb_byte_buffer, 1);
  return copy;
}

/* id_empty is used to return the empty string from to_s when necessary. */
static ID id_empty;

static VALUE grpc_rb_byte_buffer_to_s(VALUE self) {
  grpc_rb_byte_buffer *wrapper = NULL;
  grpc_byte_buffer *bb = NULL;
  grpc_byte_buffer_reader *reader = NULL;
  char *output = NULL;
  size_t length = 0;
  size_t offset = 0;
  VALUE output_obj = Qnil;
  gpr_slice next;

  Data_Get_Struct(self, grpc_rb_byte_buffer, wrapper);
  output_obj = rb_ivar_get(wrapper->mark, id_source);
  if (output_obj != Qnil) {
    /* From ruby, ByteBuffers are immutable so if a source is set, return that
     * as the to_s value */
    return output_obj;
  }

  /* Read the bytes. */
  bb = wrapper->wrapped;
  if (bb == NULL) {
    return rb_id2str(id_empty);
  }
  length = grpc_byte_buffer_length(bb);
  if (length == 0) {
    return rb_id2str(id_empty);
  }
  reader = grpc_byte_buffer_reader_create(bb);
  output = xmalloc(length);
  while (grpc_byte_buffer_reader_next(reader, &next) != 0) {
    memcpy(output + offset, GPR_SLICE_START_PTR(next), GPR_SLICE_LENGTH(next));
    offset += GPR_SLICE_LENGTH(next);
  }
  output_obj = rb_str_new(output, length);

  /* Save a references to the computed string in the mark object so that the
   * calling to_s does not do any allocations. */
  wrapper->mark = rb_class_new_instance(0, NULL, rb_cObject);
  rb_ivar_set(wrapper->mark, id_source, output_obj);

  return output_obj;
}

/* Initializes ByteBuffer instances. */
static VALUE grpc_rb_byte_buffer_init(VALUE self, VALUE src) {
  gpr_slice a_slice;
  grpc_rb_byte_buffer *wrapper = NULL;
  grpc_byte_buffer *byte_buffer = NULL;

  if (TYPE(src) != T_STRING) {
    rb_raise(rb_eTypeError, "bad byte_buffer arg: got <%s>, want <String>",
             rb_obj_classname(src));
    return Qnil;
  }
  Data_Get_Struct(self, grpc_rb_byte_buffer, wrapper);
  a_slice = gpr_slice_malloc(RSTRING_LEN(src));
  memcpy(GPR_SLICE_START_PTR(a_slice), RSTRING_PTR(src), RSTRING_LEN(src));
  byte_buffer = grpc_byte_buffer_create(&a_slice, 1);
  gpr_slice_unref(a_slice);

  if (byte_buffer == NULL) {
    rb_raise(rb_eArgError, "could not create a byte_buffer, not sure why");
    return Qnil;
  }
  wrapper->wrapped = byte_buffer;

  /* Save a references to the original string in the mark object so that the
   * pointers used there is valid for the lifetime of the object. */
  wrapper->mark = rb_class_new_instance(0, NULL, rb_cObject);
  rb_ivar_set(wrapper->mark, id_source, src);

  return self;
}

/* rb_cByteBuffer is the ruby class that proxies grpc_byte_buffer. */
VALUE rb_cByteBuffer = Qnil;

void Init_grpc_byte_buffer() {
  rb_cByteBuffer =
      rb_define_class_under(rb_mGrpcCore, "ByteBuffer", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(rb_cByteBuffer, grpc_rb_byte_buffer_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(rb_cByteBuffer, "initialize", grpc_rb_byte_buffer_init, 1);
  rb_define_method(rb_cByteBuffer, "initialize_copy",
                   grpc_rb_byte_buffer_init_copy, 1);

  /* Provides a to_s method that returns the buffer value */
  rb_define_method(rb_cByteBuffer, "to_s", grpc_rb_byte_buffer_to_s, 0);

  id_source = rb_intern("__source");
  id_empty = rb_intern("");
}

VALUE grpc_rb_byte_buffer_create_with_mark(VALUE mark, grpc_byte_buffer *bb) {
  grpc_rb_byte_buffer *byte_buffer = NULL;
  if (bb == NULL) {
    return Qnil;
  }
  byte_buffer = ALLOC(grpc_rb_byte_buffer);
  byte_buffer->wrapped = bb;
  byte_buffer->mark = mark;
  return Data_Wrap_Struct(rb_cByteBuffer, grpc_rb_byte_buffer_mark,
                          grpc_rb_byte_buffer_free, byte_buffer);
}

/* Gets the wrapped byte_buffer from the ruby wrapper */
grpc_byte_buffer *grpc_rb_get_wrapped_byte_buffer(VALUE v) {
  grpc_rb_byte_buffer *wrapper = NULL;
  Data_Get_Struct(v, grpc_rb_byte_buffer, wrapper);
  return wrapper->wrapped;
}
