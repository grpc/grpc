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

#include "rb_call.h"

#include <ruby/ruby.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include "rb_byte_buffer.h"
#include "rb_completion_queue.h"
#include "rb_grpc.h"

/* grpc_rb_cCall is the Call class whose instances proxy grpc_call. */
static VALUE grpc_rb_cCall;

/* grpc_rb_eCallError is the ruby class of the exception thrown during call
   operations; */
VALUE grpc_rb_eCallError = Qnil;

/* grpc_rb_eOutOfTime is the ruby class of the exception thrown to indicate
   a timeout. */
static VALUE grpc_rb_eOutOfTime = Qnil;

/* grpc_rb_sBatchResult is struct class used to hold the results of a batch
 * call. */
static VALUE grpc_rb_sBatchResult;

/* grpc_rb_cMdAry is the MetadataArray class whose instances proxy
 * grpc_metadata_array. */
static VALUE grpc_rb_cMdAry;

/* id_cq is the name of the hidden ivar that preserves a reference to a
 * completion queue */
static ID id_cq;

/* id_flags is the name of the hidden ivar that preserves the value of
 * the flags used to create metadata from a Hash */
static ID id_flags;

/* id_input_md is the name of the hidden ivar that preserves the hash used to
 * create metadata, so that references to the strings it contains last as long
 * as the call the metadata is added to. */
static ID id_input_md;

/* id_metadata is name of the attribute used to access the metadata hash
 * received by the call and subsequently saved on it. */
static ID id_metadata;

/* id_status is name of the attribute used to access the status object
 * received by the call and subsequently saved on it. */
static ID id_status;

/* id_write_flag is name of the attribute used to access the write_flag
 * saved on the call. */
static ID id_write_flag;

/* sym_* are the symbol for attributes of grpc_rb_sBatchResult. */
static VALUE sym_send_message;
static VALUE sym_send_metadata;
static VALUE sym_send_close;
static VALUE sym_send_status;
static VALUE sym_message;
static VALUE sym_status;
static VALUE sym_cancelled;

/* hash_all_calls is a hash of Call address -> reference count that is used to
 * track the creation and destruction of rb_call instances.
 */
static VALUE hash_all_calls;

/* Destroys a Call. */
static void grpc_rb_call_destroy(void *p) {
  grpc_call *call = NULL;
  VALUE ref_count = Qnil;
  if (p == NULL) {
    return;
  };
  call = (grpc_call *)p;

  ref_count = rb_hash_aref(hash_all_calls, OFFT2NUM((VALUE)call));
  if (ref_count == Qnil) {
    return; /* No longer in the hash, so already deleted */
  } else if (NUM2UINT(ref_count) == 1) {
    rb_hash_delete(hash_all_calls, OFFT2NUM((VALUE)call));
    grpc_call_destroy(call);
  } else {
    rb_hash_aset(hash_all_calls, OFFT2NUM((VALUE)call),
                 UINT2NUM(NUM2UINT(ref_count) - 1));
  }
}

static size_t md_ary_datasize(const void *p) {
  const grpc_metadata_array *const ary = (grpc_metadata_array *)p;
  size_t i, datasize = sizeof(grpc_metadata_array);
  for (i = 0; i < ary->count; ++i) {
    const grpc_metadata *const md = &ary->metadata[i];
    datasize += strlen(md->key);
    datasize += md->value_length;
  }
  datasize += ary->capacity * sizeof(grpc_metadata);
  return datasize;
}

static const rb_data_type_t grpc_rb_md_ary_data_type = {
    "grpc_metadata_array",
    {GRPC_RB_GC_NOT_MARKED, GRPC_RB_GC_DONT_FREE, md_ary_datasize,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    /* it is unsafe to specify RUBY_TYPED_FREE_IMMEDIATELY because
     * grpc_rb_call_destroy
     * touches a hash object.
     * TODO(yugui) Directly use st_table and call the free function earlier?
     */
    0,
#endif
};

/* Describes grpc_call struct for RTypedData */
static const rb_data_type_t grpc_call_data_type = {
    "grpc_call",
    {GRPC_RB_GC_NOT_MARKED, grpc_rb_call_destroy, GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    /* it is unsafe to specify RUBY_TYPED_FREE_IMMEDIATELY because
     * grpc_rb_call_destroy
     * touches a hash object.
     * TODO(yugui) Directly use st_table and call the free function earlier?
     */
    0,
#endif
};

/* Error code details is a hash containing text strings describing errors */
VALUE rb_error_code_details;

/* Obtains the error detail string for given error code */
const char *grpc_call_error_detail_of(grpc_call_error err) {
  VALUE detail_ref = rb_hash_aref(rb_error_code_details, UINT2NUM(err));
  const char *detail = "unknown error code!";
  if (detail_ref != Qnil) {
    detail = StringValueCStr(detail_ref);
  }
  return detail;
}

/* Called by clients to cancel an RPC on the server.
   Can be called multiple times, from any thread. */
static VALUE grpc_rb_call_cancel(VALUE self) {
  grpc_call *call = NULL;
  grpc_call_error err;
  TypedData_Get_Struct(self, grpc_call, &grpc_call_data_type, call);
  err = grpc_call_cancel(call, NULL);
  if (err != GRPC_CALL_OK) {
    rb_raise(grpc_rb_eCallError, "cancel failed: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }

  return Qnil;
}

/* Called to obtain the peer that this call is connected to. */
static VALUE grpc_rb_call_get_peer(VALUE self) {
  VALUE res = Qnil;
  grpc_call *call = NULL;
  char *peer = NULL;
  TypedData_Get_Struct(self, grpc_call, &grpc_call_data_type, call);
  peer = grpc_call_get_peer(call);
  res = rb_str_new2(peer);
  gpr_free(peer);

  return res;
}

/*
  call-seq:
  status = call.status

  Gets the status object saved the call.  */
static VALUE grpc_rb_call_get_status(VALUE self) {
  return rb_ivar_get(self, id_status);
}

/*
  call-seq:
  call.status = status

  Saves a status object on the call.  */
static VALUE grpc_rb_call_set_status(VALUE self, VALUE status) {
  if (!NIL_P(status) && rb_obj_class(status) != grpc_rb_sStatus) {
    rb_raise(rb_eTypeError, "bad status: got:<%s> want: <Struct::Status>",
             rb_obj_classname(status));
    return Qnil;
  }

  return rb_ivar_set(self, id_status, status);
}

/*
  call-seq:
  metadata = call.metadata

  Gets the metadata object saved the call.  */
static VALUE grpc_rb_call_get_metadata(VALUE self) {
  return rb_ivar_get(self, id_metadata);
}

/*
  call-seq:
  call.metadata = metadata

  Saves the metadata hash on the call.  */
static VALUE grpc_rb_call_set_metadata(VALUE self, VALUE metadata) {
  if (!NIL_P(metadata) && TYPE(metadata) != T_HASH) {
    rb_raise(rb_eTypeError, "bad metadata: got:<%s> want: <Hash>",
             rb_obj_classname(metadata));
    return Qnil;
  }

  return rb_ivar_set(self, id_metadata, metadata);
}

/*
  call-seq:
  write_flag = call.write_flag

  Gets the write_flag value saved the call.  */
static VALUE grpc_rb_call_get_write_flag(VALUE self) {
  return rb_ivar_get(self, id_write_flag);
}

/*
  call-seq:
  call.write_flag = write_flag

  Saves the write_flag on the call.  */
static VALUE grpc_rb_call_set_write_flag(VALUE self, VALUE write_flag) {
  if (!NIL_P(write_flag) && TYPE(write_flag) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "bad write_flag: got:<%s> want: <Fixnum>",
             rb_obj_classname(write_flag));
    return Qnil;
  }

  return rb_ivar_set(self, id_write_flag, write_flag);
}

/* grpc_rb_md_ary_fill_hash_cb is the hash iteration callback used
   to fill grpc_metadata_array.

   it's capacity should have been computed via a prior call to
   grpc_rb_md_ary_fill_hash_cb
*/
static int grpc_rb_md_ary_fill_hash_cb(VALUE key, VALUE val, VALUE md_ary_obj) {
  grpc_metadata_array *md_ary = NULL;
  long array_length;
  long i;

  /* Construct a metadata object from key and value and add it */
  TypedData_Get_Struct(md_ary_obj, grpc_metadata_array,
                       &grpc_rb_md_ary_data_type, md_ary);

  if (TYPE(val) == T_ARRAY) {
    /* If the value is an array, add capacity for each value in the array */
    array_length = RARRAY_LEN(val);
    for (i = 0; i < array_length; i++) {
      if (TYPE(key) == T_SYMBOL) {
        md_ary->metadata[md_ary->count].key = (char *)rb_id2name(SYM2ID(key));
      } else { /* StringValueCStr does all other type exclusions for us */
        md_ary->metadata[md_ary->count].key = StringValueCStr(key);
      }
      md_ary->metadata[md_ary->count].value = RSTRING_PTR(rb_ary_entry(val, i));
      md_ary->metadata[md_ary->count].value_length =
          RSTRING_LEN(rb_ary_entry(val, i));
      md_ary->count += 1;
    }
  } else {
    if (TYPE(key) == T_SYMBOL) {
      md_ary->metadata[md_ary->count].key = (char *)rb_id2name(SYM2ID(key));
    } else { /* StringValueCStr does all other type exclusions for us */
      md_ary->metadata[md_ary->count].key = StringValueCStr(key);
    }
    md_ary->metadata[md_ary->count].value = RSTRING_PTR(val);
    md_ary->metadata[md_ary->count].value_length = RSTRING_LEN(val);
    md_ary->count += 1;
  }

  return ST_CONTINUE;
}

/* grpc_rb_md_ary_capacity_hash_cb is the hash iteration callback used
   to pre-compute the capacity a grpc_metadata_array.
*/
static int grpc_rb_md_ary_capacity_hash_cb(VALUE key, VALUE val,
                                           VALUE md_ary_obj) {
  grpc_metadata_array *md_ary = NULL;

  (void)key;

  /* Construct a metadata object from key and value and add it */
  TypedData_Get_Struct(md_ary_obj, grpc_metadata_array,
                       &grpc_rb_md_ary_data_type, md_ary);

  if (TYPE(val) == T_ARRAY) {
    /* If the value is an array, add capacity for each value in the array */
    md_ary->capacity += RARRAY_LEN(val);
  } else {
    md_ary->capacity += 1;
  }
  return ST_CONTINUE;
}

/* grpc_rb_md_ary_convert converts a ruby metadata hash into
   a grpc_metadata_array.
*/
static void grpc_rb_md_ary_convert(VALUE md_ary_hash,
                                   grpc_metadata_array *md_ary) {
  VALUE md_ary_obj = Qnil;
  if (md_ary_hash == Qnil) {
    return; /* Do nothing if the expected has value is nil */
  }
  if (TYPE(md_ary_hash) != T_HASH) {
    rb_raise(rb_eTypeError, "md_ary_convert: got <%s>, want <Hash>",
             rb_obj_classname(md_ary_hash));
    return;
  }

  /* Initialize the array, compute it's capacity, then fill it. */
  grpc_metadata_array_init(md_ary);
  md_ary_obj =
      TypedData_Wrap_Struct(grpc_rb_cMdAry, &grpc_rb_md_ary_data_type, md_ary);
  rb_hash_foreach(md_ary_hash, grpc_rb_md_ary_capacity_hash_cb, md_ary_obj);
  md_ary->metadata = gpr_malloc(md_ary->capacity * sizeof(grpc_metadata));
  rb_hash_foreach(md_ary_hash, grpc_rb_md_ary_fill_hash_cb, md_ary_obj);
}

/* Converts a metadata array to a hash. */
VALUE grpc_rb_md_ary_to_h(grpc_metadata_array *md_ary) {
  VALUE key = Qnil;
  VALUE new_ary = Qnil;
  VALUE value = Qnil;
  VALUE result = rb_hash_new();
  size_t i;

  for (i = 0; i < md_ary->count; i++) {
    key = rb_str_new2(md_ary->metadata[i].key);
    value = rb_hash_aref(result, key);
    if (value == Qnil) {
      value = rb_str_new(md_ary->metadata[i].value,
                         md_ary->metadata[i].value_length);
      rb_hash_aset(result, key, value);
    } else if (TYPE(value) == T_ARRAY) {
      /* Add the string to the returned array */
      rb_ary_push(value, rb_str_new(md_ary->metadata[i].value,
                                    md_ary->metadata[i].value_length));
    } else {
      /* Add the current value with this key and the new one to an array */
      new_ary = rb_ary_new();
      rb_ary_push(new_ary, value);
      rb_ary_push(new_ary, rb_str_new(md_ary->metadata[i].value,
                                      md_ary->metadata[i].value_length));
      rb_hash_aset(result, key, new_ary);
    }
  }
  return result;
}

/* grpc_rb_call_check_op_keys_hash_cb is a hash iteration func that checks
   each key of an ops hash is valid.
*/
static int grpc_rb_call_check_op_keys_hash_cb(VALUE key, VALUE val,
                                              VALUE ops_ary) {
  (void)val;
  /* Update the capacity; the value is an array, add capacity for each value in
   * the array */
  if (TYPE(key) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "invalid operation : got <%s>, want <Fixnum>",
             rb_obj_classname(key));
    return ST_STOP;
  }
  switch (NUM2INT(key)) {
    case GRPC_OP_SEND_INITIAL_METADATA:
    case GRPC_OP_SEND_MESSAGE:
    case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
    case GRPC_OP_SEND_STATUS_FROM_SERVER:
    case GRPC_OP_RECV_INITIAL_METADATA:
    case GRPC_OP_RECV_MESSAGE:
    case GRPC_OP_RECV_STATUS_ON_CLIENT:
    case GRPC_OP_RECV_CLOSE_ON_SERVER:
      rb_ary_push(ops_ary, key);
      return ST_CONTINUE;
    default:
      rb_raise(rb_eTypeError, "invalid operation : bad value %d", NUM2INT(key));
  };
  return ST_STOP;
}

/* grpc_rb_op_update_status_from_server adds the values in a ruby status
   struct to the 'send_status_from_server' portion of an op.
*/
static void grpc_rb_op_update_status_from_server(grpc_op *op,
                                                 grpc_metadata_array *md_ary,
                                                 VALUE status) {
  VALUE code = rb_struct_aref(status, sym_code);
  VALUE details = rb_struct_aref(status, sym_details);
  VALUE metadata_hash = rb_struct_aref(status, sym_metadata);

  /* TODO: add check to ensure status is the correct struct type */
  if (TYPE(code) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "invalid code : got <%s>, want <Fixnum>",
             rb_obj_classname(code));
    return;
  }
  if (TYPE(details) != T_STRING) {
    rb_raise(rb_eTypeError, "invalid details : got <%s>, want <String>",
             rb_obj_classname(code));
    return;
  }
  op->data.send_status_from_server.status = NUM2INT(code);
  op->data.send_status_from_server.status_details = StringValueCStr(details);
  grpc_rb_md_ary_convert(metadata_hash, md_ary);
  op->data.send_status_from_server.trailing_metadata_count = md_ary->count;
  op->data.send_status_from_server.trailing_metadata = md_ary->metadata;
}

/* run_batch_stack holds various values used by the
 * grpc_rb_call_run_batch function */
typedef struct run_batch_stack {
  /* The batch ops */
  grpc_op ops[8]; /* 8 is the maximum number of operations */
  size_t op_num;  /* tracks the last added operation */

  /* Data being sent */
  grpc_metadata_array send_metadata;
  grpc_metadata_array send_trailing_metadata;

  /* Data being received */
  grpc_byte_buffer *recv_message;
  grpc_metadata_array recv_metadata;
  grpc_metadata_array recv_trailing_metadata;
  int recv_cancelled;
  grpc_status_code recv_status;
  char *recv_status_details;
  size_t recv_status_details_capacity;
  uint write_flag;
} run_batch_stack;

/* grpc_run_batch_stack_init ensures the run_batch_stack is properly
 * initialized */
static void grpc_run_batch_stack_init(run_batch_stack *st, uint write_flag) {
  MEMZERO(st, run_batch_stack, 1);
  grpc_metadata_array_init(&st->send_metadata);
  grpc_metadata_array_init(&st->send_trailing_metadata);
  grpc_metadata_array_init(&st->recv_metadata);
  grpc_metadata_array_init(&st->recv_trailing_metadata);
  st->op_num = 0;
  st->write_flag = write_flag;
}

/* grpc_run_batch_stack_cleanup ensures the run_batch_stack is properly
 * cleaned up */
static void grpc_run_batch_stack_cleanup(run_batch_stack *st) {
  grpc_metadata_array_destroy(&st->send_metadata);
  grpc_metadata_array_destroy(&st->send_trailing_metadata);
  grpc_metadata_array_destroy(&st->recv_metadata);
  grpc_metadata_array_destroy(&st->recv_trailing_metadata);
  if (st->recv_status_details != NULL) {
    gpr_free(st->recv_status_details);
  }
}

/* grpc_run_batch_stack_fill_ops fills the run_batch_stack ops array from
 * ops_hash */
static void grpc_run_batch_stack_fill_ops(run_batch_stack *st, VALUE ops_hash) {
  VALUE this_op = Qnil;
  VALUE this_value = Qnil;
  VALUE ops_ary = rb_ary_new();
  size_t i = 0;

  /* Create a ruby array with just the operation keys */
  rb_hash_foreach(ops_hash, grpc_rb_call_check_op_keys_hash_cb, ops_ary);

  /* Fill the ops array */
  for (i = 0; i < (size_t)RARRAY_LEN(ops_ary); i++) {
    this_op = rb_ary_entry(ops_ary, i);
    this_value = rb_hash_aref(ops_hash, this_op);
    st->ops[st->op_num].flags = 0;
    switch (NUM2INT(this_op)) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        /* N.B. later there is no need to explicitly delete the metadata keys
         * and values, they are references to data in ruby objects. */
        grpc_rb_md_ary_convert(this_value, &st->send_metadata);
        st->ops[st->op_num].data.send_initial_metadata.count =
            st->send_metadata.count;
        st->ops[st->op_num].data.send_initial_metadata.metadata =
            st->send_metadata.metadata;
        break;
      case GRPC_OP_SEND_MESSAGE:
        st->ops[st->op_num].data.send_message = grpc_rb_s_to_byte_buffer(
            RSTRING_PTR(this_value), RSTRING_LEN(this_value));
        st->ops[st->op_num].flags = st->write_flag;
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        /* N.B. later there is no need to explicitly delete the metadata keys
         * and values, they are references to data in ruby objects. */
        grpc_rb_op_update_status_from_server(
            &st->ops[st->op_num], &st->send_trailing_metadata, this_value);
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        st->ops[st->op_num].data.recv_initial_metadata = &st->recv_metadata;
        break;
      case GRPC_OP_RECV_MESSAGE:
        st->ops[st->op_num].data.recv_message = &st->recv_message;
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        st->ops[st->op_num].data.recv_status_on_client.trailing_metadata =
            &st->recv_trailing_metadata;
        st->ops[st->op_num].data.recv_status_on_client.status =
            &st->recv_status;
        st->ops[st->op_num].data.recv_status_on_client.status_details =
            &st->recv_status_details;
        st->ops[st->op_num].data.recv_status_on_client.status_details_capacity =
            &st->recv_status_details_capacity;
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        st->ops[st->op_num].data.recv_close_on_server.cancelled =
            &st->recv_cancelled;
        break;
      default:
        grpc_run_batch_stack_cleanup(st);
        rb_raise(rb_eTypeError, "invalid operation : bad value %d",
                 NUM2INT(this_op));
    };
    st->ops[st->op_num].op = (grpc_op_type)NUM2INT(this_op);
    st->ops[st->op_num].reserved = NULL;
    st->op_num++;
  }
}

/* grpc_run_batch_stack_build_result fills constructs a ruby BatchResult struct
   after the results have run */
static VALUE grpc_run_batch_stack_build_result(run_batch_stack *st) {
  size_t i = 0;
  VALUE result = rb_struct_new(grpc_rb_sBatchResult, Qnil, Qnil, Qnil, Qnil,
                               Qnil, Qnil, Qnil, Qnil, NULL);
  for (i = 0; i < st->op_num; i++) {
    switch (st->ops[i].op) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        rb_struct_aset(result, sym_send_metadata, Qtrue);
        break;
      case GRPC_OP_SEND_MESSAGE:
        rb_struct_aset(result, sym_send_message, Qtrue);
        grpc_byte_buffer_destroy(st->ops[i].data.send_message);
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        rb_struct_aset(result, sym_send_close, Qtrue);
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        rb_struct_aset(result, sym_send_status, Qtrue);
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        rb_struct_aset(result, sym_metadata,
                       grpc_rb_md_ary_to_h(&st->recv_metadata));
      case GRPC_OP_RECV_MESSAGE:
        rb_struct_aset(result, sym_message,
                       grpc_rb_byte_buffer_to_s(st->recv_message));
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        rb_struct_aset(
            result, sym_status,
            rb_struct_new(grpc_rb_sStatus, UINT2NUM(st->recv_status),
                          (st->recv_status_details == NULL
                               ? Qnil
                               : rb_str_new2(st->recv_status_details)),
                          grpc_rb_md_ary_to_h(&st->recv_trailing_metadata),
                          NULL));
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        rb_struct_aset(result, sym_send_close, Qtrue);
        break;
      default:
        break;
    }
  }
  return result;
}

/* call-seq:
   cq = CompletionQueue.new
   ops = {
     GRPC::Core::CallOps::SEND_INITIAL_METADATA => <op_value>,
     GRPC::Core::CallOps::SEND_MESSAGE => <op_value>,
     ...
   }
   tag = Object.new
   timeout = 10
   call.start_batch(cqueue, tag, timeout, ops)

   Start a batch of operations defined in the array ops; when complete, post a
   completion of type 'tag' to the completion queue bound to the call.

   Also waits for the batch to complete, until timeout is reached.
   The order of ops specified in the batch has no significance.
   Only one operation of each type can be active at once in any given
   batch */
static VALUE grpc_rb_call_run_batch(VALUE self, VALUE cqueue, VALUE tag,
                                    VALUE timeout, VALUE ops_hash) {
  run_batch_stack st;
  grpc_call *call = NULL;
  grpc_event ev;
  grpc_call_error err;
  VALUE result = Qnil;
  VALUE rb_write_flag = rb_ivar_get(self, id_write_flag);
  uint write_flag = 0;
  TypedData_Get_Struct(self, grpc_call, &grpc_call_data_type, call);

  /* Validate the ops args, adding them to a ruby array */
  if (TYPE(ops_hash) != T_HASH) {
    rb_raise(rb_eTypeError, "call#run_batch: ops hash should be a hash");
    return Qnil;
  }
  if (rb_write_flag != Qnil) {
    write_flag = NUM2UINT(rb_write_flag);
  }
  grpc_run_batch_stack_init(&st, write_flag);
  grpc_run_batch_stack_fill_ops(&st, ops_hash);

  /* call grpc_call_start_batch, then wait for it to complete using
   * pluck_event */
  err = grpc_call_start_batch(call, st.ops, st.op_num, ROBJECT(tag), NULL);
  if (err != GRPC_CALL_OK) {
    grpc_run_batch_stack_cleanup(&st);
    rb_raise(grpc_rb_eCallError,
             "grpc_call_start_batch failed with %s (code=%d)",
             grpc_call_error_detail_of(err), err);
    return Qnil;
  }
  ev = grpc_rb_completion_queue_pluck_event(cqueue, tag, timeout);
  if (ev.type == GRPC_QUEUE_TIMEOUT) {
    grpc_run_batch_stack_cleanup(&st);
    rb_raise(grpc_rb_eOutOfTime, "grpc_call_start_batch timed out");
    return Qnil;
  }

  /* Build and return the BatchResult struct result,
     if there is an error, it's reflected in the status */
  result = grpc_run_batch_stack_build_result(&st);
  grpc_run_batch_stack_cleanup(&st);
  return result;
}

static void Init_grpc_write_flags() {
  /* Constants representing the write flags in grpc.h */
  VALUE grpc_rb_mWriteFlags =
      rb_define_module_under(grpc_rb_mGrpcCore, "WriteFlags");
  rb_define_const(grpc_rb_mWriteFlags, "BUFFER_HINT",
                  UINT2NUM(GRPC_WRITE_BUFFER_HINT));
  rb_define_const(grpc_rb_mWriteFlags, "NO_COMPRESS",
                  UINT2NUM(GRPC_WRITE_NO_COMPRESS));
}

static void Init_grpc_error_codes() {
  /* Constants representing the error codes of grpc_call_error in grpc.h */
  VALUE grpc_rb_mRpcErrors =
      rb_define_module_under(grpc_rb_mGrpcCore, "RpcErrors");
  rb_define_const(grpc_rb_mRpcErrors, "OK", UINT2NUM(GRPC_CALL_OK));
  rb_define_const(grpc_rb_mRpcErrors, "ERROR", UINT2NUM(GRPC_CALL_ERROR));
  rb_define_const(grpc_rb_mRpcErrors, "NOT_ON_SERVER",
                  UINT2NUM(GRPC_CALL_ERROR_NOT_ON_SERVER));
  rb_define_const(grpc_rb_mRpcErrors, "NOT_ON_CLIENT",
                  UINT2NUM(GRPC_CALL_ERROR_NOT_ON_CLIENT));
  rb_define_const(grpc_rb_mRpcErrors, "ALREADY_ACCEPTED",
                  UINT2NUM(GRPC_CALL_ERROR_ALREADY_ACCEPTED));
  rb_define_const(grpc_rb_mRpcErrors, "ALREADY_INVOKED",
                  UINT2NUM(GRPC_CALL_ERROR_ALREADY_INVOKED));
  rb_define_const(grpc_rb_mRpcErrors, "NOT_INVOKED",
                  UINT2NUM(GRPC_CALL_ERROR_NOT_INVOKED));
  rb_define_const(grpc_rb_mRpcErrors, "ALREADY_FINISHED",
                  UINT2NUM(GRPC_CALL_ERROR_ALREADY_FINISHED));
  rb_define_const(grpc_rb_mRpcErrors, "TOO_MANY_OPERATIONS",
                  UINT2NUM(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS));
  rb_define_const(grpc_rb_mRpcErrors, "INVALID_FLAGS",
                  UINT2NUM(GRPC_CALL_ERROR_INVALID_FLAGS));

  /* Add the detail strings to a Hash */
  rb_error_code_details = rb_hash_new();
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_OK),
               rb_str_new2("ok"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR),
               rb_str_new2("unknown error"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR_NOT_ON_SERVER),
               rb_str_new2("not available on a server"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR_NOT_ON_CLIENT),
               rb_str_new2("not available on a client"));
  rb_hash_aset(rb_error_code_details,
               UINT2NUM(GRPC_CALL_ERROR_ALREADY_ACCEPTED),
               rb_str_new2("call is already accepted"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR_ALREADY_INVOKED),
               rb_str_new2("call is already invoked"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR_NOT_INVOKED),
               rb_str_new2("call is not yet invoked"));
  rb_hash_aset(rb_error_code_details,
               UINT2NUM(GRPC_CALL_ERROR_ALREADY_FINISHED),
               rb_str_new2("call is already finished"));
  rb_hash_aset(rb_error_code_details,
               UINT2NUM(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS),
               rb_str_new2("outstanding read or write present"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR_INVALID_FLAGS),
               rb_str_new2("a bad flag was given"));
  rb_define_const(grpc_rb_mRpcErrors, "ErrorMessages", rb_error_code_details);
  rb_obj_freeze(rb_error_code_details);
}

static void Init_grpc_op_codes() {
  /* Constants representing operation type codes in grpc.h */
  VALUE grpc_rb_mCallOps = rb_define_module_under(grpc_rb_mGrpcCore, "CallOps");
  rb_define_const(grpc_rb_mCallOps, "SEND_INITIAL_METADATA",
                  UINT2NUM(GRPC_OP_SEND_INITIAL_METADATA));
  rb_define_const(grpc_rb_mCallOps, "SEND_MESSAGE",
                  UINT2NUM(GRPC_OP_SEND_MESSAGE));
  rb_define_const(grpc_rb_mCallOps, "SEND_CLOSE_FROM_CLIENT",
                  UINT2NUM(GRPC_OP_SEND_CLOSE_FROM_CLIENT));
  rb_define_const(grpc_rb_mCallOps, "SEND_STATUS_FROM_SERVER",
                  UINT2NUM(GRPC_OP_SEND_STATUS_FROM_SERVER));
  rb_define_const(grpc_rb_mCallOps, "RECV_INITIAL_METADATA",
                  UINT2NUM(GRPC_OP_RECV_INITIAL_METADATA));
  rb_define_const(grpc_rb_mCallOps, "RECV_MESSAGE",
                  UINT2NUM(GRPC_OP_RECV_MESSAGE));
  rb_define_const(grpc_rb_mCallOps, "RECV_STATUS_ON_CLIENT",
                  UINT2NUM(GRPC_OP_RECV_STATUS_ON_CLIENT));
  rb_define_const(grpc_rb_mCallOps, "RECV_CLOSE_ON_SERVER",
                  UINT2NUM(GRPC_OP_RECV_CLOSE_ON_SERVER));
}

void Init_grpc_call() {
  /* CallError inherits from Exception to signal that it is non-recoverable */
  grpc_rb_eCallError =
      rb_define_class_under(grpc_rb_mGrpcCore, "CallError", rb_eException);
  grpc_rb_eOutOfTime =
      rb_define_class_under(grpc_rb_mGrpcCore, "OutOfTime", rb_eException);
  grpc_rb_cCall = rb_define_class_under(grpc_rb_mGrpcCore, "Call", rb_cObject);
  grpc_rb_cMdAry =
      rb_define_class_under(grpc_rb_mGrpcCore, "MetadataArray", rb_cObject);

  /* Prevent allocation or inialization of the Call class */
  rb_define_alloc_func(grpc_rb_cCall, grpc_rb_cannot_alloc);
  rb_define_method(grpc_rb_cCall, "initialize", grpc_rb_cannot_init, 0);
  rb_define_method(grpc_rb_cCall, "initialize_copy", grpc_rb_cannot_init_copy,
                   1);

  /* Add ruby analogues of the Call methods. */
  rb_define_method(grpc_rb_cCall, "run_batch", grpc_rb_call_run_batch, 4);
  rb_define_method(grpc_rb_cCall, "cancel", grpc_rb_call_cancel, 0);
  rb_define_method(grpc_rb_cCall, "peer", grpc_rb_call_get_peer, 0);
  rb_define_method(grpc_rb_cCall, "status", grpc_rb_call_get_status, 0);
  rb_define_method(grpc_rb_cCall, "status=", grpc_rb_call_set_status, 1);
  rb_define_method(grpc_rb_cCall, "metadata", grpc_rb_call_get_metadata, 0);
  rb_define_method(grpc_rb_cCall, "metadata=", grpc_rb_call_set_metadata, 1);
  rb_define_method(grpc_rb_cCall, "write_flag", grpc_rb_call_get_write_flag, 0);
  rb_define_method(grpc_rb_cCall, "write_flag=", grpc_rb_call_set_write_flag,
                   1);

  /* Ids used to support call attributes */
  id_metadata = rb_intern("metadata");
  id_status = rb_intern("status");
  id_write_flag = rb_intern("write_flag");

  /* Ids used by the c wrapping internals. */
  id_cq = rb_intern("__cq");
  id_flags = rb_intern("__flags");
  id_input_md = rb_intern("__input_md");

  /* Ids used in constructing the batch result. */
  sym_send_message = ID2SYM(rb_intern("send_message"));
  sym_send_metadata = ID2SYM(rb_intern("send_metadata"));
  sym_send_close = ID2SYM(rb_intern("send_close"));
  sym_send_status = ID2SYM(rb_intern("send_status"));
  sym_message = ID2SYM(rb_intern("message"));
  sym_status = ID2SYM(rb_intern("status"));
  sym_cancelled = ID2SYM(rb_intern("cancelled"));

  /* The Struct used to return the run_batch result. */
  grpc_rb_sBatchResult = rb_struct_define(
      "BatchResult", "send_message", "send_metadata", "send_close",
      "send_status", "message", "metadata", "status", "cancelled", NULL);

  /* The hash for reference counting calls, to ensure they can't be destroyed
   * more than once */
  hash_all_calls = rb_hash_new();
  rb_define_const(grpc_rb_cCall, "INTERNAL_ALL_CALLs", hash_all_calls);

  Init_grpc_error_codes();
  Init_grpc_op_codes();
  Init_grpc_write_flags();
}

/* Gets the call from the ruby object */
grpc_call *grpc_rb_get_wrapped_call(VALUE v) {
  grpc_call *c = NULL;
  TypedData_Get_Struct(v, grpc_call, &grpc_call_data_type, c);
  return c;
}

/* Obtains the wrapped object for a given call */
VALUE grpc_rb_wrap_call(grpc_call *c) {
  VALUE obj = Qnil;
  if (c == NULL) {
    return Qnil;
  }
  obj = rb_hash_aref(hash_all_calls, OFFT2NUM((VALUE)c));
  if (obj == Qnil) { /* Not in the hash add it */
    rb_hash_aset(hash_all_calls, OFFT2NUM((VALUE)c), UINT2NUM(1));
  } else {
    rb_hash_aset(hash_all_calls, OFFT2NUM((VALUE)c),
                 UINT2NUM(NUM2UINT(obj) + 1));
  }
  return TypedData_Wrap_Struct(grpc_rb_cCall, &grpc_call_data_type, c);
}
