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

#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

#include <inttypes.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include <ratio>
#include <vector>

#include "fuzzing_event_engine.h"

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/util/port.h"

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

namespace grpc_event_engine {
namespace experimental {

namespace {

EventEngine::ResolvedAddress PortToAddress(int port) {
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  return EventEngine::ResolvedAddress(reinterpret_cast<const sockaddr*>(&addr),
                                      sizeof(addr));
}

absl::StatusOr<int> AddressToPort(const EventEngine::ResolvedAddress& addr) {
  switch (addr.address()->sa_family) {
    default:
      return absl::InvalidArgumentError("Unknown address family");
    case AF_INET: {
      const sockaddr_in* addr_in =
          reinterpret_cast<const sockaddr_in*>(addr.address());
      if (addr_in->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
        return absl::InvalidArgumentError("Address is not loopback");
      }
      return ntohs(addr_in->sin_port);
    }
    case AF_INET6: {
      const sockaddr_in6* addr_in =
          reinterpret_cast<const sockaddr_in6*>(addr.address());
      if (memcmp(&addr_in->sin6_addr, &in6addr_loopback,
                 sizeof(in6addr_loopback)) != 0) {
        return absl::InvalidArgumentError("Address is not loopback");
      }
      return ntohs(addr_in->sin6_port);
    }
  }
}

}  // namespace

grpc_core::NoDestruct<grpc_core::Mutex> FuzzingEventEngine::mu_;

namespace {
const intptr_t kTaskHandleSalt = 12345;
FuzzingEventEngine* g_fuzzing_event_engine = nullptr;
gpr_timespec (*g_orig_gpr_now_impl)(gpr_clock_type clock_type);
}  // namespace

FuzzingEventEngine::FuzzingEventEngine(
    Options options, const fuzzing_event_engine::Actions& actions)
    : final_tick_length_(options.final_tick_length) {
  tick_increments_.clear();
  task_delays_.clear();
  tasks_by_id_.clear();
  tasks_by_time_.clear();
  next_task_id_ = 1;
  current_tick_ = 0;
  // Start at 5 seconds after the epoch.
  // This needs to be more than 1, and otherwise is kind of arbitrary.
  // The grpc_core::Timer code special cases the zero second time period after
  // epoch to allow for some fancy atomic stuff.
  now_ = Time() + std::chrono::seconds(5);

  // Whilst a fuzzing EventEngine is active we override grpc's now function.
  grpc_core::TestOnlySetProcessEpoch(NowAsTimespec(GPR_CLOCK_MONOTONIC));

  auto update_delay = [](std::map<intptr_t, Duration>* map,
                         const fuzzing_event_engine::Delay& delay,
                         Duration max) {
    auto& value = (*map)[delay.id()];
    if (delay.delay_us() > static_cast<uint64_t>(max.count() / GPR_NS_PER_US)) {
      value = max;
      return;
    }
    Duration add = std::chrono::microseconds(delay.delay_us());
    if (add >= max - value) {
      value = max;
    } else {
      value += add;
    }
  };

  for (const auto& delay : actions.tick_lengths()) {
    update_delay(&tick_increments_, delay, std::chrono::hours(24));
  }
  for (const auto& delay : actions.run_delay()) {
    update_delay(&task_delays_, delay, std::chrono::seconds(30));
  }

  GPR_ASSERT(g_fuzzing_event_engine == nullptr);
  g_fuzzing_event_engine = this;
  g_orig_gpr_now_impl = gpr_now_impl;
  gpr_now_impl = GlobalNowImpl;
  grpc_set_pick_port_functions(
      grpc_pick_port_functions{+[]() -> int {
                                 grpc_core::MutexLock lock(&*mu_);
                                 return g_fuzzing_event_engine->AllocatePort();
                               },
                               +[]() -> int {
                                 grpc_core::MutexLock lock(&*mu_);
                                 return g_fuzzing_event_engine->AllocatePort();
                               },
                               +[](int) {}});
}

void FuzzingEventEngine::FuzzingDone() {
  grpc_core::MutexLock lock(&*mu_);
  tick_increments_.clear();
}

gpr_timespec FuzzingEventEngine::NowAsTimespec(gpr_clock_type clock_type) {
  // TODO(ctiller): add a facility to track realtime and monotonic clocks
  // separately to simulate divergence.
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  const Duration d = now_.time_since_epoch();
  auto secs = std::chrono::duration_cast<std::chrono::seconds>(d);
  return {secs.count(), static_cast<int32_t>((d - secs).count()), clock_type};
}

void FuzzingEventEngine::Tick() {
  std::vector<absl::AnyInvocable<void()>> to_run;
  {
    grpc_core::MutexLock lock(&*mu_);
    // Increment time
    auto tick_it = tick_increments_.find(current_tick_);
    if (tick_it != tick_increments_.end()) {
      now_ += tick_it->second;
      GPR_ASSERT(now_.time_since_epoch().count() >= 0);
      tick_increments_.erase(tick_it);
    } else if (tick_increments_.empty()) {
      now_ += final_tick_length_;
      GPR_ASSERT(now_.time_since_epoch().count() >= 0);
    }
    ++current_tick_;
    // Find newly expired timers.
    while (!tasks_by_time_.empty() && tasks_by_time_.begin()->first <= now_) {
      auto& task = *tasks_by_time_.begin()->second;
      tasks_by_id_.erase(task.id);
      if (task.closure != nullptr) {
        to_run.push_back(std::move(task.closure));
      }
      tasks_by_time_.erase(tasks_by_time_.begin());
    }
  }
  for (auto& closure : to_run) {
    closure();
  }
}

FuzzingEventEngine::Time FuzzingEventEngine::Now() {
  grpc_core::MutexLock lock(&*mu_);
  return now_;
}

int FuzzingEventEngine::AllocatePort() {
  if (!free_ports_.empty()) {
    int p = free_ports_.front();
    free_ports_.pop();
    return p;
  }
  while (true) {
    int p = next_free_port_++;
    if (fuzzer_mentioned_ports_.count(p) == 0) {
      return p;
    }
  }
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
FuzzingEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown, const EndpointConfig&,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  grpc_core::MutexLock lock(&*mu_);
  return absl::make_unique<FuzzingListener>(
      *listeners_
           .emplace(std::make_shared<ListenerInfo>(
               std::move(on_accept), std::move(on_shutdown),
               std::move(memory_allocator_factory)))
           .first);
}

FuzzingEventEngine::FuzzingListener::~FuzzingListener() {
  grpc_core::MutexLock lock(&*mu_);
  g_fuzzing_event_engine->listeners_.erase(info_);
}

bool FuzzingEventEngine::IsPortUsed(int port) {
  for (const auto& listener : listeners_) {
    if (std::find(listener->ports.begin(), listener->ports.end(), port) !=
        listener->ports.end()) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<int> FuzzingEventEngine::FuzzingListener::Bind(
    const ResolvedAddress& addr) {
  auto port = AddressToPort(addr);
  if (!port.ok()) return port.status();
  grpc_core::MutexLock lock(&*mu_);
  if (info_->started) return absl::InternalError("Already started");
  if (*port != 0) {
    if (g_fuzzing_event_engine->IsPortUsed(*port)) {
      return absl::InternalError("Port in use");
    }
  } else {
    do {
      port = g_fuzzing_event_engine->AllocatePort();
    } while (g_fuzzing_event_engine->IsPortUsed(*port));
  }
  info_->ports.push_back(*port);
  return port;
}

absl::Status FuzzingEventEngine::FuzzingListener::Start() {
  grpc_core::MutexLock lock(&*mu_);
  if (info_->started) return absl::InternalError("Already started");
  info_->started = true;
  return absl::OkStatus();
}

bool FuzzingEventEngine::EndpointMiddle::Write(SliceBuffer* data, int index) {
  GPR_ASSERT(!closed);
  if (data->Length() == 0) return true;
  size_t write_len = std::numeric_limits<size_t>::max();
  if (!write_sizes[index].empty()) {
    write_len = write_sizes[index].front();
    write_sizes[index].pop();
  }
  if (write_len > data->Length()) {
    write_len = data->Length();
  }
  if (write_len == 0) write_len = 1;
  size_t prev_len = pending[index].size();
  pending[index].resize(prev_len + write_len);
  data->MoveFirstNBytesIntoBuffer(write_len, pending[index].data() + prev_len);
  if (pending_read[1 - index].has_value()) {
    pending_read[1 - index]->buffer->Append(
        Slice::FromCopiedBuffer(pending[index]));
    pending[index].clear();
    g_fuzzing_event_engine->RunLocked(
        [cb = std::move(pending_read[1 - index]->on_read)]() mutable {
          cb(absl::OkStatus());
        });
    pending_read[1 - index].reset();
  }
  return data->Length() == 0;
}

bool FuzzingEventEngine::FuzzingEndpoint::Write(
    absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data,
    const WriteArgs* args) {
  grpc_core::MutexLock lock(&*mu_);
  if (middle_->closed) {
    g_fuzzing_event_engine->RunLocked(
        [on_writable = std::move(on_writable)]() mutable {
          on_writable(absl::CancelledError("Endpoint closed"));
        });
  }
  if (middle_->Write(data, index_)) return true;
  ScheduleDelayedWrite(middle_, index_, std::move(on_writable), data);
  return false;
}

void FuzzingEventEngine::FuzzingEndpoint::ScheduleDelayedWrite(
    std::shared_ptr<EndpointMiddle> middle, int index,
    absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data) {
  g_fuzzing_event_engine->RunLocked(
      [middle = std::move(middle), index, data,
       on_writable = std::move(on_writable)]() mutable {
        grpc_core::MutexLock lock(&*mu_);
        if (middle->closed) {
          g_fuzzing_event_engine->RunLocked(
              [on_writable = std::move(on_writable)]() mutable {
                on_writable(absl::CancelledError("Endpoint closed"));
              });
          return;
        }
        if (middle->Write(data, index)) {
          on_writable(absl::OkStatus());
          return;
        }
        ScheduleDelayedWrite(std::move(middle), index, std::move(on_writable),
                             data);
      });
}

FuzzingEventEngine::FuzzingEndpoint::~FuzzingEndpoint() {
  grpc_core::MutexLock lock(&*mu_);
  middle_->closed = true;
  for (int i = 0; i < 2; i++) {
    if (middle_->pending_read[i].has_value()) {
      g_fuzzing_event_engine->RunLocked(
          [cb = std::move(middle_->pending_read[i]->on_read)]() mutable {
            cb(absl::CancelledError("Endpoint closed"));
          });
      middle_->pending_read[i].reset();
    }
  }
}

bool FuzzingEventEngine::FuzzingEndpoint::Read(
    absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
    const ReadArgs* args) {
  buffer->Clear();
  grpc_core::MutexLock lock(&*mu_);
  if (middle_->pending[1 - index_].empty()) {
    middle_->pending_read[index_] = PendingRead{std::move(on_read), buffer};
    return false;
  } else {
    buffer->Append(Slice::FromCopiedBuffer(middle_->pending[1 - index_]));
    middle_->pending[1 - index_].clear();
    return true;
  }
}

FuzzingEventEngine::EndpointMiddle::EndpointMiddle(int listener_port,
                                                   int client_port)
    : addrs{PortToAddress(listener_port), PortToAddress(client_port)} {}

EventEngine::ConnectionHandle FuzzingEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    Duration timeout) {
  auto task_handle = RunAfter(
      Duration(0), [this, addr, on_connect = std::move(on_connect)]() mutable {
        auto port = AddressToPort(addr);
        if (!port.ok()) {
          on_connect(port.status());
          return;
        }
        grpc_core::MutexLock lock(&*mu_);
        for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
          const auto& listener = *it;
          if (!listener->started) continue;
          for (int listener_port : listener->ports) {
            if (*port == listener_port) {
              auto middle = std::make_shared<EndpointMiddle>(
                  listener_port, g_fuzzing_event_engine->AllocatePort());
              auto ep1 = std::make_unique<FuzzingEndpoint>(middle, 0);
              auto ep2 = std::make_unique<FuzzingEndpoint>(middle, 1);
              RunLocked([listener, ep1 = std::move(ep1)]() mutable {
                listener->on_accept(
                    std::move(ep1),
                    listener->memory_allocator_factory->CreateMemoryAllocator(
                        "fuzzing"));
              });
              RunLocked([on_connect = std::move(on_connect),
                         ep2 = std::move(ep2)]() mutable {
                on_connect(std::move(ep2));
              });
              return;
            }
          }
        }
        RunLocked([on_connect = std::move(on_connect)]() mutable {
          on_connect(absl::InvalidArgumentError("No listener found"));
        });
      });
  return ConnectionHandle{{task_handle.keys[0], task_handle.keys[1]}};
}

bool FuzzingEventEngine::CancelConnect(ConnectionHandle connection_handle) {
  return Cancel(
      TaskHandle{{connection_handle.keys[0], connection_handle.keys[1]}});
}

bool FuzzingEventEngine::IsWorkerThread() { abort(); }

std::unique_ptr<EventEngine::DNSResolver> FuzzingEventEngine::GetDNSResolver(
    const DNSResolver::ResolverOptions&) {
  abort();
}

void FuzzingEventEngine::Run(Closure* closure) {
  RunAfter(Duration::zero(), closure);
}

void FuzzingEventEngine::Run(absl::AnyInvocable<void()> closure) {
  RunAfter(Duration::zero(), std::move(closure));
}

EventEngine::TaskHandle FuzzingEventEngine::RunAfter(Duration when,
                                                     Closure* closure) {
  return RunAfter(when, [closure]() { closure->Run(); });
}

EventEngine::TaskHandle FuzzingEventEngine::RunAfter(
    Duration when, absl::AnyInvocable<void()> closure) {
  grpc_core::MutexLock lock(&*mu_);
  return RunAfterLocked(when, std::move(closure));
}

EventEngine::TaskHandle FuzzingEventEngine::RunAfterLocked(
    Duration when, absl::AnyInvocable<void()> closure) {
  const intptr_t id = next_task_id_;
  ++next_task_id_;
  const auto delay_it = task_delays_.find(id);
  // Under fuzzer configuration control, maybe make the task run later.
  if (delay_it != task_delays_.end()) {
    when += delay_it->second;
    task_delays_.erase(delay_it);
  }
  auto task = std::make_shared<Task>(id, std::move(closure));
  tasks_by_id_.emplace(id, task);
  tasks_by_time_.emplace(now_ + when, std::move(task));
  return TaskHandle{id, kTaskHandleSalt};
}

bool FuzzingEventEngine::Cancel(TaskHandle handle) {
  grpc_core::MutexLock lock(&*mu_);
  GPR_ASSERT(handle.keys[1] == kTaskHandleSalt);
  const intptr_t id = handle.keys[0];
  auto it = tasks_by_id_.find(id);
  if (it == tasks_by_id_.end()) {
    return false;
  }
  if (it->second->closure == nullptr) {
    return false;
  }
  it->second->closure = nullptr;
  return true;
}

gpr_timespec FuzzingEventEngine::GlobalNowImpl(gpr_clock_type clock_type) {
  if (g_fuzzing_event_engine == nullptr) {
    return gpr_inf_future(clock_type);
  }
  GPR_ASSERT(g_fuzzing_event_engine != nullptr);
  grpc_core::MutexLock lock(&*mu_);
  return g_fuzzing_event_engine->NowAsTimespec(clock_type);
}

void FuzzingEventEngine::UnsetGlobalHooks() {
  if (g_fuzzing_event_engine != this) return;
  g_fuzzing_event_engine = nullptr;
  gpr_now_impl = g_orig_gpr_now_impl;
  g_orig_gpr_now_impl = nullptr;
  grpc_set_pick_port_functions(grpc_pick_port_functions{
      +[]() -> int { grpc_core::Crash("not set"); },
      +[]() -> int { grpc_core::Crash("not set"); }, +[](int) {}});
}

FuzzingEventEngine::ListenerInfo::~ListenerInfo() {
  GPR_ASSERT(g_fuzzing_event_engine != nullptr);
  g_fuzzing_event_engine->Run(
      [on_shutdown = std::move(on_shutdown),
       shutdown_status = std::move(shutdown_status)]() mutable {
        on_shutdown(std::move(shutdown_status));
      });
}

}  // namespace experimental
}  // namespace grpc_event_engine
