// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/call_trace.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

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
                    Activity::current()->DebugTag().c_str(),
                    source_filter->name,
                    call_args.client_initial_metadata->DebugString().c_str());
                return [source_filter, child = next_promise_factory(
                                           std::move(call_args))]() mutable {
                  gpr_log(GPR_DEBUG, "%s[%s] PollCallPromise: begin",
                          Activity::current()->DebugTag().c_str(),
                          source_filter->name);
                  auto r = child();
                  if (auto* p = r.value_if_ready()) {
                    gpr_log(GPR_DEBUG, "%s[%s] PollCallPromise: done: %s",
                            Activity::current()->DebugTag().c_str(),
                            source_filter->name, (*p)->DebugString().c_str());
                  } else {
                    gpr_log(GPR_DEBUG, "%s[%s] PollCallPromise: <<pending>>",
                            Activity::current()->DebugTag().c_str(),
                            source_filter->name);
                  }
                  return r;
                };
              },
              grpc_channel_next_op, /* sizeof_call_data: */ 0,
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

}  // namespace grpc_core
