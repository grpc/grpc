// Copyright 2024 gRPC authors.
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

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/promise/all_ok.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/client_call.h"
#include "src/core/lib/transport/call_arena_allocator.h"

namespace grpc_core {
namespace {

class TestCallDestination : public UnstartedCallDestination {
 public:
  void StartCall(UnstartedCallHandler handler) override {
    handler_ = std::move(handler);
  }

  UnstartedCallHandler TakeHandler() {
    CHECK(handler_.has_value());
    auto handler = std::move(*handler_);
    handler_.reset();
    return handler;
  }

  void Orphaned() override { handler_.reset(); }

 private:
  std::optional<UnstartedCallHandler> handler_;
};

class Helper {
 public:
  ~Helper() {
    grpc_completion_queue_shutdown(cq_);
    auto ev = grpc_completion_queue_next(
        cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    CHECK_EQ(ev.type, GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cq_);
  }

  auto MakeCall() {
    auto arena = arena_allocator_->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    return std::unique_ptr<grpc_call, void (*)(grpc_call*)>(
        MakeClientCall(nullptr, 0, cq_, path_.Copy(), std::nullopt, true,
                       Timestamp::InfFuture(), compression_options_,
                       std::move(arena), destination_),
        grpc_call_unref);
  }

  UnstartedCallHandler TakeHandler() { return destination_->TakeHandler(); }

  grpc_completion_queue* cq() { return cq_; }

 private:
  grpc_completion_queue* cq_ = grpc_completion_queue_create_for_next(nullptr);
  Slice path_ = Slice::FromStaticString("/foo/bar");
  const grpc_compression_options compression_options_ = {
      1,
      {0, GRPC_COMPRESS_LEVEL_NONE},
      {0, GRPC_COMPRESS_NONE},
  };
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
  RefCountedPtr<CallArenaAllocator> arena_allocator_ =
      MakeRefCounted<CallArenaAllocator>(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "test-allocator"),
          1024);
  RefCountedPtr<TestCallDestination> destination_ =
      MakeRefCounted<TestCallDestination>();
};

void BM_CreateDestroy(benchmark::State& state) {
  Helper helper;
  for (auto _ : state) {
    helper.MakeCall();
  }
}
BENCHMARK(BM_CreateDestroy);

void BM_Unary(benchmark::State& state) {
  Helper helper;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("hello");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_status_code status;
  grpc_slice status_details;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  Slice response_payload = Slice::FromStaticString("world");
  grpc_byte_buffer* recv_response_payload = nullptr;
  for (auto _ : state) {
    auto call = helper.MakeCall();
    // Create ops the old school way to avoid any overheads
    grpc_op ops[6];
    memset(ops, 0, sizeof(ops));
    grpc_metadata_array_init(&initial_metadata_recv);
    grpc_metadata_array_init(&trailing_metadata_recv);
    ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
    ops[0].data.send_initial_metadata.count = 0;
    ops[1].op = GRPC_OP_SEND_MESSAGE;
    ops[1].data.send_message.send_message = request_payload;
    ops[2].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    ops[3].op = GRPC_OP_RECV_INITIAL_METADATA;
    ops[3].data.recv_initial_metadata.recv_initial_metadata =
        &initial_metadata_recv;
    ops[4].op = GRPC_OP_RECV_MESSAGE;
    ops[4].data.recv_message.recv_message = &recv_response_payload;
    ops[5].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    ops[5].data.recv_status_on_client.status = &status;
    ops[5].data.recv_status_on_client.status_details = &status_details;
    ops[5].data.recv_status_on_client.trailing_metadata =
        &trailing_metadata_recv;
    grpc_call_start_batch(call.get(), ops, 6, reinterpret_cast<void*>(1),
                          nullptr);
    // Now fetch the handler at the other side, retrieve the request, and poke
    // back a response.
    auto unstarted_handler = helper.TakeHandler();
    unstarted_handler.SpawnInfallible("run_handler", [&]() mutable {
      auto handler = unstarted_handler.StartCall();
      handler.PushServerInitialMetadata(
          Arena::MakePooledForOverwrite<ServerMetadata>());
      auto response =
          Arena::MakePooled<Message>(SliceBuffer(response_payload.Copy()), 0);
      return Map(
          AllOk<StatusFlag>(
              Map(handler.PullClientInitialMetadata(),
                  [](ValueOrFailure<ClientMetadataHandle> status) {
                    return status.status();
                  }),
              Map(handler.PullMessage(),
                  [](ClientToServerNextMessage message) {
                    return message.status();
                  }),
              handler.PushMessage(std::move(response))),
          [handler](StatusFlag status) mutable {
            CHECK(status.ok());
            auto trailing_metadata =
                Arena::MakePooledForOverwrite<ServerMetadata>();
            trailing_metadata->Set(GrpcStatusMetadata(), GRPC_STATUS_OK);
            handler.PushServerTrailingMetadata(std::move(trailing_metadata));
          });
    });
    auto ev = grpc_completion_queue_next(
        helper.cq(), gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    CHECK_EQ(ev.type, GRPC_OP_COMPLETE);
    call.reset();
    grpc_byte_buffer_destroy(recv_response_payload);
    grpc_metadata_array_destroy(&initial_metadata_recv);
    grpc_metadata_array_destroy(&trailing_metadata_recv);
  }
  grpc_byte_buffer_destroy(request_payload);
}
BENCHMARK(BM_Unary);

}  // namespace
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  grpc_init();
  {
    auto ee = grpc_event_engine::experimental::GetDefaultEventEngine();
    benchmark::RunTheBenchmarksNamespaced();
  }
  grpc_shutdown();
  return 0;
}
