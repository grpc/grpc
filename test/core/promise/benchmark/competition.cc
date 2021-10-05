// Copyright 2021 gRPC authors.
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

/* This benchmark exists to ensure that immediately-firing alarms are fast */

#include <benchmark/benchmark.h>

#include "absl/synchronization/mutex.h"

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "test/core/promise/benchmark/filter_stack.h"
#include "test/core/promise/test_wakeup_schedulers.h"

namespace filter_stack {

Filter passthrough_filter = {
    CallNextOp, NoCallData, NoCallData, NoChannelData, NoChannelData, 0, 0,
};

struct Interject {
  Closure c;
  Closure* next;

  static void Callback(void* p, absl::Status status) {
    auto* i = static_cast<Interject*>(p);
    i->next->Run(std::move(status));
  }

  static void Init(CallElem* elem) {
    auto* i = static_cast<Interject*>(elem->call_data);
    i->c.f = Callback;
    i->c.p = i;
  }

  static void Destroy(CallElem*) {}

  static void StartOp(CallElem* elem, Op* op) {
    auto* i = static_cast<Interject*>(elem->call_data);
    if (op->recv_initial_metadata) {
      i->next = op->on_complete;
      op->on_complete = &i->c;
    }
    CallNextOp(elem, op);
  }
};

Filter interject_filter = {
    Interject::StartOp,
    Interject::Init,
    Interject::Destroy,
    NoChannelData,
    NoChannelData,
    sizeof(Interject),
    0,
};

struct InterjectPipe {
  Closure c_init_metadata;
  Closure* next_init_metadata;
  Closure c_payload;
  Closure* next_payload;
  Closure c_trailing_metadata;
  Closure* next_trailing_metadata;

  static void CallbackInitMetadata(void* p, absl::Status status) {
    auto* i = static_cast<InterjectPipe*>(p);
    i->next_init_metadata->Run(std::move(status));
  }

  static void CallbackPayload(void* p, absl::Status status) {
    auto* i = static_cast<InterjectPipe*>(p);
    i->next_payload->Run(std::move(status));
  }

  static void CallbackTrailingMetadata(void* p, absl::Status status) {
    auto* i = static_cast<InterjectPipe*>(p);
    i->next_trailing_metadata->Run(std::move(status));
  }

  static void Init(CallElem* elem) {
    auto* i = static_cast<InterjectPipe*>(elem->call_data);
    i->c_init_metadata.f = CallbackInitMetadata;
    i->c_init_metadata.p = i;
    i->c_payload.f = CallbackPayload;
    i->c_payload.p = i;
    i->c_trailing_metadata.f = CallbackTrailingMetadata;
    i->c_trailing_metadata.p = i;
  }

  static void Destroy(CallElem*) {}

  static void StartOp(CallElem* elem, Op* op) {
    auto* i = static_cast<InterjectPipe*>(elem->call_data);
    if (op->recv_trailing_metadata) {
      i->next_trailing_metadata = op->on_complete;
      op->on_complete = &i->c_trailing_metadata;
    }
    if (op->recv_message) {
      i->next_payload = op->on_complete;
      op->on_complete = &i->c_payload;
    }
    if (op->recv_initial_metadata) {
      i->next_init_metadata = op->on_complete;
      op->on_complete = &i->c_init_metadata;
    }
    CallNextOp(elem, op);
  }
};

Filter interject_pipe = {
    InterjectPipe::StartOp,
    InterjectPipe::Init,
    InterjectPipe::Destroy,
    NoChannelData,
    NoChannelData,
    sizeof(InterjectPipe),
    0,
};

void EndOp(CallElem*, Op* op) { op->on_complete->Run(absl::OkStatus()); }

Filter end_filter = {EndOp,         NoCallData, NoCallData, NoChannelData,
                     NoChannelData, 0,          0};

static void unary(benchmark::State& state,
                  std::initializer_list<Filter*> filters) {
  auto* channel =
      MakeChannel(const_cast<Filter**>(&*filters.begin()), filters.size());
  for (auto _ : state) {
    auto* call = MakeCall(channel);
    Op op;
    Op::Payload payload;
    op.recv_initial_metadata = true;
    op.recv_message = true;
    op.recv_trailing_metadata = true;
    op.payload = &payload;
    Closure done = {call, +[](void* p, absl::Status status) {
                      if (!status.ok()) abort();
                      FreeCall(static_cast<CallStack*>(p));
                    }};
    op.on_complete = &done;
    RunOp(call, &op);
  }
  FreeChannel(channel);
}

static void BM_FilterStack_Passthrough3_Unary(benchmark::State& state) {
  unary(state, {&passthrough_filter, &passthrough_filter, &passthrough_filter,
                &end_filter});
}
BENCHMARK(BM_FilterStack_Passthrough3_Unary);

static void BM_FilterStack_Passthrough10_Unary(benchmark::State& state) {
  unary(state, {&passthrough_filter, &passthrough_filter, &passthrough_filter,
                &passthrough_filter, &passthrough_filter, &passthrough_filter,
                &passthrough_filter, &passthrough_filter, &passthrough_filter,
                &passthrough_filter, &end_filter});
}
BENCHMARK(BM_FilterStack_Passthrough10_Unary);

static void BM_FilterStack_Interject3_Unary(benchmark::State& state) {
  unary(state,
        {&interject_filter, &interject_filter, &interject_filter, &end_filter});
}
BENCHMARK(BM_FilterStack_Interject3_Unary);

static void BM_FilterStack_Interject10_Unary(benchmark::State& state) {
  unary(state, {&interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &end_filter});
}
BENCHMARK(BM_FilterStack_Interject10_Unary);

static void BM_FilterStack_Interject30_Unary(benchmark::State& state) {
  unary(state, {&interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &end_filter});
}
BENCHMARK(BM_FilterStack_Interject30_Unary);

static void BM_FilterStack_Interject3Pipe_Unary(benchmark::State& state) {
  unary(state,
        {&interject_pipe, &interject_pipe, &interject_pipe, &end_filter});
}
BENCHMARK(BM_FilterStack_Interject3Pipe_Unary);

static void BM_FilterStack_Interject10Pipe_Unary(benchmark::State& state) {
  unary(state,
        {&interject_pipe, &interject_pipe, &interject_pipe, &interject_pipe,
         &interject_pipe, &interject_pipe, &interject_pipe, &interject_pipe,
         &interject_pipe, &interject_pipe, &end_filter});
}
BENCHMARK(BM_FilterStack_Interject10Pipe_Unary);

static void BM_FilterStack_Interject30Pipe_Unary(benchmark::State& state) {
  unary(state,
        {&interject_pipe, &interject_pipe, &interject_pipe, &interject_pipe,
         &interject_pipe, &interject_pipe, &interject_pipe, &interject_pipe,
         &interject_pipe, &interject_pipe, &interject_pipe, &interject_pipe,
         &interject_pipe, &interject_pipe, &interject_pipe, &interject_pipe,
         &interject_pipe, &interject_pipe, &interject_pipe, &interject_pipe,
         &interject_pipe, &interject_pipe, &interject_pipe, &interject_pipe,
         &interject_pipe, &interject_pipe, &interject_pipe, &interject_pipe,
         &interject_pipe, &interject_pipe, &end_filter});
}
BENCHMARK(BM_FilterStack_Interject30Pipe_Unary);

}  // namespace filter_stack

namespace grpc_core {

namespace activity_stack {
struct RPCIO {
  Latch<int> recv_initial_metadata;
};

struct RPCP {
  Pipe<int> pipe;
};
}  // namespace activity_stack

template <>
struct ContextType<activity_stack::RPCIO> {};

template <>
struct ContextType<activity_stack::RPCP> {};

namespace activity_stack {

template <typename MakeCall>
static void unary(benchmark::State& state, MakeCall make_call) {
  printf("activity stack size: %d\n", static_cast<int>(make_call()->Size()));
  for (auto _ : state) {
    make_call();
  }
}

static void BM_ActivityStack_Passthrough3_Unary(benchmark::State& state) {
  unary(state, []() {
    return MakeActivity(
        []() {
          auto one = []() { return absl::OkStatus(); };
          return TrySeq(one, one, one);
        },
        NoWakeupScheduler(),
        [](absl::Status status) {
          if (!status.ok()) abort();
        });
  });
}
BENCHMARK(BM_ActivityStack_Passthrough3_Unary);

static void BM_ActivityStack_Passthrough10_Unary(benchmark::State& state) {
  unary(state, []() {
    return MakeActivity(
        []() {
          auto one = []() { return absl::OkStatus(); };
          return TrySeq(one, one, one, one, one, one, one, one, one, one);
        },
        NoWakeupScheduler(),
        [](absl::Status status) {
          if (!status.ok()) abort();
        });
  });
}
BENCHMARK(BM_ActivityStack_Passthrough10_Unary);

static void BM_ActivityStack_Interject3Latches_Unary(benchmark::State& state) {
  unary(state, []() {
    RPCIO rpcio;
    return MakeActivity(
        []() {
          auto one = []() {
            return GetContext<RPCIO>()->recv_initial_metadata.Wait();
          };
          return Seq(Join(one(), one(), one(),
                          []() {
                            GetContext<RPCIO>()->recv_initial_metadata.Set(42);
                            return true;
                          }),
                     []() { return absl::OkStatus(); });
        },
        NoWakeupScheduler(),
        [](absl::Status status) {
          if (!status.ok()) abort();
        },
        std::move(rpcio));
  });
}
BENCHMARK(BM_ActivityStack_Interject3Latches_Unary);

static void BM_ActivityStack_Interject10Latches_Unary(benchmark::State& state) {
  unary(state, []() {
    RPCIO rpcio;
    return MakeActivity(
        []() {
          auto one = []() {
            return GetContext<RPCIO>()->recv_initial_metadata.Wait();
          };
          return Seq(Join(one(), one(), one(), one(), one(), one(), one(),
                          one(), one(), one(),
                          []() {
                            GetContext<RPCIO>()->recv_initial_metadata.Set(42);
                            return true;
                          }),
                     []() { return absl::OkStatus(); });
        },
        NoWakeupScheduler(),
        [](absl::Status status) {
          if (!status.ok()) abort();
        },
        std::move(rpcio));
  });
}
BENCHMARK(BM_ActivityStack_Interject10Latches_Unary);

static void BM_ActivityStack_Interject30Latches_Unary(benchmark::State& state) {
  unary(state, []() {
    RPCIO rpcio;
    return MakeActivity(
        []() {
          auto one = []() {
            return GetContext<RPCIO>()->recv_initial_metadata.Wait();
          };
          return Seq(
              Join(one(), one(), one(), one(), one(), one(), one(), one(),
                   one(), one(), one(), one(), one(), one(), one(), one(),
                   one(), one(), one(), one(), one(), one(), one(), one(),
                   one(), one(), one(), one(), one(), one(),
                   []() {
                     GetContext<RPCIO>()->recv_initial_metadata.Set(42);
                     return true;
                   }),
              []() { return absl::OkStatus(); });
        },
        NoWakeupScheduler(),
        [](absl::Status status) {
          if (!status.ok()) abort();
        },
        std::move(rpcio));
  });
}
BENCHMARK(BM_ActivityStack_Interject30Latches_Unary);

static void BM_ActivityStack_Interject3Filters_Unary(benchmark::State& state) {
  unary(state, []() {
    RPCP rpcio;
    return MakeActivity(
        []() {
          auto one = []() {
            return GetContext<RPCP>()->pipe.sender.Filter(
                [](int i) { return absl::StatusOr<int>(i); });
          };
          return TryJoin(
              one(), one(), one(),
              Seq(
                  GetContext<RPCP>()->pipe.sender.Push(42),
                  []() { return GetContext<RPCP>()->pipe.sender.Push(43); },
                  []() { return GetContext<RPCP>()->pipe.sender.Push(44); },
                  []() {
                    auto x = std::move(GetContext<RPCP>()->pipe.sender);
                    return absl::OkStatus();
                  }),
              Seq(
                  GetContext<RPCP>()->pipe.receiver.Next(),
                  []() { return GetContext<RPCP>()->pipe.receiver.Next(); },
                  []() { return GetContext<RPCP>()->pipe.receiver.Next(); },
                  []() { return absl::OkStatus(); }));
        },
        NoWakeupScheduler(),
        [](absl::Status status) {
          if (!status.ok()) abort();
        },
        std::move(rpcio));
  });
}
BENCHMARK(BM_ActivityStack_Interject3Filters_Unary);

static void BM_ActivityStack_Interject10Filters_Unary(benchmark::State& state) {
  unary(state, []() {
    RPCP rpcio;
    return MakeActivity(
        []() {
          auto one = []() {
            return GetContext<RPCP>()->pipe.sender.Filter(
                [](int i) { return absl::StatusOr<int>(i); });
          };
          return TryJoin(
              one(), one(), one(), one(), one(), one(), one(), one(), one(),
              one(),
              Seq(
                  GetContext<RPCP>()->pipe.sender.Push(42),
                  []() { return GetContext<RPCP>()->pipe.sender.Push(43); },
                  []() { return GetContext<RPCP>()->pipe.sender.Push(44); },
                  []() {
                    auto x = std::move(GetContext<RPCP>()->pipe.sender);
                    return absl::OkStatus();
                  }),
              Seq(
                  GetContext<RPCP>()->pipe.receiver.Next(),
                  []() { return GetContext<RPCP>()->pipe.receiver.Next(); },
                  []() { return GetContext<RPCP>()->pipe.receiver.Next(); },
                  []() { return absl::OkStatus(); }));
        },
        NoWakeupScheduler(),
        [](absl::Status status) {
          if (!status.ok()) abort();
        },
        std::move(rpcio));
  });
}
BENCHMARK(BM_ActivityStack_Interject10Filters_Unary);

static void BM_ActivityStack_Interject30Filters_Unary(benchmark::State& state) {
  unary(state, []() {
    RPCP rpcio;
    return MakeActivity(
        []() {
          auto one = []() {
            return GetContext<RPCP>()->pipe.sender.Filter(
                [](int i) { return absl::StatusOr<int>(i); });
          };
          return TryJoin(
              one(), one(), one(), one(), one(), one(), one(), one(), one(),
              one(), one(), one(), one(), one(), one(), one(), one(), one(),
              one(), one(), one(), one(), one(), one(), one(), one(), one(),
              one(), one(), one(),
              Seq(
                  GetContext<RPCP>()->pipe.sender.Push(42),
                  []() { return GetContext<RPCP>()->pipe.sender.Push(43); },
                  []() { return GetContext<RPCP>()->pipe.sender.Push(44); },
                  []() {
                    auto x = std::move(GetContext<RPCP>()->pipe.sender);
                    return absl::OkStatus();
                  }),
              Seq(
                  GetContext<RPCP>()->pipe.receiver.Next(),
                  []() { return GetContext<RPCP>()->pipe.receiver.Next(); },
                  []() { return GetContext<RPCP>()->pipe.receiver.Next(); },
                  []() { return absl::OkStatus(); }));
        },
        NoWakeupScheduler(),
        [](absl::Status status) {
          if (!status.ok()) abort();
        },
        std::move(rpcio));
  });
}
BENCHMARK(BM_ActivityStack_Interject30Filters_Unary);

}  // namespace activity_stack
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
