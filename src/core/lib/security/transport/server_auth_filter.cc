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

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
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

const NoInterceptor ServerAuthFilter::Call::OnClientToServerMessage;
const NoInterceptor ServerAuthFilter::Call::OnClientToServerHalfClose;
const NoInterceptor ServerAuthFilter::Call::OnServerToClientMessage;
const NoInterceptor ServerAuthFilter::Call::OnServerInitialMetadata;
const NoInterceptor ServerAuthFilter::Call::OnServerTrailingMetadata;
const NoInterceptor ServerAuthFilter::Call::OnFinalize;

namespace {

class ArrayEncoder {
 public:
  explicit ArrayEncoder(grpc_metadata_array* result) : result_(result) {}

  void Encode(const Slice& key, const Slice& value) {
    Append(key.Ref(), value.Ref());
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& value) {
    Append(Slice(StaticSlice::FromStaticString(Which::key())),
           Slice(Which::Encode(value)));
  }

  void Encode(HttpMethodMetadata,
              const typename HttpMethodMetadata::ValueType&) {}

 private:
  void Append(Slice key, Slice value) {
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

struct ServerAuthFilter::RunApplicationCode::State {
  explicit State(ClientMetadata& client_metadata)
      : client_metadata(&client_metadata) {}
  Waker waker{GetContext<Activity>()->MakeOwningWaker()};
  absl::StatusOr<ClientMetadata*> client_metadata;
  grpc_metadata_array md = MetadataBatchToMetadataArray(*client_metadata);
  std::atomic<bool> done{false};
};

ServerAuthFilter::RunApplicationCode::RunApplicationCode(
    ServerAuthFilter* filter, ClientMetadata& metadata)
    : state_(GetContext<Arena>()->ManagedNew<State>(metadata)) {
  if (GRPC_TRACE_FLAG_ENABLED(call)) {
    gpr_log(GPR_ERROR,
            "%s[server-auth]: Delegate to application: filter=%p this=%p "
            "auth_ctx=%p",
            GetContext<Activity>()->DebugTag().c_str(), filter, this,
            filter->auth_context_.get());
  }
  filter->server_credentials_->auth_metadata_processor().process(
      filter->server_credentials_->auth_metadata_processor().state,
      filter->auth_context_.get(), state_->md.metadata, state_->md.count,
      OnMdProcessingDone, state_);
}

Poll<absl::Status> ServerAuthFilter::RunApplicationCode::operator()() {
  if (state_->done.load(std::memory_order_acquire)) {
    return Poll<absl::Status>(std::move(state_->client_metadata).status());
  }
  return Pending{};
}

void ServerAuthFilter::RunApplicationCode::OnMdProcessingDone(
    void* user_data, const grpc_metadata* consumed_md, size_t num_consumed_md,
    const grpc_metadata* response_md, size_t num_response_md,
    grpc_status_code status, const char* error_details) {
  ApplicationCallbackExecCtx callback_exec_ctx;
  ExecCtx exec_ctx;

  auto* state = static_cast<State*>(user_data);

  // TODO(ZhenLian): Implement support for response_md.
  if (response_md != nullptr && num_response_md > 0) {
    gpr_log(GPR_ERROR,
            "response_md in auth metadata processing not supported for now. "
            "Ignoring...");
  }

  if (status == GRPC_STATUS_OK) {
    ClientMetadata& md = **state->client_metadata;
    for (size_t i = 0; i < num_consumed_md; i++) {
      md.Remove(StringViewFromSlice(consumed_md[i].key));
    }
  } else {
    if (error_details == nullptr) {
      error_details = "Authentication metadata processing failed.";
    }
    state->client_metadata = grpc_error_set_int(
        absl::Status(static_cast<absl::StatusCode>(status), error_details),
        StatusIntProperty::kRpcStatus, status);
  }

  // Clean up.
  for (size_t i = 0; i < state->md.count; i++) {
    CSliceUnref(state->md.metadata[i].key);
    CSliceUnref(state->md.metadata[i].value);
  }
  grpc_metadata_array_destroy(&state->md);

  auto waker = std::move(state->waker);
  state->done.store(true, std::memory_order_release);
  waker.Wakeup();
}

ServerAuthFilter::Call::Call(ServerAuthFilter* filter) {
  // Create server security context.  Set its auth context from channel
  // data and save it in the call context.
  grpc_server_security_context* server_ctx =
      grpc_server_security_context_create(GetContext<Arena>());
  server_ctx->auth_context =
      filter->auth_context_->Ref(DEBUG_LOCATION, "server_auth_filter");
  SetContext<SecurityContext>(server_ctx);
}

ServerAuthFilter::ServerAuthFilter(
    RefCountedPtr<grpc_server_credentials> server_credentials,
    RefCountedPtr<grpc_auth_context> auth_context)
    : server_credentials_(server_credentials), auth_context_(auth_context) {}

absl::StatusOr<std::unique_ptr<ServerAuthFilter>> ServerAuthFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  auto auth_context = args.GetObjectRef<grpc_auth_context>();
  CHECK(auth_context != nullptr);
  auto creds = args.GetObjectRef<grpc_server_credentials>();
  return std::make_unique<ServerAuthFilter>(std::move(creds),
                                            std::move(auth_context));
}

}  // namespace grpc_core
