//
//
// Copyright 2016 gRPC authors.
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

#include "src/core/lib/channel/channel_stack_builder_impl.h"

#include <string.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/surface/call_trace.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

namespace {

const grpc_channel_filter* PromiseTracingFilterFor(
    const grpc_channel_filter* filter) {
  struct DerivedFilter : public grpc_channel_filter {
    explicit DerivedFilter(const grpc_channel_filter* filter)
        : grpc_channel_filter{
              // start_transport_stream_op_batch:
              grpc_call_next_op,
              // make_call_promise:
              [](grpc_channel_element* elem, CallArgs call_args,
                 NextPromiseFactory next_promise_factory)
                  -> ArenaPromise<ServerMetadataHandle> {
                auto* source_filter =
                    static_cast<const DerivedFilter*>(elem->filter)->filter;
                gpr_log(
                    GPR_DEBUG,
                    "%s[%s] CreateCallPromise: client_initial_metadata=%s",
                    GetContext<Activity>()->DebugTag().c_str(),
                    source_filter->name,
                    call_args.client_initial_metadata->DebugString().c_str());
                return [source_filter, child = next_promise_factory(
                                           std::move(call_args))]() mutable {
                  gpr_log(GPR_DEBUG, "%s[%s] PollCallPromise: begin",
                          GetContext<Activity>()->DebugTag().c_str(),
                          source_filter->name);
                  auto r = child();
                  if (auto* p = r.value_if_ready()) {
                    gpr_log(GPR_DEBUG, "%s[%s] PollCallPromise: done: %s",
                            GetContext<Activity>()->DebugTag().c_str(),
                            source_filter->name, (*p)->DebugString().c_str());
                  } else {
                    gpr_log(GPR_DEBUG, "%s[%s] PollCallPromise: <<pending>>",
                            GetContext<Activity>()->DebugTag().c_str(),
                            source_filter->name);
                  }
                  return r;
                };
              },
              /* init_call: */
              [](grpc_channel_element* elem, CallSpineInterface* call) {
                auto* c = DownCast<PipeBasedCallSpine*>(call);
                auto* source_filter =
                    static_cast<const DerivedFilter*>(elem->filter)->filter;
                c->client_initial_metadata().receiver.InterceptAndMap(
                    [source_filter](ClientMetadataHandle md) {
                      gpr_log(GPR_DEBUG, "%s[%s] OnClientInitialMetadata: %s",
                              GetContext<Activity>()->DebugTag().c_str(),
                              source_filter->name, md->DebugString().c_str());
                      return md;
                    });
                c->client_to_server_messages().receiver.InterceptAndMap(
                    [source_filter](MessageHandle msg) {
                      gpr_log(GPR_DEBUG, "%s[%s] OnClientToServerMessage: %s",
                              GetContext<Activity>()->DebugTag().c_str(),
                              source_filter->name, msg->DebugString().c_str());
                      return msg;
                    });
                c->server_initial_metadata().sender.InterceptAndMap(
                    [source_filter](ServerMetadataHandle md) {
                      gpr_log(GPR_DEBUG, "%s[%s] OnServerInitialMetadata: %s",
                              GetContext<Activity>()->DebugTag().c_str(),
                              source_filter->name, md->DebugString().c_str());
                      return md;
                    });
                c->server_to_client_messages().sender.InterceptAndMap(
                    [source_filter](MessageHandle msg) {
                      gpr_log(GPR_DEBUG, "%s[%s] OnServerToClientMessage: %s",
                              GetContext<Activity>()->DebugTag().c_str(),
                              source_filter->name, msg->DebugString().c_str());
                      return msg;
                    });
              },
              grpc_channel_next_op,
              /* sizeof_call_data: */ 0,
              // init_call_elem:
              [](grpc_call_element*, const grpc_call_element_args*) {
                return absl::OkStatus();
              },
              grpc_call_stack_ignore_set_pollset_or_pollset_set,
              // destroy_call_elem:
              [](grpc_call_element*, const grpc_call_final_info*,
                 grpc_closure*) {},
              // sizeof_channel_data:
              0,
              // init_channel_elem:
              [](grpc_channel_element*, grpc_channel_element_args*) {
                return absl::OkStatus();
              },
              // post_init_channel_elem:
              [](grpc_channel_stack*, grpc_channel_element*) {},
              // destroy_channel_elem:
              [](grpc_channel_element*) {}, grpc_channel_next_get_info,
              // name:
              nullptr},
          filter(filter),
          name_str(absl::StrCat(filter->name, ".trace")) {
      this->name = name_str.c_str();
    }
    const grpc_channel_filter* const filter;
    const std::string name_str;
  };
  struct Globals {
    Mutex mu;
    absl::flat_hash_map<const grpc_channel_filter*,
                        std::unique_ptr<DerivedFilter>>
        map ABSL_GUARDED_BY(mu);
  };
  auto* globals = NoDestructSingleton<Globals>::Get();
  MutexLock lock(&globals->mu);
  auto it = globals->map.find(filter);
  if (it != globals->map.end()) return it->second.get();
  return globals->map.emplace(filter, std::make_unique<DerivedFilter>(filter))
      .first->second.get();
}

}  // namespace

bool ChannelStackBuilderImpl::IsPromising() const {
  for (const auto* filter : stack()) {
    if (filter->make_call_promise == nullptr) return false;
  }
  return true;
}

absl::StatusOr<RefCountedPtr<grpc_channel_stack>>
ChannelStackBuilderImpl::Build() {
  std::vector<const grpc_channel_filter*> stack;
  const bool is_promising = IsPromising();
  const bool is_client =
      grpc_channel_stack_type_is_client(channel_stack_type());
  const bool client_promise_tracing =
      is_client && is_promising && grpc_call_trace.enabled();
  const bool server_promise_tracing =
      !is_client && is_promising && grpc_call_trace.enabled();

  for (const auto* filter : this->stack()) {
    if (client_promise_tracing) {
      stack.push_back(PromiseTracingFilterFor(filter));
    }
    stack.push_back(filter);
    if (server_promise_tracing) {
      stack.push_back(PromiseTracingFilterFor(filter));
    }
  }
  if (server_promise_tracing) {
    stack.pop_back();  // connected_channel must be last => can't be traced
  }

  // calculate the size of the channel stack
  size_t channel_stack_size =
      grpc_channel_stack_size(stack.data(), stack.size());

  // allocate memory
  auto* channel_stack =
      static_cast<grpc_channel_stack*>(gpr_zalloc(channel_stack_size));

  // and initialize it
  grpc_error_handle error = grpc_channel_stack_init(
      1,
      [](void* p, grpc_error_handle) {
        auto* stk = static_cast<grpc_channel_stack*>(p);
        grpc_channel_stack_destroy(stk);
        gpr_free(stk);
      },
      channel_stack, stack.data(), stack.size(), channel_args(), name(),
      channel_stack);

  if (!error.ok()) {
    grpc_channel_stack_destroy(channel_stack);
    gpr_free(channel_stack);
    auto status = grpc_error_to_absl_status(error);
    return status;
  }

  // run post-initialization functions
  for (size_t i = 0; i < stack.size(); i++) {
    auto* elem = grpc_channel_stack_element(channel_stack, i);
    elem->filter->post_init_channel_elem(channel_stack, elem);
  }

  return RefCountedPtr<grpc_channel_stack>(channel_stack);
}

}  // namespace grpc_core
