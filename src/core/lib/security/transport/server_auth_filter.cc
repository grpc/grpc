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

#include <grpc/support/port_platform.h>

#include <string.h>

#include <algorithm>
#include <new>

#include "absl/status/status.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/auth_filters.h"  // IWYU pragma: keep
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

static void recv_initial_metadata_ready(void* arg, grpc_error_handle error);
static void recv_trailing_metadata_ready(void* user_data,
                                         grpc_error_handle error);

namespace {
enum async_state {
  STATE_INIT = 0,
  STATE_DONE,
  STATE_CANCELLED,
};

struct channel_data {
  channel_data(grpc_auth_context* auth_context, grpc_server_credentials* creds)
      : auth_context(auth_context->Ref()), creds(creds->Ref()) {}
  ~channel_data() { auth_context.reset(DEBUG_LOCATION, "server_auth_filter"); }

  grpc_core::RefCountedPtr<grpc_auth_context> auth_context;
  grpc_core::RefCountedPtr<grpc_server_credentials> creds;
};

struct call_data {
  call_data(grpc_call_element* elem, const grpc_call_element_args& args)
      : call_combiner(args.call_combiner), owning_call(args.call_stack) {
    GRPC_CLOSURE_INIT(&recv_initial_metadata_ready,
                      ::recv_initial_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready,
                      ::recv_trailing_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
    // Create server security context.  Set its auth context from channel
    // data and save it in the call context.
    grpc_server_security_context* server_ctx =
        grpc_server_security_context_create(args.arena);
    channel_data* chand = static_cast<channel_data*>(elem->channel_data);
    server_ctx->auth_context =
        chand->auth_context->Ref(DEBUG_LOCATION, "server_auth_filter");
    if (args.context[GRPC_CONTEXT_SECURITY].value != nullptr) {
      args.context[GRPC_CONTEXT_SECURITY].destroy(
          args.context[GRPC_CONTEXT_SECURITY].value);
    }
    args.context[GRPC_CONTEXT_SECURITY].value = server_ctx;
    args.context[GRPC_CONTEXT_SECURITY].destroy =
        grpc_server_security_context_destroy;
  }

  ~call_data() {}

  grpc_core::CallCombiner* call_combiner;
  grpc_call_stack* owning_call;
  grpc_transport_stream_op_batch* recv_initial_metadata_batch;
  grpc_closure* original_recv_initial_metadata_ready;
  grpc_closure recv_initial_metadata_ready;
  grpc_error_handle recv_initial_metadata_error;
  grpc_closure recv_trailing_metadata_ready;
  grpc_closure* original_recv_trailing_metadata_ready;
  grpc_error_handle recv_trailing_metadata_error;
  bool seen_recv_trailing_metadata_ready = false;
  grpc_metadata_array md;
  grpc_closure cancel_closure;
  gpr_atm state = STATE_INIT;  // async_state
};

class ArrayEncoder {
 public:
  explicit ArrayEncoder(grpc_metadata_array* result) : result_(result) {}

  void Encode(const grpc_core::Slice& key, const grpc_core::Slice& value) {
    Append(key.Ref(), value.Ref());
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& value) {
    Append(grpc_core::Slice(
               grpc_core::StaticSlice::FromStaticString(Which::key())),
           grpc_core::Slice(Which::Encode(value)));
  }

  void Encode(grpc_core::HttpMethodMetadata,
              const typename grpc_core::HttpMethodMetadata::ValueType&) {}

 private:
  void Append(grpc_core::Slice key, grpc_core::Slice value) {
    if (result_->count == result_->capacity) {
      result_->capacity =
          std::max(result_->capacity + 8, result_->capacity * 2);
      result_->metadata = static_cast<grpc_metadata*>(gpr_realloc(
          result_->metadata, result_->capacity * sizeof(grpc_metadata)));
    }
    auto* usr_md = &result_->metadata[result_->count++];
    usr_md->key = key.TakeCSlice();
    usr_md->value = value.TakeCSlice();
  }

  grpc_metadata_array* result_;
};

}  // namespace

static grpc_metadata_array metadata_batch_to_md_array(
    const grpc_metadata_batch* batch) {
  grpc_metadata_array result;
  grpc_metadata_array_init(&result);
  ArrayEncoder encoder(&result);
  batch->Encode(&encoder);
  return result;
}

static void on_md_processing_done_inner(grpc_call_element* elem,
                                        const grpc_metadata* consumed_md,
                                        size_t num_consumed_md,
                                        const grpc_metadata* response_md,
                                        size_t num_response_md,
                                        grpc_error_handle error) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_transport_stream_op_batch* batch = calld->recv_initial_metadata_batch;
  /* TODO(ZhenLian): Implement support for response_md. */
  if (response_md != nullptr && num_response_md > 0) {
    gpr_log(GPR_ERROR,
            "response_md in auth metadata processing not supported for now. "
            "Ignoring...");
  }
  if (error.ok()) {
    for (size_t i = 0; i < num_consumed_md; i++) {
      batch->payload->recv_initial_metadata.recv_initial_metadata->Remove(
          grpc_core::StringViewFromSlice(consumed_md[i].key));
    }
  }
  calld->recv_initial_metadata_error = error;
  grpc_closure* closure = calld->original_recv_initial_metadata_ready;
  calld->original_recv_initial_metadata_ready = nullptr;
  if (calld->seen_recv_trailing_metadata_ready) {
    GRPC_CALL_COMBINER_START(calld->call_combiner,
                             &calld->recv_trailing_metadata_ready,
                             calld->recv_trailing_metadata_error,
                             "continue recv_trailing_metadata_ready");
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, error);
}

// Called from application code.
static void on_md_processing_done(
    void* user_data, const grpc_metadata* consumed_md, size_t num_consumed_md,
    const grpc_metadata* response_md, size_t num_response_md,
    grpc_status_code status, const char* error_details) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  // If the call was not cancelled while we were in flight, process the result.
  if (gpr_atm_full_cas(&calld->state, static_cast<gpr_atm>(STATE_INIT),
                       static_cast<gpr_atm>(STATE_DONE))) {
    grpc_error_handle error;
    if (status != GRPC_STATUS_OK) {
      if (error_details == nullptr) {
        error_details = "Authentication metadata processing failed.";
      }
      error =
          grpc_error_set_int(GRPC_ERROR_CREATE(error_details),
                             grpc_core::StatusIntProperty::kRpcStatus, status);
    }
    on_md_processing_done_inner(elem, consumed_md, num_consumed_md, response_md,
                                num_response_md, error);
  }
  // Clean up.
  for (size_t i = 0; i < calld->md.count; i++) {
    grpc_core::CSliceUnref(calld->md.metadata[i].key);
    grpc_core::CSliceUnref(calld->md.metadata[i].value);
  }
  grpc_metadata_array_destroy(&calld->md);
  GRPC_CALL_STACK_UNREF(calld->owning_call, "server_auth_metadata");
}

static void cancel_call(void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // If the result was not already processed, invoke the callback now.
  if (!error.ok() &&
      gpr_atm_full_cas(&calld->state, static_cast<gpr_atm>(STATE_INIT),
                       static_cast<gpr_atm>(STATE_CANCELLED))) {
    on_md_processing_done_inner(elem, nullptr, 0, nullptr, 0, error);
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call, "cancel_call");
}

static void recv_initial_metadata_ready(void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_transport_stream_op_batch* batch = calld->recv_initial_metadata_batch;
  if (error.ok()) {
    if (chand->creds != nullptr &&
        chand->creds->auth_metadata_processor().process != nullptr) {
      // We're calling out to the application, so we need to make sure
      // to drop the call combiner early if we get cancelled.
      // TODO(yashykt): We would not need this ref if call combiners used
      // Closure::Run() instead of ExecCtx::Run()
      GRPC_CALL_STACK_REF(calld->owning_call, "cancel_call");
      GRPC_CLOSURE_INIT(&calld->cancel_closure, cancel_call, elem,
                        grpc_schedule_on_exec_ctx);
      calld->call_combiner->SetNotifyOnCancel(&calld->cancel_closure);
      GRPC_CALL_STACK_REF(calld->owning_call, "server_auth_metadata");
      calld->md = metadata_batch_to_md_array(
          batch->payload->recv_initial_metadata.recv_initial_metadata);
      chand->creds->auth_metadata_processor().process(
          chand->creds->auth_metadata_processor().state,
          chand->auth_context.get(), calld->md.metadata, calld->md.count,
          on_md_processing_done, elem);
      return;
    }
  }
  grpc_closure* closure = calld->original_recv_initial_metadata_ready;
  calld->original_recv_initial_metadata_ready = nullptr;
  if (calld->seen_recv_trailing_metadata_ready) {
    GRPC_CALL_COMBINER_START(calld->call_combiner,
                             &calld->recv_trailing_metadata_ready,
                             calld->recv_trailing_metadata_error,
                             "continue recv_trailing_metadata_ready");
  }
  grpc_core::Closure::Run(DEBUG_LOCATION, closure, error);
}

static void recv_trailing_metadata_ready(void* user_data,
                                         grpc_error_handle err) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (calld->original_recv_initial_metadata_ready != nullptr) {
    calld->recv_trailing_metadata_error = err;
    calld->seen_recv_trailing_metadata_ready = true;
    GRPC_CALL_COMBINER_STOP(calld->call_combiner,
                            "deferring recv_trailing_metadata_ready until "
                            "after recv_initial_metadata_ready");
    return;
  }
  err = grpc_error_add_child(err, calld->recv_initial_metadata_error);
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->original_recv_trailing_metadata_ready, err);
}

static void server_auth_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (batch->recv_initial_metadata) {
    // Inject our callback.
    calld->recv_initial_metadata_batch = batch;
    calld->original_recv_initial_metadata_ready =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready;
  }
  if (batch->recv_trailing_metadata) {
    calld->original_recv_trailing_metadata_ready =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &calld->recv_trailing_metadata_ready;
  }
  grpc_call_next_op(elem, batch);
}

/* Constructor for call_data */
static grpc_error_handle server_auth_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  new (elem->call_data) call_data(elem, *args);
  return absl::OkStatus();
}

/* Destructor for call_data */
static void server_auth_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->~call_data();
}

/* Constructor for channel_data */
static grpc_error_handle server_auth_init_channel_elem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  grpc_auth_context* auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  GPR_ASSERT(auth_context != nullptr);
  grpc_server_credentials* creds =
      grpc_find_server_credentials_in_args(args->channel_args);
  new (elem->channel_data) channel_data(auth_context, creds);
  return absl::OkStatus();
}

/* Destructor for channel data */
static void server_auth_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->~channel_data();
}

const grpc_channel_filter grpc_server_auth_filter = {
    server_auth_start_transport_stream_op_batch,
    nullptr,
    grpc_channel_next_op,
    sizeof(call_data),
    server_auth_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    server_auth_destroy_call_elem,
    sizeof(channel_data),
    server_auth_init_channel_elem,
    grpc_channel_stack_no_post_init,
    server_auth_destroy_channel_elem,
    grpc_channel_next_get_info,
    "server-auth"};
