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

#include "rb_call.h"

#include <grpc/grpc.h>
#include <grpc/impl/codegen/compression_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "rb_byte_buffer.h"
#include "rb_call_credentials.h"
#include "rb_completion_queue.h"
#include "rb_grpc.h"
#include "rb_grpc_imports.generated.h"

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
VALUE grpc_rb_cMdAry;

/* id_credentials is the name of the hidden ivar that preserves the value
 * of the credentials added to the call */
static ID id_credentials;

/* id_metadata is name of the attribute used to access the metadata hash
 * received by the call and subsequently saved on it. */
static ID id_metadata;

/* id_trailing_metadata is the name of the attribute used to access the trailing
 * metadata hash received by the call and subsequently saved on it. */
static ID id_trailing_metadata;

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

typedef struct grpc_rb_call {
  grpc_call* wrapped;
  grpc_completion_queue* queue;
} grpc_rb_call;

static void destroy_call(grpc_rb_call* call) {
  /* Ensure that we only try to destroy the call once */
  if (call->wrapped != NULL) {
    grpc_call_unref(call->wrapped);
    call->wrapped = NULL;
    grpc_rb_completion_queue_destroy(call->queue);
    call->queue = NULL;
  }
}

/* Destroys a Call. */
static void grpc_rb_call_destroy(void* p) {
  if (p == NULL) {
    return;
  }
  destroy_call((grpc_rb_call*)p);
  xfree(p);
}

const rb_data_type_t grpc_rb_md_ary_data_type = {
    "grpc_metadata_array",
    {GRPC_RB_GC_NOT_MARKED,
     GRPC_RB_GC_DONT_FREE,
     GRPC_RB_MEMSIZE_UNAVAILABLE,
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
static const rb_data_type_t grpc_call_data_type = {"grpc_call",
                                                   {GRPC_RB_GC_NOT_MARKED,
                                                    grpc_rb_call_destroy,
                                                    GRPC_RB_MEMSIZE_UNAVAILABLE,
                                                    {NULL, NULL}},
                                                   NULL,
                                                   NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
                                                   RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Error code details is a hash containing text strings describing errors */
VALUE rb_error_code_details;

/* Obtains the error detail string for given error code */
const char* grpc_call_error_detail_of(grpc_call_error err) {
  VALUE detail_ref = rb_hash_aref(rb_error_code_details, UINT2NUM(err));
  const char* detail = "unknown error code!";
  if (detail_ref != Qnil) {
    detail = StringValueCStr(detail_ref);
  }
  return detail;
}

/* Called by clients to cancel an RPC on the server.
   Can be called multiple times, from any thread. */
static VALUE grpc_rb_call_cancel(VALUE self) {
  grpc_rb_call* call = NULL;
  grpc_call_error err;
  if (RTYPEDDATA_DATA(self) == NULL) {
    // This call has been closed
    return Qnil;
  }

  TypedData_Get_Struct(self, grpc_rb_call, &grpc_call_data_type, call);
  err = grpc_call_cancel(call->wrapped, NULL);
  if (err != GRPC_CALL_OK) {
    rb_raise(grpc_rb_eCallError, "cancel failed: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }

  return Qnil;
}

/* TODO: expose this as part of the surface API if needed.
 * This is meant for internal usage by the "write thread" of grpc-ruby
 * client-side bidi calls. It provides a way for the background write-thread
 * to propagate failures to the main read-thread and give the user an error
 * message. */
static VALUE grpc_rb_call_cancel_with_status(VALUE self, VALUE status_code,
                                             VALUE details) {
  grpc_rb_call* call = NULL;
  grpc_call_error err;
  if (RTYPEDDATA_DATA(self) == NULL) {
    // This call has been closed
    return Qnil;
  }

  if (TYPE(details) != T_STRING || TYPE(status_code) != T_FIXNUM) {
    rb_raise(rb_eTypeError,
             "Bad parameter type error for cancel with status. Want Fixnum, "
             "String.");
    return Qnil;
  }

  TypedData_Get_Struct(self, grpc_rb_call, &grpc_call_data_type, call);
  err = grpc_call_cancel_with_status(call->wrapped, NUM2LONG(status_code),
                                     StringValueCStr(details), NULL);
  if (err != GRPC_CALL_OK) {
    rb_raise(grpc_rb_eCallError, "cancel with status failed: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }

  return Qnil;
}

/* Releases the c-level resources associated with a call
   Once a call has been closed, no further requests can be
   processed.
*/
static VALUE grpc_rb_call_close(VALUE self) {
  grpc_rb_call* call = NULL;
  TypedData_Get_Struct(self, grpc_rb_call, &grpc_call_data_type, call);
  if (call != NULL) {
    destroy_call(call);
    xfree(RTYPEDDATA_DATA(self));
    RTYPEDDATA_DATA(self) = NULL;
  }
  return Qnil;
}

/* Called to obtain the peer that this call is connected to. */
static VALUE grpc_rb_call_get_peer(VALUE self) {
  VALUE res = Qnil;
  grpc_rb_call* call = NULL;
  char* peer = NULL;
  if (RTYPEDDATA_DATA(self) == NULL) {
    rb_raise(grpc_rb_eCallError, "Cannot get peer value on closed call");
    return Qnil;
  }
  TypedData_Get_Struct(self, grpc_rb_call, &grpc_call_data_type, call);
  peer = grpc_call_get_peer(call->wrapped);
  res = rb_str_new2(peer);
  gpr_free(peer);

  return res;
}

/* Called to obtain the x509 cert of an authenticated peer. */
static VALUE grpc_rb_call_get_peer_cert(VALUE self) {
  grpc_rb_call* call = NULL;
  VALUE res = Qnil;
  grpc_auth_context* ctx = NULL;
  if (RTYPEDDATA_DATA(self) == NULL) {
    rb_raise(grpc_rb_eCallError, "Cannot get peer cert on closed call");
    return Qnil;
  }
  TypedData_Get_Struct(self, grpc_rb_call, &grpc_call_data_type, call);

  ctx = grpc_call_auth_context(call->wrapped);

  if (!ctx || !grpc_auth_context_peer_is_authenticated(ctx)) {
    return Qnil;
  }

  {
    grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
        ctx, GRPC_X509_PEM_CERT_PROPERTY_NAME);
    const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
    if (prop == NULL) {
      return Qnil;
    }

    res = rb_str_new2(prop->value);
  }

  grpc_auth_context_release(ctx);

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
  trailing_metadata = call.trailing_metadata

  Gets the trailing metadata object saved on the call */
static VALUE grpc_rb_call_get_trailing_metadata(VALUE self) {
  return rb_ivar_get(self, id_trailing_metadata);
}

/*
  call-seq:
  call.trailing_metadata = trailing_metadata

  Saves the trailing metadata hash on the call. */
static VALUE grpc_rb_call_set_trailing_metadata(VALUE self, VALUE metadata) {
  if (!NIL_P(metadata) && TYPE(metadata) != T_HASH) {
    rb_raise(rb_eTypeError, "bad metadata: got:<%s> want: <Hash>",
             rb_obj_classname(metadata));
    return Qnil;
  }

  return rb_ivar_set(self, id_trailing_metadata, metadata);
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

/*
  call-seq:
  call.set_credentials call_credentials

  Sets credentials on a call */
static VALUE grpc_rb_call_set_credentials(VALUE self, VALUE credentials) {
  grpc_rb_call* call = NULL;
  grpc_call_credentials* creds;
  grpc_call_error err;
  if (RTYPEDDATA_DATA(self) == NULL) {
    rb_raise(grpc_rb_eCallError, "Cannot set credentials of closed call");
    return Qnil;
  }
  TypedData_Get_Struct(self, grpc_rb_call, &grpc_call_data_type, call);
  creds = grpc_rb_get_wrapped_call_credentials(credentials);
  err = grpc_call_set_credentials(call->wrapped, creds);
  if (err != GRPC_CALL_OK) {
    rb_raise(grpc_rb_eCallError,
             "grpc_call_set_credentials failed with %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }
  /* We need the credentials to be alive for as long as the call is alive,
     but we don't care about destruction order. */
  rb_ivar_set(self, id_credentials, credentials);
  return Qnil;
}

/* grpc_rb_md_ary_fill_hash_cb is the hash iteration callback used
   to fill grpc_metadata_array.

   it's capacity should have been computed via a prior call to
   grpc_rb_md_ary_capacity_hash_cb
*/
static int grpc_rb_md_ary_fill_hash_cb(VALUE key, VALUE val, VALUE md_ary_obj) {
  grpc_metadata_array* md_ary = NULL;
  long array_length;
  long i;
  grpc_slice key_slice;
  grpc_slice value_slice;
  char* tmp_str = NULL;

  if (TYPE(key) == T_SYMBOL) {
    key_slice = grpc_slice_from_static_string(rb_id2name(SYM2ID(key)));
  } else if (TYPE(key) == T_STRING) {
    key_slice =
        grpc_slice_from_copied_buffer(RSTRING_PTR(key), RSTRING_LEN(key));
  } else {
    rb_raise(rb_eTypeError,
             "grpc_rb_md_ary_fill_hash_cb: bad type for key parameter");
    return ST_STOP;
  }

  if (!grpc_header_key_is_legal(key_slice)) {
    tmp_str = grpc_slice_to_c_string(key_slice);
    rb_raise(rb_eArgError,
             "'%s' is an invalid header key, must match [a-z0-9-_.]+", tmp_str);
    return ST_STOP;
  }

  /* Construct a metadata object from key and value and add it */
  TypedData_Get_Struct(md_ary_obj, grpc_metadata_array,
                       &grpc_rb_md_ary_data_type, md_ary);

  if (TYPE(val) == T_ARRAY) {
    array_length = RARRAY_LEN(val);
    /* If the value is an array, add capacity for each value in the array */
    for (i = 0; i < array_length; i++) {
      value_slice = grpc_slice_from_copied_buffer(
          RSTRING_PTR(rb_ary_entry(val, i)), RSTRING_LEN(rb_ary_entry(val, i)));
      if (!grpc_is_binary_header(key_slice) &&
          !grpc_header_nonbin_value_is_legal(value_slice)) {
        // The value has invalid characters
        tmp_str = grpc_slice_to_c_string(value_slice);
        rb_raise(rb_eArgError, "Header value '%s' has invalid characters",
                 tmp_str);
        return ST_STOP;
      }
      GRPC_RUBY_ASSERT(md_ary->count < md_ary->capacity);
      md_ary->metadata[md_ary->count].key = key_slice;
      md_ary->metadata[md_ary->count].value = value_slice;
      md_ary->count += 1;
    }
  } else if (TYPE(val) == T_STRING) {
    value_slice =
        grpc_slice_from_copied_buffer(RSTRING_PTR(val), RSTRING_LEN(val));
    if (!grpc_is_binary_header(key_slice) &&
        !grpc_header_nonbin_value_is_legal(value_slice)) {
      // The value has invalid characters
      tmp_str = grpc_slice_to_c_string(value_slice);
      rb_raise(rb_eArgError, "Header value '%s' has invalid characters",
               tmp_str);
      return ST_STOP;
    }
    GRPC_RUBY_ASSERT(md_ary->count < md_ary->capacity);
    md_ary->metadata[md_ary->count].key = key_slice;
    md_ary->metadata[md_ary->count].value = value_slice;
    md_ary->count += 1;
  } else {
    rb_raise(rb_eArgError, "Header values must be of type string or array");
    return ST_STOP;
  }
  return ST_CONTINUE;
}

/* grpc_rb_md_ary_capacity_hash_cb is the hash iteration callback used
   to pre-compute the capacity a grpc_metadata_array.
*/
static int grpc_rb_md_ary_capacity_hash_cb(VALUE key, VALUE val,
                                           VALUE md_ary_obj) {
  grpc_metadata_array* md_ary = NULL;

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
   Note that this function may throw exceptions.
*/
void grpc_rb_md_ary_convert(VALUE md_ary_hash, grpc_metadata_array* md_ary) {
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
  md_ary->metadata = gpr_zalloc(md_ary->capacity * sizeof(grpc_metadata));
  rb_hash_foreach(md_ary_hash, grpc_rb_md_ary_fill_hash_cb, md_ary_obj);
}

/* Converts a metadata array to a hash. */
VALUE grpc_rb_md_ary_to_h(grpc_metadata_array* md_ary) {
  VALUE key = Qnil;
  VALUE new_ary = Qnil;
  VALUE value = Qnil;
  VALUE result = rb_hash_new();
  size_t i;

  for (i = 0; i < md_ary->count; i++) {
    key = grpc_rb_slice_to_ruby_string(md_ary->metadata[i].key);
    value = rb_hash_aref(result, key);
    if (value == Qnil) {
      value = grpc_rb_slice_to_ruby_string(md_ary->metadata[i].value);
      rb_hash_aset(result, key, value);
    } else if (TYPE(value) == T_ARRAY) {
      /* Add the string to the returned array */
      rb_ary_push(value,
                  grpc_rb_slice_to_ruby_string(md_ary->metadata[i].value));
    } else {
      /* Add the current value with this key and the new one to an array */
      new_ary = rb_ary_new();
      rb_ary_push(new_ary, value);
      rb_ary_push(new_ary,
                  grpc_rb_slice_to_ruby_string(md_ary->metadata[i].value));
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
static void grpc_rb_op_update_status_from_server(
    grpc_op* op, grpc_metadata_array* md_ary, grpc_slice* send_status_details,
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

  *send_status_details =
      grpc_slice_from_copied_buffer(RSTRING_PTR(details), RSTRING_LEN(details));

  op->data.send_status_from_server.status = NUM2INT(code);
  op->data.send_status_from_server.status_details = send_status_details;
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
  grpc_byte_buffer* recv_message;
  grpc_metadata_array recv_metadata;
  grpc_metadata_array recv_trailing_metadata;
  int recv_cancelled;
  grpc_status_code recv_status;
  grpc_slice recv_status_details;
  const char* recv_status_debug_error_string;
  unsigned write_flag;
  grpc_slice send_status_details;
} run_batch_stack;

/* grpc_run_batch_stack_init ensures the run_batch_stack is properly
 * initialized */
static void grpc_run_batch_stack_init(run_batch_stack* st,
                                      unsigned write_flag) {
  MEMZERO(st, run_batch_stack, 1);
  grpc_metadata_array_init(&st->send_metadata);
  grpc_metadata_array_init(&st->send_trailing_metadata);
  grpc_metadata_array_init(&st->recv_metadata);
  grpc_metadata_array_init(&st->recv_trailing_metadata);
  st->op_num = 0;
  st->write_flag = write_flag;
}

void grpc_rb_metadata_array_destroy_including_entries(
    grpc_metadata_array* array) {
  size_t i;
  if (array->metadata) {
    for (i = 0; i < array->count; i++) {
      grpc_slice_unref(array->metadata[i].key);
      grpc_slice_unref(array->metadata[i].value);
    }
  }
  grpc_metadata_array_destroy(array);
}

/* grpc_run_batch_stack_cleanup ensures the run_batch_stack is properly
 * cleaned up */
static void grpc_run_batch_stack_cleanup(run_batch_stack* st) {
  size_t i = 0;

  grpc_rb_metadata_array_destroy_including_entries(&st->send_metadata);
  grpc_rb_metadata_array_destroy_including_entries(&st->send_trailing_metadata);
  grpc_metadata_array_destroy(&st->recv_metadata);
  grpc_metadata_array_destroy(&st->recv_trailing_metadata);

  if (GRPC_SLICE_START_PTR(st->send_status_details) != NULL) {
    grpc_slice_unref(st->send_status_details);
  }

  if (GRPC_SLICE_START_PTR(st->recv_status_details) != NULL) {
    grpc_slice_unref(st->recv_status_details);
  }

  if (st->recv_message != NULL) {
    grpc_byte_buffer_destroy(st->recv_message);
  }

  for (i = 0; i < st->op_num; i++) {
    if (st->ops[i].op == GRPC_OP_SEND_MESSAGE) {
      grpc_byte_buffer_destroy(st->ops[i].data.send_message.send_message);
    }
  }
}

/* grpc_run_batch_stack_fill_ops fills the run_batch_stack ops array from
 * ops_hash */
static void grpc_run_batch_stack_fill_ops(run_batch_stack* st, VALUE ops_hash) {
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
        grpc_rb_md_ary_convert(this_value, &st->send_metadata);
        st->ops[st->op_num].data.send_initial_metadata.count =
            st->send_metadata.count;
        st->ops[st->op_num].data.send_initial_metadata.metadata =
            st->send_metadata.metadata;
        break;
      case GRPC_OP_SEND_MESSAGE:
        st->ops[st->op_num].data.send_message.send_message =
            grpc_rb_s_to_byte_buffer(RSTRING_PTR(this_value),
                                     RSTRING_LEN(this_value));
        st->ops[st->op_num].flags = st->write_flag;
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        grpc_rb_op_update_status_from_server(
            &st->ops[st->op_num], &st->send_trailing_metadata,
            &st->send_status_details, this_value);
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        st->ops[st->op_num].data.recv_initial_metadata.recv_initial_metadata =
            &st->recv_metadata;
        break;
      case GRPC_OP_RECV_MESSAGE:
        st->ops[st->op_num].data.recv_message.recv_message = &st->recv_message;
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        st->ops[st->op_num].data.recv_status_on_client.trailing_metadata =
            &st->recv_trailing_metadata;
        st->ops[st->op_num].data.recv_status_on_client.status =
            &st->recv_status;
        st->ops[st->op_num].data.recv_status_on_client.status_details =
            &st->recv_status_details;
        st->ops[st->op_num].data.recv_status_on_client.error_string =
            &st->recv_status_debug_error_string;
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
static VALUE grpc_run_batch_stack_build_result(run_batch_stack* st) {
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
            rb_struct_new(
                grpc_rb_sStatus, UINT2NUM(st->recv_status),
                (GRPC_SLICE_START_PTR(st->recv_status_details) == NULL
                     ? Qnil
                     : grpc_rb_slice_to_ruby_string(st->recv_status_details)),
                grpc_rb_md_ary_to_h(&st->recv_trailing_metadata),
                st->recv_status_debug_error_string == NULL
                    ? Qnil
                    : rb_str_new_cstr(st->recv_status_debug_error_string),
                NULL));
        gpr_free((void*)st->recv_status_debug_error_string);
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

struct call_run_batch_args {
  grpc_rb_call* call;
  unsigned write_flag;
  VALUE ops_hash;
  run_batch_stack* st;
};

static VALUE grpc_rb_call_run_batch_try(VALUE value_args) {
  grpc_rb_fork_unsafe_begin();
  struct call_run_batch_args* args = (struct call_run_batch_args*)value_args;
  void* tag = (void*)&args->st;

  grpc_event ev;
  grpc_call_error err;

  args->st = gpr_malloc(sizeof(run_batch_stack));
  grpc_run_batch_stack_init(args->st, args->write_flag);
  grpc_run_batch_stack_fill_ops(args->st, args->ops_hash);

  /* call grpc_call_start_batch, then wait for it to complete using
   * pluck_event */
  err = grpc_call_start_batch(args->call->wrapped, args->st->ops,
                              args->st->op_num, tag, NULL);
  if (err != GRPC_CALL_OK) {
    rb_raise(grpc_rb_eCallError,
             "grpc_call_start_batch failed with %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }
  ev = rb_completion_queue_pluck(args->call->queue, tag,
                                 gpr_inf_future(GPR_CLOCK_REALTIME), "call op");
  if (!ev.success) {
    rb_raise(grpc_rb_eCallError, "call#run_batch failed somehow");
  }
  /* Build and return the BatchResult struct result,
     if there is an error, it's reflected in the status */
  return grpc_run_batch_stack_build_result(args->st);
}

static VALUE grpc_rb_call_run_batch_ensure(VALUE value_args) {
  grpc_rb_fork_unsafe_end();
  struct call_run_batch_args* args = (struct call_run_batch_args*)value_args;

  if (args->st) {
    grpc_run_batch_stack_cleanup(args->st);
    gpr_free(args->st);
  }

  return Qnil;
}

/* call-seq:
   ops = {
     GRPC::Core::CallOps::SEND_INITIAL_METADATA => <op_value>,
     GRPC::Core::CallOps::SEND_MESSAGE => <op_value>,
     ...
   }
   tag = Object.new
   timeout = 10
   call.start_batch(tag, timeout, ops)

   Start a batch of operations defined in the array ops; when complete, post a
   completion of type 'tag' to the completion queue bound to the call.

   Also waits for the batch to complete, until timeout is reached.
   The order of ops specified in the batch has no significance.
   Only one operation of each type can be active at once in any given
   batch */
static VALUE grpc_rb_call_run_batch(VALUE self, VALUE ops_hash) {
  grpc_ruby_fork_guard();
  if (RTYPEDDATA_DATA(self) == NULL) {
    rb_raise(grpc_rb_eCallError, "Cannot run batch on closed call");
  }

  grpc_rb_call* call = NULL;
  TypedData_Get_Struct(self, grpc_rb_call, &grpc_call_data_type, call);

  /* Validate the ops args, adding them to a ruby array */
  if (TYPE(ops_hash) != T_HASH) {
    rb_raise(rb_eTypeError, "call#run_batch: ops hash should be a hash");
  }

  VALUE rb_write_flag = rb_ivar_get(self, id_write_flag);

  struct call_run_batch_args args = {
      .call = call,
      .write_flag = rb_write_flag == Qnil ? 0 : NUM2UINT(rb_write_flag),
      .ops_hash = ops_hash,
      .st = NULL};

  return rb_ensure(grpc_rb_call_run_batch_try, (VALUE)&args,
                   grpc_rb_call_run_batch_ensure, (VALUE)&args);
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

  /* Hint the GC that this is a global and shouldn't be sweeped. */
  rb_global_variable(&rb_error_code_details);

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

static void Init_grpc_metadata_keys() {
  VALUE grpc_rb_mMetadataKeys =
      rb_define_module_under(grpc_rb_mGrpcCore, "MetadataKeys");
  rb_define_const(grpc_rb_mMetadataKeys, "COMPRESSION_REQUEST_ALGORITHM",
                  rb_str_new2(GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY));
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
  rb_undef_alloc_func(grpc_rb_cMdAry);

  /* Prevent allocation or inialization of the Call class */
  rb_define_alloc_func(grpc_rb_cCall, grpc_rb_cannot_alloc);
  rb_define_method(grpc_rb_cCall, "initialize", grpc_rb_cannot_init, 0);
  rb_define_method(grpc_rb_cCall, "initialize_copy", grpc_rb_cannot_init_copy,
                   1);

  /* Add ruby analogues of the Call methods. */
  rb_define_method(grpc_rb_cCall, "run_batch", grpc_rb_call_run_batch, 1);
  rb_define_method(grpc_rb_cCall, "cancel", grpc_rb_call_cancel, 0);
  rb_define_method(grpc_rb_cCall, "cancel_with_status",
                   grpc_rb_call_cancel_with_status, 2);
  rb_define_method(grpc_rb_cCall, "close", grpc_rb_call_close, 0);
  rb_define_method(grpc_rb_cCall, "peer", grpc_rb_call_get_peer, 0);
  rb_define_method(grpc_rb_cCall, "peer_cert", grpc_rb_call_get_peer_cert, 0);
  rb_define_method(grpc_rb_cCall, "status", grpc_rb_call_get_status, 0);
  rb_define_method(grpc_rb_cCall, "status=", grpc_rb_call_set_status, 1);
  rb_define_method(grpc_rb_cCall, "metadata", grpc_rb_call_get_metadata, 0);
  rb_define_method(grpc_rb_cCall, "metadata=", grpc_rb_call_set_metadata, 1);
  rb_define_method(grpc_rb_cCall, "trailing_metadata",
                   grpc_rb_call_get_trailing_metadata, 0);
  rb_define_method(grpc_rb_cCall,
                   "trailing_metadata=", grpc_rb_call_set_trailing_metadata, 1);
  rb_define_method(grpc_rb_cCall, "write_flag", grpc_rb_call_get_write_flag, 0);
  rb_define_method(grpc_rb_cCall, "write_flag=", grpc_rb_call_set_write_flag,
                   1);
  rb_define_method(grpc_rb_cCall, "set_credentials!",
                   grpc_rb_call_set_credentials, 1);

  /* Ids used to support call attributes */
  id_metadata = rb_intern("metadata");
  id_trailing_metadata = rb_intern("trailing_metadata");
  id_status = rb_intern("status");
  id_write_flag = rb_intern("write_flag");

  /* Ids used by the c wrapping internals. */
  id_credentials = rb_intern("__credentials");

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

  Init_grpc_error_codes();
  Init_grpc_op_codes();
  Init_grpc_write_flags();
  Init_grpc_metadata_keys();
}

/* Gets the call from the ruby object */
grpc_call* grpc_rb_get_wrapped_call(VALUE v) {
  grpc_rb_call* call = NULL;
  TypedData_Get_Struct(v, grpc_rb_call, &grpc_call_data_type, call);
  return call->wrapped;
}

/* Obtains the wrapped object for a given call */
VALUE grpc_rb_wrap_call(grpc_call* c, grpc_completion_queue* q) {
  grpc_rb_call* wrapper;
  if (c == NULL || q == NULL) {
    return Qnil;
  }
  wrapper = ALLOC(grpc_rb_call);
  wrapper->wrapped = c;
  wrapper->queue = q;
  return TypedData_Wrap_Struct(grpc_rb_cCall, &grpc_call_data_type, wrapper);
}
