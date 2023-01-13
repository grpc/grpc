//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include <string.h>

#include <algorithm>
#include <new>

#include "absl/status/status.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
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

namespace grpc_core {

const grpc_channel_filter ServerAuthFilter::kFilter =
    MakePromiseBasedFilter<ServerAuthFilter, FilterEndpoint::kServer>(
        "server-auth");

namespace {

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

// TODO(ctiller): seek out all users of this functionality and change API so
// that this unilateral format conversion IS NOT REQUIRED.
grpc_metadata_array MetadataBatchToMetadataArray(
    const grpc_metadata_batch* batch) {
  grpc_metadata_array result;
  grpc_metadata_array_init(&result);
  ArrayEncoder encoder(&result);
  batch->Encode(&encoder);
  return result;
}

}  // namespace

class ServerAuthFilter::RunApplicationCode {
 public:
  // TODO(ctiller): Allocate state_ into a pool on the arena to reuse this
  // memory later
  RunApplicationCode(ServerAuthFilter* filter, CallArgs call_args)
      : state_(GetContext<Arena>()->ManagedNew<State>(std::move(call_args))) {
    filter->server_credentials_->auth_metadata_processor().process(
        filter->server_credentials_->auth_metadata_processor().state,
        filter->auth_context_.get(), state_->md.metadata, state_->md.count,
        OnMdProcessingDone, state_);
  }

  Poll<absl::StatusOr<CallArgs>> operator()() {
    if (state_->done.load(std::memory_order_acquire)) {
      return Poll<absl::StatusOr<CallArgs>>(std::move(state_->call_args));
    }
    return Pending{};
  }

 private:
  struct State {
    explicit State(CallArgs call_args) : call_args(std::move(call_args)) {}
    Waker waker{Activity::current()->MakeOwningWaker()};
    absl::StatusOr<CallArgs> call_args;
    grpc_metadata_array md =
        MetadataBatchToMetadataArray(call_args.client_initial_metadata.get());
    std::atomic<bool> done{false};
  };

  // Called from application code.
  static void OnMdProcessingDone(
      void* user_data, const grpc_metadata* consumed_md, size_t num_consumed_md,
      const grpc_metadata* response_md, size_t num_response_md,
      grpc_status_code status, const char* error_details) {
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx exec_ctx;

    auto* state = static_cast<State*>(user_data);

    // TODO(ZhenLian): Implement support for response_md.
    if (response_md != nullptr && num_response_md > 0) {
      gpr_log(GPR_ERROR,
              "response_md in auth metadata processing not supported for now. "
              "Ignoring...");
    }

    if (status == GRPC_STATUS_OK) {
      CallArgs md = state->call_args->client_initial_metadata;
      for (size_t i = 0; i < num_consumed_md; i++) {
        md->Remove(grpc_core::StringViewFromSlice(consumed_md[i].key));
      }
    } else {
      if (error_details == nullptr) {
        error_details = "Authentication metadata processing failed.";
      }
      state_->call_args =
          grpc_error_set_int(GRPC_ERROR_CREATE(error_details),
                             grpc_core::StatusIntProperty::kRpcStatus, status);
    }

    // Clean up.
    for (size_t i = 0; i < state_->md.count; i++) {
      grpc_core::CSliceUnref(state_->md.metadata[i].key);
      grpc_core::CSliceUnref(state_->md.metadata[i].value);
    }
    grpc_metadata_array_destroy(&state_->md);

    auto waker = std::move(state_->waker);
    state_->done.store(true, std::memory_order_release);
    waker.Wakeup();
  }

  State* state_;
};

ArenaPromise<ServerMetadataHandle> ServerAuthFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  // Create server security context.  Set its auth context from channel
  // data and save it in the call context.
  grpc_server_security_context* server_ctx =
      grpc_server_security_context_create(GetContext<Arena>());
  server_ctx->auth_context =
      auth_context_->Ref(DEBUG_LOCATION, "server_auth_filter");
  grpc_call_context_element& context =
      GetContext<grpc_call_context_element>()[GRPC_CONTEXT_SECURITY];
  if (context.value != nullptr) context.destroy(context.value);
  context.value = server_ctx;
  context.destroy = grpc_server_security_context_destroy;

  if (server_credentials_ == nullptr ||
      server_credentials_->auth_metadata_processor().process == nullptr) {
    return next_promise_factory(std::move(call_args));
  }

  return TrySeq(RunApplicationCode(this, std::move(call_args)),
                std::move(next_promise_factory));
}

absl::StatusOr<ServerAuthFilter> ServerAuthFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  auto auth_context = args.GetObjectRef<grpc_auth_context>();
  GPR_ASSERT(auth_context != nullptr);
  auto creds = args.GetObjectRef<grpc_server_credentials>();
  return ServerAuthFilter(std::move(auth_context), std::move(creds));
}

}  // namespace grpc_core
