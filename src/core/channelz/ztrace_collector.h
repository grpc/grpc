// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CHANNELZ_ZTRACE_COLLECTOR_H
#define GRPC_SRC_CORE_CHANNELZ_ZTRACE_COLLECTOR_H

#include <memory>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "src/core/channelz/channelz.h"
#include "src/core/util/single_set_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"

namespace grpc_core::channelz {

namespace ztrace_collector_detail {
template <typename T>
void AppendResults(const std::vector<std::pair<gpr_cycle_counter, T>>& data,
                   Json::Array& results) {
  for (const auto& value : data) {
    Json::Object object;
    object["timestamp"] = Json::FromString(
        gpr_format_timespec(gpr_cycle_counter_to_time(data.first)));
    data.second.RenderJson(object);
    results.emplace_back(Json::FromObject(std::move(object)));
  }
}
}  // namespace ztrace_collector_detail

template <typename Config, typename... Data>
class ZTraceCollector {
 public:
  template <typename F>
  void Append(F producer) {
    if (!impl_.is_set()) return;
    AppendValue(producer());
  }

  std::unique_ptr<ZTrace> MakeZTrace() {
    return std::make_unique<ZTraceImpl>(impl_.GetOrCreate());
  }

 private:
  struct Instance : public RefCounted<Instance> {
    Instance(std::map<std::string, std::string> args,
             std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                 event_engine,
             absl::AnyInvocable<void(absl::StatusOr<Json> output)> done)
        : config(std::move(args)),
          event_engine(std::move(event_engine)),
          done(std::move(done)) {}
    void Finish(absl::Status status) {
      if (status.ok()) {
        event_engine->Run([data = std::move(data),
                           done = std::move(done)]() mutable {
          Json::Array results;
          (ztrace_collector_detail::AppendResults(
               std::get<std::vector<std::pair<gpr_cycle_counter, Data>>>(data),
               results),
           ...);
          done(Json::FromArray(std::move(results)));
        });
      } else {
        event_engine->Run([status = std::move(status), data = std::move(data),
                           done = std::move(done)]() mutable { done(status); });
      }
    }
    Config config;
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine;
    grpc_event_engine::experimental::EventEngine::TaskHandle task_handle{
        grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid};
    std::tuple<std::vector<std::pair<gpr_cycle_counter, Data>...>> data;
    absl::AnyInvocable<void(absl::StatusOr<Json> output)> done;
  };
  struct Impl : public RefCounted<Impl> {
    Mutex mu;
    absl::flat_hash_set<RefCountedPtr<Instance>> instances ABSL_GUARDED_BY(mu);
  };
  class ZTraceImpl final : public ZTrace {
   public:
    explicit ZTraceImpl(RefCountedPtr<Impl> impl);

    void Run(Timestamp deadline, std::map<std::string, std::string> args,
             std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                 event_engine,
             absl::AnyInvocable<void(absl::StatusOr<Json> output)> callback) {
      auto instance = MakeRefCounted<Instance>(
          std::move(args), std::move(event_engine), std::move(callback));
      MutexLock lock(&impl_->mu);
      instance->task_handle =
          event_engine->RunAfter(deadline, [instance, impl = impl_]() {
            bool finish;
            {
              MutexLock lock(&impl->mu);
              finish = impl->instances.erase(instance);
            }
            if (finish) instance->Finish(absl::DeadlineExceededError(""));
          });
      impl_->instances.insert(instance);
    }

   private:
    RefCountedPtr<Impl> impl_;
  };

  template <typename T>
  void AppendValue(T&& data) {
    auto value = std::pair(gpr_get_cycle_counter(), std::forward<T>(data));
    auto* impl = impl_.Get();
    std::vector<RefCountedPtr<Instance>> finished;
    {
      MutexLock lock(&impl->mu);
      for (auto& instance : impl->instances) {
        std::get<std::vector<T>>(instance.data).push_back(data);
        if (instance.config.Finishes(instance.data)) {
          finished.push_back(instance);
        }
      }
      for (const auto& instance : finished) {
        impl->instances.erase(instance);
      }
    }
    for (const auto& instance : finished) {
      instance->Finish(std::move(data));
    }
  }

  SingleSetRefCountedPtr<Impl> impl_;
};

}  // namespace grpc_core::channelz

#endif
