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

#include <grpc/event_engine/slice.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <vector>

#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/extensions/blocking_dns.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/telemetry/stats.h"
#include "src/core/util/dump_args.h"
#include "src/core/util/time.h"
#include "src/core/util/useful.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/test_util/port.h"

#if defined(GRPC_POSIX_SOCKET_TCP)
#include "src/core/lib/event_engine/posix_engine/native_posix_dns_resolver.h"
#else
#include "src/core/util/crash.h"
#endif
// IWYU pragma: no_include <sys/socket.h>

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

using namespace std::chrono_literals;

namespace grpc_event_engine {
namespace experimental {

namespace {

constexpr EventEngine::Duration kOneYear = 8760h;

// Inside the fuzzing event engine we consider everything is bound to a single
// loopback device. It cannot reach any other devices, and shares all ports
// between ipv4 and ipv6.

EventEngine::ResolvedAddress PortToAddress(int port) {
  return URIToResolvedAddress(absl::StrCat("ipv4:127.0.0.1:", port)).value();
}

}  // namespace

grpc_core::NoDestruct<grpc_core::Mutex> FuzzingEventEngine::mu_;
grpc_core::NoDestruct<grpc_core::Mutex> FuzzingEventEngine::now_mu_;

namespace {
const intptr_t kTaskHandleSalt = 12345;
FuzzingEventEngine* g_fuzzing_event_engine = nullptr;
gpr_timespec (*g_orig_gpr_now_impl)(gpr_clock_type clock_type);
}  // namespace

FuzzingEventEngine::FuzzingEventEngine(
    Options options, const fuzzing_event_engine::Actions& actions)
    : max_delay_{options.max_delay_write, options.max_delay_run_after} {
  // Allow the fuzzer to assign ports.
  // Once this list is exhausted, we fall back to a deterministic algorithm.
  for (auto port : actions.assign_ports()) {
    if (port == 0 || port > 65535) continue;
    free_ports_.push(port);
    fuzzer_mentioned_ports_.insert(port);
  }

  // Fill the write sizes queue for future connections.
  for (const auto& connection : actions.connections()) {
    std::queue<size_t> write_sizes;
    for (auto size : connection.write_size()) {
      write_sizes.push(size);
    }
    write_sizes_for_future_connections_.emplace(std::move(write_sizes));
  }

  // Whilst a fuzzing EventEngine is active we override grpc's now function.
  g_orig_gpr_now_impl = gpr_now_impl;
  gpr_now_impl = GlobalNowImpl;
  CHECK_EQ(g_fuzzing_event_engine, nullptr);
  g_fuzzing_event_engine = this;
  grpc_core::TestOnlySetProcessEpoch(NowAsTimespec(GPR_CLOCK_MONOTONIC));

  for (const auto& delay_ns : actions.run_delay()) {
    Duration delay = std::chrono::nanoseconds(delay_ns);
    task_delays_.push(delay);
  }

  previous_pick_port_functions_ = grpc_set_pick_port_functions(
      grpc_pick_port_functions{+[]() -> int {
                                 grpc_core::MutexLock lock(&*mu_);
                                 return g_fuzzing_event_engine->AllocatePort();
                               },
                               +[](int) {}});
}

void FuzzingEventEngine::FuzzingDone() {
  grpc_core::MutexLock lock(&*mu_);
  while (!task_delays_.empty()) task_delays_.pop();
}

gpr_timespec FuzzingEventEngine::NowAsTimespec(gpr_clock_type clock_type) {
  // TODO(ctiller): add a facility to track realtime and monotonic clocks
  // separately to simulate divergence.
  CHECK(clock_type != GPR_TIMESPAN);
  const Duration d = now_.time_since_epoch();
  auto secs = std::chrono::duration_cast<std::chrono::seconds>(d);
  return {secs.count(), static_cast<int32_t>((d - secs).count()), clock_type};
}

void FuzzingEventEngine::Tick(Duration max_time) {
  if (IsSaneTimerEnvironment()) {
    std::vector<absl::AnyInvocable<void()>> to_run;
    Duration incr = max_time;
    DCHECK_GT(incr.count(), Duration::zero().count());
    {
      grpc_core::MutexLock lock(&*mu_);
      grpc_core::MutexLock now_lock(&*now_mu_);
      if (!tasks_by_time_.empty()) {
        incr = std::min(incr, tasks_by_time_.begin()->first - now_);
      }
      const auto max_incr =
          std::numeric_limits<
              decltype(now_.time_since_epoch().count())>::max() -
          now_.time_since_epoch().count();
      CHECK_GE(max_incr, 0u);
      incr = std::max(Duration::zero(), incr);
      incr = std::min(incr, Duration(max_incr));
      GRPC_TRACE_LOG(fuzzing_ee_timers, INFO)
          << "Tick "
          << GRPC_DUMP_ARGS(now_.time_since_epoch().count(), incr.count(),
                            max_incr);
      if (!tasks_by_time_.empty()) {
        GRPC_TRACE_LOG(fuzzing_ee_timers, INFO)
            << "first time: "
            << tasks_by_time_.begin()->first.time_since_epoch().count();
      }
      now_ += incr;
      CHECK_GE(now_.time_since_epoch().count(), 0);
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
    OnClockIncremented(incr);
    if (to_run.empty()) return;
    for (auto& closure : to_run) {
      closure();
    }
  } else {
    bool incremented_time = false;
    while (true) {
      std::vector<absl::AnyInvocable<void()>> to_run;
      Duration incr = Duration::zero();
      {
        grpc_core::MutexLock lock(&*mu_);
        grpc_core::MutexLock now_lock(&*now_mu_);
        if (!incremented_time) {
          incr = max_time;
          // TODO(ctiller): look at tasks_by_time_ and jump forward (once iomgr
          // timers are gone)
          if (!tasks_by_time_.empty()) {
            incr = std::min(incr, tasks_by_time_.begin()->first - now_);
          }
          if (incr < exponential_gate_time_increment_) {
            exponential_gate_time_increment_ = std::chrono::milliseconds(1);
          } else {
            incr = std::min(incr, exponential_gate_time_increment_);
            exponential_gate_time_increment_ +=
                exponential_gate_time_increment_ / 1000;
          }
          incr = std::max(incr, std::chrono::duration_cast<Duration>(
                                    std::chrono::milliseconds(1)));
          now_ += incr;
          CHECK_GE(now_.time_since_epoch().count(), 0);
          ++current_tick_;
          incremented_time = true;
        }
        // Find newly expired timers.
        while (!tasks_by_time_.empty() &&
               tasks_by_time_.begin()->first <= now_) {
          auto& task = *tasks_by_time_.begin()->second;
          tasks_by_id_.erase(task.id);
          if (task.closure != nullptr) {
            to_run.push_back(std::move(task.closure));
          }
          tasks_by_time_.erase(tasks_by_time_.begin());
        }
      }
      OnClockIncremented(incr);
      if (to_run.empty()) return;
      for (auto& closure : to_run) {
        closure();
      }
    }
  }
}

void FuzzingEventEngine::TickUntilIdle() {
  while (true) {
    {
      grpc_core::MutexLock lock(&*mu_);
      LOG_EVERY_N_SEC(INFO, 5)
          << "TickUntilIdle: "
          << GRPC_DUMP_ARGS(tasks_by_id_.size(), outstanding_reads_.load(),
                            outstanding_writes_.load());
      if (IsIdleLocked()) return;
    }
    Tick();
  }
}

bool FuzzingEventEngine::IsIdle() {
  grpc_core::MutexLock lock(&*mu_);
  return IsIdleLocked();
}

bool FuzzingEventEngine::IsIdleLocked() {
  return tasks_by_id_.empty() &&
         outstanding_writes_.load(std::memory_order_relaxed) == 0 &&
         outstanding_reads_.load(std::memory_order_relaxed) == 0;
}

void FuzzingEventEngine::TickUntil(Time t) {
  while (true) {
    auto now = Now();
    if (now >= t) break;
    Tick(t - now);
  }
}

void FuzzingEventEngine::TickForDuration(Duration d) { TickUntil(Now() + d); }

void FuzzingEventEngine::SetRunAfterDurationCallback(
    absl::AnyInvocable<void(Duration)> callback) {
  grpc_core::MutexLock lock(&run_after_duration_callback_mu_);
  run_after_duration_callback_ = std::move(callback);
}

FuzzingEventEngine::Time FuzzingEventEngine::Now() {
  grpc_core::MutexLock lock(&*now_mu_);
  return now_;
}

int FuzzingEventEngine::AllocatePort() {
  // If the fuzzer selected some port orderings, do that first.
  if (!free_ports_.empty()) {
    int p = free_ports_.front();
    free_ports_.pop();
    return p;
  }
  // Otherwise just scan through starting at one and skipping any ports
  // that were in the fuzzers initial list.
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
  // Create a listener and register it into the set of listener info in the
  // event engine.
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
  // Return true if a port is bound to a listener.
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
  // Extract the port from the address (or fail if non-localhost).
  auto port = ResolvedAddressGetPort(addr);
  grpc_core::MutexLock lock(&*mu_);
  // Check that the listener hasn't already been started.
  if (info_->started) return absl::InternalError("Already started");
  if (port != 0) {
    // If the port is non-zero, check that it's not already in use.
    if (g_fuzzing_event_engine->IsPortUsed(port)) {
      return absl::InternalError("Port in use");
    }
  } else {
    // If the port is zero, allocate a new one.
    do {
      port = g_fuzzing_event_engine->AllocatePort();
    } while (g_fuzzing_event_engine->IsPortUsed(port));
  }
  // Add the port to the listener.
  info_->ports.push_back(port);
  return port;
}

absl::Status FuzzingEventEngine::FuzzingListener::Start() {
  // Start the listener or fail if it's already started.
  grpc_core::MutexLock lock(&*mu_);
  if (info_->started) return absl::InternalError("Already started");
  info_->started = true;
  return absl::OkStatus();
}

bool FuzzingEventEngine::EndpointMiddle::Write(SliceBuffer* data, int index) {
  CHECK(!closed[index]);
  const int peer_index = 1 - index;
  GRPC_TRACE_LOG(fuzzing_ee_writes, INFO)
      << "WRITE[" << this << ":" << index << "]: entry "
      << GRPC_DUMP_ARGS(data->Length());
  if (data->Length() == 0) return true;
  size_t write_len = std::numeric_limits<size_t>::max();
  // Check the write_sizes queue for fuzzer imposed restrictions on this write
  // size. This allows the fuzzer to force small writes to be seen by the
  // reader.
  if (!write_sizes[index].empty()) {
    write_len = write_sizes[index].front();
    write_sizes[index].pop();
  }
  if (write_len > data->Length()) {
    write_len = data->Length();
  }
  // If the write_len is zero, we still need to write something, so we write one
  // byte.
  if (write_len == 0) write_len = 1;
  GRPC_TRACE_LOG(fuzzing_ee_writes, INFO)
      << "WRITE[" << this << ":" << index << "]: " << write_len << " bytes; "
      << GRPC_DUMP_ARGS(pending_read[peer_index].has_value());
  // Expand the pending buffer.
  size_t prev_len = pending[index].size();
  pending[index].resize(prev_len + write_len);
  // Move bytes from the to-write data into the pending buffer.
  data->MoveFirstNBytesIntoBuffer(write_len, pending[index].data() + prev_len);
  GRPC_TRACE_LOG(fuzzing_ee_writes, INFO)
      << "WRITE[" << this << ":" << index << "]: post-move "
      << GRPC_DUMP_ARGS(data->Length());
  // If there was a pending read, then we can fulfill it.
  if (pending_read[peer_index].has_value()) {
    pending_read[peer_index]->buffer->Append(
        Slice::FromCopiedBuffer(pending[index]));
    pending[index].clear();
    g_fuzzing_event_engine->RunLocked(
        RunType::kWrite,
        [cb = std::move(pending_read[peer_index]->on_read), this, peer_index,
         buffer = pending_read[peer_index]->buffer]() mutable {
          GRPC_TRACE_LOG(fuzzing_ee_writes, INFO)
              << "FINISH_READ[" << this << ":" << peer_index
              << "]: " << GRPC_DUMP_ARGS(buffer->Length());
          cb(absl::OkStatus());
        });
    pending_read[peer_index].reset();
  }
  return data->Length() == 0;
}

bool FuzzingEventEngine::FuzzingEndpoint::Write(
    absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data,
    const WriteArgs*) {
  grpc_core::global_stats().IncrementSyscallWrite();
  grpc_core::MutexLock lock(&*mu_);
  IoToken write_token({"WRITE", middle_.get(), my_index(),
                       &g_fuzzing_event_engine->outstanding_writes_});
  CHECK(!middle_->closed[my_index()]);
  CHECK(!middle_->writing[my_index()]);
  // If the write succeeds immediately, then we return true.
  if (middle_->Write(data, my_index())) return true;
  middle_->writing[my_index()] = true;
  ScheduleDelayedWrite(middle_, my_index(), std::move(on_writable), data,
                       std::move(write_token));
  return false;
}

void FuzzingEventEngine::FuzzingEndpoint::ScheduleDelayedWrite(
    std::shared_ptr<EndpointMiddle> middle, int index,
    absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data,
    IoToken write_token) {
  g_fuzzing_event_engine->RunLocked(
      RunType::kWrite,
      [write_token = std::move(write_token), middle = std::move(middle), index,
       data, on_writable = std::move(on_writable)]() mutable {
        grpc_core::ReleasableMutexLock lock(&*mu_);
        CHECK(middle->writing[index]);
        if (middle->closed[index]) {
          GRPC_TRACE_LOG(fuzzing_ee_writes, INFO)
              << "CLOSED[" << middle.get() << ":" << index << "]";
          g_fuzzing_event_engine->RunLocked(
              RunType::kRunAfter,
              [on_writable = std::move(on_writable)]() mutable {
                on_writable(absl::InternalError("Endpoint closed"));
              });
          if (middle->pending_read[1 - index].has_value()) {
            g_fuzzing_event_engine->RunLocked(
                RunType::kRunAfter,
                [cb = std::move(
                     middle->pending_read[1 - index]->on_read)]() mutable {
                  cb(absl::InternalError("Endpoint closed"));
                });
            middle->pending_read[1 - index].reset();
          }
          return;
        }
        if (middle->Write(data, index)) {
          middle->writing[index] = false;
          lock.Release();
          on_writable(absl::OkStatus());
          return;
        }
        ScheduleDelayedWrite(std::move(middle), index, std::move(on_writable),
                             data, std::move(write_token));
      });
}

FuzzingEventEngine::FuzzingEndpoint::~FuzzingEndpoint() {
  grpc_core::MutexLock lock(&*mu_);
  GRPC_TRACE_LOG(fuzzing_ee_writes, INFO)
      << "CLOSE[" << middle_.get() << ":" << my_index() << "]: "
      << GRPC_DUMP_ARGS(
             middle_->closed[my_index()], middle_->closed[peer_index()],
             middle_->pending_read[my_index()].has_value(),
             middle_->pending_read[peer_index()].has_value(),
             middle_->writing[my_index()], middle_->writing[peer_index()]);
  middle_->closed[my_index()] = true;
  if (middle_->pending_read[my_index()].has_value()) {
    GRPC_TRACE_LOG(fuzzing_ee_writes, INFO)
        << "CLOSED_READING[" << middle_.get() << ":" << my_index() << "]";
    g_fuzzing_event_engine->RunLocked(
        RunType::kRunAfter,
        [cb = std::move(middle_->pending_read[my_index()]->on_read)]() mutable {
          cb(absl::InternalError("Endpoint closed"));
        });
    middle_->pending_read[my_index()].reset();
  }
  if (!middle_->writing[my_index()] &&
      middle_->pending_read[peer_index()].has_value()) {
    g_fuzzing_event_engine->RunLocked(
        RunType::kRunAfter,
        [cb = std::move(
             middle_->pending_read[peer_index()]->on_read)]() mutable {
          cb(absl::InternalError("Endpoint closed"));
        });
    middle_->pending_read[peer_index()].reset();
  }
}

bool FuzzingEventEngine::FuzzingEndpoint::Read(
    absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
    const ReadArgs*) {
  buffer->Clear();
  grpc_core::MutexLock lock(&*mu_);
  IoToken read_token({"READ", middle_.get(), my_index(),
                      &g_fuzzing_event_engine->outstanding_reads_});
  CHECK(!middle_->closed[my_index()]);
  if (middle_->pending[peer_index()].empty()) {
    // If the endpoint is closed, fail asynchronously.
    if (middle_->closed[peer_index()]) {
      g_fuzzing_event_engine->RunLocked(
          RunType::kRunAfter, [read_token = std::move(read_token),
                               on_read = std::move(on_read)]() mutable {
            on_read(absl::InternalError("Endpoint closed"));
          });
      return false;
    }
    // If the endpoint has no pending data, then we need to wait for a write.
    middle_->pending_read[my_index()] =
        PendingRead{std::move(read_token), std::move(on_read), buffer};
    return false;
  } else {
    // If the endpoint has pending data, then we can fulfill the read
    // immediately.
    buffer->Append(Slice::FromCopiedBuffer(middle_->pending[peer_index()]));
    middle_->pending[peer_index()].clear();
    return true;
  }
}

std::queue<size_t> FuzzingEventEngine::WriteSizesForConnection() {
  if (write_sizes_for_future_connections_.empty()) return std::queue<size_t>();
  auto ret = std::move(write_sizes_for_future_connections_.front());
  write_sizes_for_future_connections_.pop();
  return ret;
}

FuzzingEventEngine::EndpointMiddle::EndpointMiddle(int listener_port,
                                                   int client_port)
    : addrs{PortToAddress(listener_port), PortToAddress(client_port)},
      write_sizes{g_fuzzing_event_engine->WriteSizesForConnection(),
                  g_fuzzing_event_engine->WriteSizesForConnection()} {}

EventEngine::ConnectionHandle FuzzingEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig&, MemoryAllocator, Duration) {
  // TODO(ctiller): do something with the timeout
  // Schedule a timer to run (with some fuzzer selected delay) the on_connect
  // callback.
  grpc_core::MutexLock lock(&*mu_);
  auto task_handle = RunAfterLocked(
      RunType::kRunAfter, Duration(0),
      [this, addr, on_connect = std::move(on_connect)]() mutable {
        // Check for a legal address and extract the target port number.
        auto port = ResolvedAddressGetPort(addr);
        grpc_core::MutexLock lock(&*mu_);
        // Find the listener that is listening on the target port.
        for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
          const auto& listener = *it;
          // Listener must be started.
          if (!listener->started) continue;
          for (int listener_port : listener->ports) {
            if (port == listener_port) {
              // Port matches on a started listener: create an endpoint, call
              // on_accept for the listener and on_connect for the client.
              auto middle = std::make_shared<EndpointMiddle>(
                  listener_port, g_fuzzing_event_engine->AllocatePort());
              auto ep1 = std::make_unique<FuzzingEndpoint>(middle, 0);
              auto ep2 = std::make_unique<FuzzingEndpoint>(middle, 1);
              RunLocked(RunType::kRunAfter, [listener,
                                             ep1 = std::move(ep1)]() mutable {
                listener->on_accept(
                    std::move(ep1),
                    listener->memory_allocator_factory->CreateMemoryAllocator(
                        "fuzzing"));
              });
              RunLocked(RunType::kRunAfter, [on_connect = std::move(on_connect),
                                             ep2 = std::move(ep2)]() mutable {
                on_connect(std::move(ep2));
              });
              return;
            }
          }
        }
        // Fail: no such listener.
        RunLocked(RunType::kRunAfter,
                  [on_connect = std::move(on_connect)]() mutable {
                    on_connect(absl::InvalidArgumentError("No listener found"));
                  });
      });
  return ConnectionHandle{{task_handle.keys[0], task_handle.keys[1]}};
}

std::pair<std::unique_ptr<EventEngine::Endpoint>,
          std::unique_ptr<EventEngine::Endpoint>>
FuzzingEventEngine::CreateEndpointPair() {
  grpc_core::MutexLock lock(&*mu_);
  auto middle =
      std::make_shared<EndpointMiddle>(g_fuzzing_event_engine->AllocatePort(),
                                       g_fuzzing_event_engine->AllocatePort());
  auto ep1 = std::make_unique<FuzzingEndpoint>(middle, 0);
  auto ep2 = std::make_unique<FuzzingEndpoint>(middle, 1);
  return {std::move(ep1), std::move(ep2)};
}

bool FuzzingEventEngine::CancelConnect(ConnectionHandle connection_handle) {
  return Cancel(
      TaskHandle{{connection_handle.keys[0], connection_handle.keys[1]}});
}

bool FuzzingEventEngine::IsWorkerThread() { abort(); }

namespace {

class FuzzerDNSResolver : public ExtendedType<EventEngine::DNSResolver,
                                              ResolverSupportsBlockingLookups> {
 public:
  explicit FuzzerDNSResolver(std::shared_ptr<EventEngine> engine)
      : engine_(std::move(engine)) {}

  void LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name,
                      absl::string_view /* default_port */) override {
    GetDefaultEventEngine()->RunAfter(
        grpc_core::Duration::Seconds(1),
        [name = std::string(name), cb = std::move(on_resolve)]() mutable {
          cb(GetHostnameResponse(name));
        });
  }

  void LookupSRV(LookupSRVCallback on_resolve,
                 absl::string_view /* name */) override {
    engine_->Run([on_resolve = std::move(on_resolve)]() mutable {
      on_resolve(absl::UnimplementedError(
          "The Fuzzing DNS resolver does not support looking up SRV records"));
    });
  };

  void LookupTXT(LookupTXTCallback on_resolve,
                 absl::string_view /* name */) override {
    // Not supported
    engine_->Run([on_resolve = std::move(on_resolve)]() mutable {
      on_resolve(absl::UnimplementedError(
          "The Fuzing DNS resolver does not support looking up TXT records"));
    });
  };

  // Blocking resolution
  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>>
  LookupHostnameBlocking(absl::string_view name,
                         absl::string_view /* default_port */) override {
    return GetHostnameResponse(name);
  }

  absl::StatusOr<std::vector<EventEngine::DNSResolver::SRVRecord>>
  LookupSRVBlocking(absl::string_view /* name */) override {
    return absl::UnimplementedError(
        "The Fuzing DNS resolver does not support looking up TXT records");
  }

  absl::StatusOr<std::vector<std::string>> LookupTXTBlocking(
      absl::string_view /* name */) override {
    return absl::UnimplementedError(
        "The Fuzing DNS resolver does not support looking up TXT records");
  }

 private:
  static absl::StatusOr<std::vector<EventEngine::ResolvedAddress>>
  GetHostnameResponse(absl::string_view name) {
    if (name == "server") {
      return {{EventEngine::ResolvedAddress()}};
    }
    return absl::UnknownError("Resolution failed");
  }

  std::shared_ptr<EventEngine> engine_;
};
}  // namespace

absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>>
FuzzingEventEngine::GetDNSResolver(const DNSResolver::ResolverOptions&) {
#if defined(GRPC_POSIX_SOCKET_TCP)
  if (grpc_core::IsEventEngineDnsNonClientChannelEnabled()) {
    return std::make_unique<FuzzerDNSResolver>(shared_from_this());
  }
  return std::make_unique<NativePosixDNSResolver>(shared_from_this());
#else
  grpc_core::Crash("FuzzingEventEngine::GetDNSResolver Not implemented");
#endif
}

void FuzzingEventEngine::Run(Closure* closure) {
  grpc_core::MutexLock lock(&*mu_);
  RunAfterLocked(RunType::kRunAfter, Duration::zero(),
                 [closure]() { closure->Run(); });
}

void FuzzingEventEngine::Run(absl::AnyInvocable<void()> closure) {
  grpc_core::MutexLock lock(&*mu_);
  RunAfterLocked(RunType::kRunAfter, Duration::zero(), std::move(closure));
}

EventEngine::TaskHandle FuzzingEventEngine::RunAfter(Duration when,
                                                     Closure* closure) {
  return RunAfter(when, [closure]() { closure->Run(); });
}

EventEngine::TaskHandle FuzzingEventEngine::RunAfter(
    Duration when, absl::AnyInvocable<void()> closure) {
  {
    grpc_core::MutexLock lock(&run_after_duration_callback_mu_);
    if (run_after_duration_callback_ != nullptr) {
      run_after_duration_callback_(when);
    }
  }
  grpc_core::MutexLock lock(&*mu_);
  // (b/258949216): Cap it to one year to avoid integer overflow errors.
  return RunAfterLocked(RunType::kRunAfter, std::min(when, kOneYear),
                        std::move(closure));
}

EventEngine::TaskHandle FuzzingEventEngine::RunAfterExactly(
    Duration when, absl::AnyInvocable<void()> closure) {
  grpc_core::MutexLock lock(&*mu_);
  // (b/258949216): Cap it to one year to avoid integer overflow errors.
  return RunAfterLocked(RunType::kExact, std::min(when, kOneYear),
                        std::move(closure));
}

EventEngine::TaskHandle FuzzingEventEngine::RunAfterLocked(
    RunType run_type, Duration when, absl::AnyInvocable<void()> closure) {
  const intptr_t id = next_task_id_;
  ++next_task_id_;
  Duration delay_taken = Duration::zero();
  when = std::max(when, Duration::zero());
  if (run_type != RunType::kExact) {
    if (!task_delays_.empty()) {
      delay_taken = grpc_core::Clamp(task_delays_.front(), Duration::zero(),
                                     max_delay_[static_cast<int>(run_type)]);
      task_delays_.pop();
    } else if (run_type != RunType::kWrite && when == Duration::zero()) {
      // For zero-duration events, if there is no more delay input from
      // the test case, we default to a small non-zero value to avoid
      // busy loops that prevent us from making forward progress.
      delay_taken = std::chrono::microseconds(1);
    }
    when += delay_taken;
  }
  auto task = std::make_shared<Task>(id, std::move(closure));
  tasks_by_id_.emplace(id, task);
  Time final_time;
  Time now;
  {
    grpc_core::MutexLock lock(&*now_mu_);
    final_time = now_ + when;
    now = now_;
    tasks_by_time_.emplace(final_time, std::move(task));
  }
  GRPC_TRACE_LOG(fuzzing_ee_timers, INFO)
      << "Schedule timer " << id << " @ "
      << static_cast<uint64_t>(final_time.time_since_epoch().count())
      << " (now=" << now.time_since_epoch().count()
      << "; delay=" << when.count() << "; fuzzing_added=" << delay_taken.count()
      << "; type=" << static_cast<int>(run_type) << ")";
  return TaskHandle{id, kTaskHandleSalt};
}

bool FuzzingEventEngine::Cancel(TaskHandle handle) {
  grpc_core::MutexLock lock(&*mu_);
  CHECK(handle.keys[1] == kTaskHandleSalt);
  const intptr_t id = handle.keys[0];
  auto it = tasks_by_id_.find(id);
  if (it == tasks_by_id_.end()) {
    return false;
  }
  if (it->second->closure == nullptr) {
    return false;
  }
  GRPC_TRACE_LOG(fuzzing_ee_timers, INFO) << "Cancel timer " << id;
  it->second->closure = nullptr;
  return true;
}

gpr_timespec FuzzingEventEngine::GlobalNowImpl(gpr_clock_type clock_type) {
  if (g_fuzzing_event_engine == nullptr) {
    return gpr_inf_future(clock_type);
  }
  CHECK_NE(g_fuzzing_event_engine, nullptr);
  grpc_core::MutexLock lock(&*now_mu_);
  return g_fuzzing_event_engine->NowAsTimespec(clock_type);
}

void FuzzingEventEngine::UnsetGlobalHooks() {
  if (g_fuzzing_event_engine != this) return;
  g_fuzzing_event_engine = nullptr;
  gpr_now_impl = g_orig_gpr_now_impl;
  g_orig_gpr_now_impl = nullptr;
  grpc_set_pick_port_functions(previous_pick_port_functions_);
}

FuzzingEventEngine::ListenerInfo::~ListenerInfo() {
  CHECK_NE(g_fuzzing_event_engine, nullptr);
  g_fuzzing_event_engine->Run(
      [on_shutdown = std::move(on_shutdown),
       shutdown_status = std::move(shutdown_status)]() mutable {
        on_shutdown(std::move(shutdown_status));
      });
}

}  // namespace experimental
}  // namespace grpc_event_engine
