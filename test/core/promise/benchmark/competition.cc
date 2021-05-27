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
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "test/core/promise/benchmark/filter_stack.h"

namespace filter_stack {

Filter passthrough_filter = {
    CallNextOp, NoCallData, NoChannelData, 0, 0,
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

  static void StartOp(CallElem* elem, Op* op) {
    auto* i = static_cast<Interject*>(elem->call_data);
    if (op->send_initial_metadata) {
      i->next = op->on_complete;
    }
  }
};

Filter interject_filter = {
    Interject::StartOp, Interject::Init, NoChannelData, sizeof(Interject), 0,
};

void EndOp(CallElem* elem, Op* op) { op->on_complete->Run(absl::OkStatus()); }

Filter end_filter = {EndOp, NoCallData, NoChannelData, 0, 0};

static void unary(benchmark::State& state,
                  std::initializer_list<Filter*> filters) {
  auto* channel =
      MakeChannel(const_cast<Filter**>(&*filters.begin()), filters.size());
  for (auto _ : state) {
    auto* call = MakeCall(channel);
    Op op;
    Op::Payload payload;
    op.send_initial_metadata = true;
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

static void BM_FilterStack_Interject10_Unary(benchmark::State& state) {
  unary(state, {&interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &interject_filter, &interject_filter,
                &interject_filter, &end_filter});
}
BENCHMARK(BM_FilterStack_Interject10_Unary);

}  // namespace filter_stack

namespace grpc_core {

namespace activity_stack {
struct RPCIO {
  Pipe<int> recv_pipe;
};
}  // namespace activity_stack

template <>
struct ContextType<activity_stack::RPCIO> {};

namespace activity_stack {

static void unary(
    benchmark::State& state,
    std::function<ActivityPtr(std::function<void(absl::Status)>)> make_call) {
  printf("activity stack size: %d\n",
         (int)make_call([](absl::Status) {})->Size());
  for (auto _ : state) {
    make_call([](absl::Status status) {
      if (!status.ok()) abort();
    });
  }
}

static void BM_ActivityStack_Passthrough3_Unary(benchmark::State& state) {
  unary(state, [](std::function<void(absl::Status)> on_done) {
    return MakeActivity(
        []() {
          auto one = []() { return ready(absl::OkStatus()); };
          return TrySeq(one, one, one);
        },
        std::move(on_done), nullptr);
  });
}
BENCHMARK(BM_ActivityStack_Passthrough3_Unary);

static void BM_ActivityStack_Passthrough10_Unary(benchmark::State& state) {
  unary(state, [](std::function<void(absl::Status)> on_done) {
    return MakeActivity(
        []() {
          auto one = []() { return ready(absl::OkStatus()); };
          return TrySeq(one, one, one, one, one, one, one, one, one, one);
        },
        std::move(on_done), nullptr);
  });
}
BENCHMARK(BM_ActivityStack_Passthrough10_Unary);

static void BM_ActivityStack_Interject10_Unary(benchmark::State& state) {
  unary(state, [](std::function<void(absl::Status)> on_done) {
    RPCIO rpcio;
    return MakeActivity(
        []() {
          auto one = []() {
            auto* io = GetContext<RPCIO>();
            Pipe<int> interjection;
            std::swap(interjection.sender, io->recv_pipe.sender);
            return ForEach(std::move(interjection.receiver),
                           Capture(
                               [](PipeSender<int>* sender, int i) {
                                 return Seq(sender->Push(i), []() {
                                   return ready(absl::OkStatus());
                                 });
                               },
                               std::move(interjection.sender)));
          };
          return Seq(
              Join(GetContext<RPCIO>()->recv_pipe.receiver.Next(), one(), one(),
                   one(), one(), one(), one(), one(), one(), one(), one(),
                   GetContext<RPCIO>()->recv_pipe.sender.Push(42)),
              []() { return ready(absl::OkStatus()); });
        },
        std::move(on_done), nullptr, std::move(rpcio));
  });
}
BENCHMARK(BM_ActivityStack_Interject10_Unary);

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
