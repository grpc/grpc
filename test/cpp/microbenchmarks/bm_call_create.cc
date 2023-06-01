//
//
// Copyright 2017 gRPC authors.
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

// This benchmark exists to ensure that the benchmark integration is
// working

#include <string.h>

#include <sstream>

#include <benchmark/benchmark.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpcpp/channel.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/message_compress/compression_filter.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport_impl.h"
#include "src/cpp/client/create_channel_internal.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

void BM_Zalloc(benchmark::State& state) {
  // speed of light for call creation is zalloc, so benchmark a few interesting
  // sizes
  size_t sz = state.range(0);
  for (auto _ : state) {
    gpr_free(gpr_zalloc(sz));
  }
}
BENCHMARK(BM_Zalloc)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Arg(1536)
    ->Arg(2048)
    ->Arg(3072)
    ->Arg(4096)
    ->Arg(5120)
    ->Arg(6144)
    ->Arg(7168);

////////////////////////////////////////////////////////////////////////////////
// Benchmarks creating full stacks

class BaseChannelFixture {
 public:
  explicit BaseChannelFixture(grpc_channel* channel) : channel_(channel) {}
  ~BaseChannelFixture() { grpc_channel_destroy(channel_); }

  grpc_channel* channel() const { return channel_; }

 private:
  grpc_channel* const channel_;
};

static grpc_channel* CreateChannel() {
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  grpc_channel* channel = grpc_channel_create("localhost:1234", creds, nullptr);
  grpc_channel_credentials_release(creds);
  return channel;
}

class InsecureChannel : public BaseChannelFixture {
 public:
  InsecureChannel() : BaseChannelFixture(CreateChannel()) {}
};

class LameChannel : public BaseChannelFixture {
 public:
  LameChannel()
      : BaseChannelFixture(grpc_lame_client_channel_create(
            "localhost:1234", GRPC_STATUS_UNAUTHENTICATED, "blah")) {}
};

template <class Fixture>
static void BM_CallCreateDestroy(benchmark::State& state) {
  Fixture fixture;
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  void* method_hdl = grpc_channel_register_call(fixture.channel(), "/foo/bar",
                                                nullptr, nullptr);
  for (auto _ : state) {
    grpc_call_unref(grpc_channel_create_registered_call(
        fixture.channel(), nullptr, GRPC_PROPAGATE_DEFAULTS, cq, method_hdl,
        deadline, nullptr));
  }
  grpc_completion_queue_destroy(cq);
}

BENCHMARK_TEMPLATE(BM_CallCreateDestroy, InsecureChannel);
BENCHMARK_TEMPLATE(BM_CallCreateDestroy, LameChannel);

////////////////////////////////////////////////////////////////////////////////
// Benchmarks isolating individual filters

static void* tag(int i) {
  return reinterpret_cast<void*>(static_cast<intptr_t>(i));
}

static void BM_LameChannelCallCreateCpp(benchmark::State& state) {
  auto stub =
      grpc::testing::EchoTestService::NewStub(grpc::CreateChannelInternal(
          "",
          grpc_lame_client_channel_create("localhost:1234",
                                          GRPC_STATUS_UNAUTHENTICATED, "blah"),
          std::vector<std::unique_ptr<
              grpc::experimental::ClientInterceptorFactoryInterface>>()));
  grpc::CompletionQueue cq;
  grpc::testing::EchoRequest send_request;
  grpc::testing::EchoResponse recv_response;
  grpc::Status recv_status;
  for (auto _ : state) {
    grpc::ClientContext cli_ctx;
    auto reader = stub->AsyncEcho(&cli_ctx, send_request, &cq);
    reader->Finish(&recv_response, &recv_status, tag(0));
    void* t;
    bool ok;
    GPR_ASSERT(cq.Next(&t, &ok));
    GPR_ASSERT(ok);
  }
}
BENCHMARK(BM_LameChannelCallCreateCpp);

static void do_nothing(void* /*ignored*/) {}

static void BM_LameChannelCallCreateCore(benchmark::State& state) {
  grpc_channel* channel;
  grpc_completion_queue* cq;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_status_code status;
  grpc_slice details;
  grpc::testing::EchoRequest send_request;
  grpc_slice send_request_slice =
      grpc_slice_new(&send_request, sizeof(send_request), do_nothing);

  channel = grpc_lame_client_channel_create(
      "localhost:1234", GRPC_STATUS_UNAUTHENTICATED, "blah");
  cq = grpc_completion_queue_create_for_next(nullptr);
  void* rc = grpc_channel_register_call(
      channel, "/grpc.testing.EchoTestService/Echo", nullptr, nullptr);
  for (auto _ : state) {
    grpc_call* call = grpc_channel_create_registered_call(
        channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq, rc,
        gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    grpc_metadata_array_init(&initial_metadata_recv);
    grpc_metadata_array_init(&trailing_metadata_recv);
    grpc_byte_buffer* request_payload_send =
        grpc_raw_byte_buffer_create(&send_request_slice, 1);

    // Fill in call ops
    grpc_op ops[6];
    memset(ops, 0, sizeof(ops));
    grpc_op* op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op++;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = request_payload_send;
    op++;
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op++;
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &initial_metadata_recv;
    op++;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload_recv;
    op++;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    op++;

    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops,
                                                     (size_t)(op - ops),
                                                     (void*)1, nullptr));
    grpc_event ev = grpc_completion_queue_next(
        cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(ev.type != GRPC_QUEUE_SHUTDOWN);
    GPR_ASSERT(ev.success != 0);
    grpc_call_unref(call);
    grpc_byte_buffer_destroy(request_payload_send);
    grpc_byte_buffer_destroy(response_payload_recv);
    grpc_metadata_array_destroy(&initial_metadata_recv);
    grpc_metadata_array_destroy(&trailing_metadata_recv);
  }
  grpc_channel_destroy(channel);
  grpc_completion_queue_destroy(cq);
  grpc_slice_unref(send_request_slice);
}
BENCHMARK(BM_LameChannelCallCreateCore);

static void BM_LameChannelCallCreateCoreSeparateBatch(benchmark::State& state) {
  grpc_channel* channel;
  grpc_completion_queue* cq;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_status_code status;
  grpc_slice details;
  grpc::testing::EchoRequest send_request;
  grpc_slice send_request_slice =
      grpc_slice_new(&send_request, sizeof(send_request), do_nothing);

  channel = grpc_lame_client_channel_create(
      "localhost:1234", GRPC_STATUS_UNAUTHENTICATED, "blah");
  cq = grpc_completion_queue_create_for_next(nullptr);
  void* rc = grpc_channel_register_call(
      channel, "/grpc.testing.EchoTestService/Echo", nullptr, nullptr);
  for (auto _ : state) {
    grpc_call* call = grpc_channel_create_registered_call(
        channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq, rc,
        gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    grpc_metadata_array_init(&initial_metadata_recv);
    grpc_metadata_array_init(&trailing_metadata_recv);
    grpc_byte_buffer* request_payload_send =
        grpc_raw_byte_buffer_create(&send_request_slice, 1);

    // Fill in call ops
    grpc_op ops[3];
    memset(ops, 0, sizeof(ops));
    grpc_op* op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op++;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = request_payload_send;
    op++;
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op++;
    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops,
                                                     (size_t)(op - ops),
                                                     (void*)nullptr, nullptr));
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &initial_metadata_recv;
    op++;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload_recv;
    op++;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    op++;

    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops,
                                                     (size_t)(op - ops),
                                                     (void*)1, nullptr));
    grpc_event ev = grpc_completion_queue_next(
        cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(ev.type != GRPC_QUEUE_SHUTDOWN);
    GPR_ASSERT(ev.success == 0);
    ev = grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr);
    GPR_ASSERT(ev.type != GRPC_QUEUE_SHUTDOWN);
    GPR_ASSERT(ev.success != 0);
    grpc_call_unref(call);
    grpc_byte_buffer_destroy(request_payload_send);
    grpc_byte_buffer_destroy(response_payload_recv);
    grpc_metadata_array_destroy(&initial_metadata_recv);
    grpc_metadata_array_destroy(&trailing_metadata_recv);
  }
  grpc_channel_destroy(channel);
  grpc_completion_queue_destroy(cq);
  grpc_slice_unref(send_request_slice);
}
BENCHMARK(BM_LameChannelCallCreateCoreSeparateBatch);

static void FilterDestroy(void* arg, grpc_error_handle /*error*/) {
  gpr_free(arg);
}

static void DoNothing(void* /*arg*/, grpc_error_handle /*error*/) {}

class FakeClientChannelFactory : public grpc_core::ClientChannelFactory {
 public:
  grpc_core::RefCountedPtr<grpc_core::Subchannel> CreateSubchannel(
      const grpc_resolved_address& /*address*/,
      const grpc_core::ChannelArgs& /*args*/) override {
    return nullptr;
  }
};

enum FixtureFlags : uint32_t {
  CHECKS_NOT_LAST = 1,
  REQUIRES_TRANSPORT = 2,
};

template <const grpc_channel_filter* kFilter, uint32_t kFlags>
struct Fixture {
  const grpc_channel_filter* filter = kFilter;
  const uint32_t flags = kFlags;
};

namespace phony_filter {

static void StartTransportStreamOp(grpc_call_element* /*elem*/,
                                   grpc_transport_stream_op_batch* /*op*/) {}

static void StartTransportOp(grpc_channel_element* /*elem*/,
                             grpc_transport_op* /*op*/) {}

static grpc_error_handle InitCallElem(grpc_call_element* /*elem*/,
                                      const grpc_call_element_args* /*args*/) {
  return absl::OkStatus();
}

static void SetPollsetOrPollsetSet(grpc_call_element* /*elem*/,
                                   grpc_polling_entity* /*pollent*/) {}

static void DestroyCallElem(grpc_call_element* /*elem*/,
                            const grpc_call_final_info* /*final_info*/,
                            grpc_closure* /*then_sched_closure*/) {}

grpc_error_handle InitChannelElem(grpc_channel_element* /*elem*/,
                                  grpc_channel_element_args* /*args*/) {
  return absl::OkStatus();
}

void DestroyChannelElem(grpc_channel_element* /*elem*/) {}

void GetChannelInfo(grpc_channel_element* /*elem*/,
                    const grpc_channel_info* /*channel_info*/) {}

static const grpc_channel_filter phony_filter = {
    StartTransportStreamOp, nullptr,
    StartTransportOp,       0,
    InitCallElem,           SetPollsetOrPollsetSet,
    DestroyCallElem,        0,
    InitChannelElem,        grpc_channel_stack_no_post_init,
    DestroyChannelElem,     GetChannelInfo,
    "phony_filter"};

}  // namespace phony_filter

namespace phony_transport {

// Memory required for a single stream element - this is allocated by upper
// layers and initialized by the transport
size_t sizeof_stream;  // = sizeof(transport stream)

// name of this transport implementation
const char* name;

// implementation of grpc_transport_init_stream
int InitStream(grpc_transport* /*self*/, grpc_stream* /*stream*/,
               grpc_stream_refcount* /*refcount*/, const void* /*server_data*/,
               grpc_core::Arena* /*arena*/) {
  return 0;
}

// implementation of grpc_transport_set_pollset
void SetPollset(grpc_transport* /*self*/, grpc_stream* /*stream*/,
                grpc_pollset* /*pollset*/) {}

// implementation of grpc_transport_set_pollset
void SetPollsetSet(grpc_transport* /*self*/, grpc_stream* /*stream*/,
                   grpc_pollset_set* /*pollset_set*/) {}

// implementation of grpc_transport_perform_stream_op
void PerformStreamOp(grpc_transport* /*self*/, grpc_stream* /*stream*/,
                     grpc_transport_stream_op_batch* op) {
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete, absl::OkStatus());
}

// implementation of grpc_transport_perform_op
void PerformOp(grpc_transport* /*self*/, grpc_transport_op* /*op*/) {}

// implementation of grpc_transport_destroy_stream
void DestroyStream(grpc_transport* /*self*/, grpc_stream* /*stream*/,
                   grpc_closure* /*then_sched_closure*/) {}

// implementation of grpc_transport_destroy
void Destroy(grpc_transport* /*self*/) {}

// implementation of grpc_transport_get_endpoint
grpc_endpoint* GetEndpoint(grpc_transport* /*self*/) { return nullptr; }

static const grpc_transport_vtable phony_transport_vtable = {
    0,         false,         "phony_http2", InitStream,
    nullptr,   SetPollset,    SetPollsetSet, PerformStreamOp,
    PerformOp, DestroyStream, Destroy,       GetEndpoint};

static grpc_transport phony_transport = {&phony_transport_vtable};

grpc_arg Arg() {
  static const grpc_arg_pointer_vtable vtable = {
      // copy
      [](void* p) { return p; },
      // destroy
      [](void*) {},
      // cmp
      [](void* a, void* b) { return grpc_core::QsortCompare(a, b); },
  };
  return grpc_channel_arg_pointer_create(const_cast<char*>(GRPC_ARG_TRANSPORT),
                                         &phony_transport, &vtable);
}

}  // namespace phony_transport

class NoOp {
 public:
  class Op {
   public:
    Op(NoOp* /*p*/, grpc_call_stack* /*s*/, grpc_core::Arena*) {}
    void Finish() {}
  };
};

class SendEmptyMetadata {
 public:
  SendEmptyMetadata() : op_payload_(nullptr) {
    op_ = {};
    op_.on_complete = GRPC_CLOSURE_INIT(&closure_, DoNothing, nullptr,
                                        grpc_schedule_on_exec_ctx);
    op_.send_initial_metadata = true;
    op_.payload = &op_payload_;
  }

  class Op {
   public:
    Op(SendEmptyMetadata* p, grpc_call_stack* /*s*/, grpc_core::Arena* arena)
        : batch_(arena) {
      p->op_payload_.send_initial_metadata.send_initial_metadata = &batch_;
    }
    void Finish() {}

   private:
    grpc_metadata_batch batch_;
  };

 private:
  const gpr_timespec deadline_ = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  const gpr_timespec start_time_ = gpr_now(GPR_CLOCK_MONOTONIC);
  const grpc_slice method_ = grpc_slice_from_static_string("/foo/bar");
  grpc_transport_stream_op_batch op_;
  grpc_transport_stream_op_batch_payload op_payload_;
  grpc_closure closure_;
};

// Test a filter in isolation. Fixture specifies the filter under test (use the
// Fixture<> template to specify this), and TestOp defines some unit of work to
// perform on said filter.
template <class Fixture, class TestOp>
static void BM_IsolatedFilter(benchmark::State& state) {
  Fixture fixture;
  std::ostringstream label;
  FakeClientChannelFactory fake_client_channel_factory;

  grpc_core::ChannelArgs channel_args =
      grpc_core::CoreConfiguration::Get()
          .channel_args_preconditioning()
          .PreconditionChannelArgs(nullptr)
          .SetObject(&fake_client_channel_factory)
          .Set(GRPC_ARG_SERVER_URI, "localhost");
  if (fixture.flags & REQUIRES_TRANSPORT) {
    channel_args = channel_args.Set(phony_transport::Arg());
  }

  std::vector<const grpc_channel_filter*> filters;
  if (fixture.filter != nullptr) {
    filters.push_back(fixture.filter);
  }
  if (fixture.flags & CHECKS_NOT_LAST) {
    filters.push_back(&phony_filter::phony_filter);
    label << " #has_phony_filter";
  }

  grpc_core::ExecCtx exec_ctx;
  size_t channel_size = grpc_channel_stack_size(
      filters.empty() ? nullptr : &filters[0], filters.size());
  grpc_channel_stack* channel_stack =
      static_cast<grpc_channel_stack*>(gpr_zalloc(channel_size));
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "channel_stack_init",
      grpc_channel_stack_init(1, FilterDestroy, channel_stack,
                              filters.empty() ? nullptr : &filters[0],
                              filters.size(), channel_args, "CHANNEL",
                              channel_stack)));
  grpc_core::ExecCtx::Get()->Flush();
  grpc_call_stack* call_stack =
      static_cast<grpc_call_stack*>(gpr_zalloc(channel_stack->call_stack_size));
  grpc_core::Timestamp deadline = grpc_core::Timestamp::InfFuture();
  gpr_cycle_counter start_time = gpr_get_cycle_counter();
  grpc_slice method = grpc_slice_from_static_string("/foo/bar");
  grpc_call_final_info final_info;
  TestOp test_op_data;
  const int kArenaSize = 32 * 1024 * 1024;
  grpc_call_context_element context[GRPC_CONTEXT_COUNT] = {};
  grpc_core::MemoryAllocator memory_allocator =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
  grpc_call_element_args call_args{
      call_stack,
      nullptr,
      context,
      method,
      start_time,
      deadline,
      grpc_core::Arena::Create(kArenaSize, &memory_allocator),
      nullptr};
  while (state.KeepRunning()) {
    (void)grpc_call_stack_init(channel_stack, 1, DoNothing, nullptr,
                               &call_args);
    typename TestOp::Op op(&test_op_data, call_stack, call_args.arena);
    grpc_call_stack_destroy(call_stack, &final_info, nullptr);
    op.Finish();
    grpc_core::ExecCtx::Get()->Flush();
    // recreate arena every 64k iterations to avoid oom
    if (0 == (state.iterations() & 0xffff)) {
      call_args.arena->Destroy();
      call_args.arena = grpc_core::Arena::Create(kArenaSize, &memory_allocator);
    }
  }
  call_args.arena->Destroy();
  grpc_channel_stack_destroy(channel_stack);
  grpc_core::ExecCtx::Get()->Flush();

  gpr_free(channel_stack);
  gpr_free(call_stack);

  state.SetLabel(label.str());
}

typedef Fixture<nullptr, 0> NoFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, NoFilter, NoOp);
typedef Fixture<&phony_filter::phony_filter, 0> PhonyFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, PhonyFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, PhonyFilter, SendEmptyMetadata);
typedef Fixture<&grpc_core::ClientChannel::kFilterVtable, 0>
    ClientChannelFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ClientChannelFilter, NoOp);
typedef Fixture<&grpc_core::ClientCompressionFilter::kFilter, CHECKS_NOT_LAST>
    ClientCompressFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ClientCompressFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ClientCompressFilter, SendEmptyMetadata);
typedef Fixture<&grpc_client_deadline_filter, CHECKS_NOT_LAST>
    ClientDeadlineFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ClientDeadlineFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ClientDeadlineFilter, SendEmptyMetadata);
typedef Fixture<&grpc_server_deadline_filter, CHECKS_NOT_LAST>
    ServerDeadlineFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ServerDeadlineFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ServerDeadlineFilter, SendEmptyMetadata);
typedef Fixture<&grpc_core::HttpClientFilter::kFilter,
                CHECKS_NOT_LAST | REQUIRES_TRANSPORT>
    HttpClientFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, HttpClientFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, HttpClientFilter, SendEmptyMetadata);
typedef Fixture<&grpc_core::HttpServerFilter::kFilter, CHECKS_NOT_LAST>
    HttpServerFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, HttpServerFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, HttpServerFilter, SendEmptyMetadata);
typedef Fixture<&grpc_core::ServerCompressionFilter::kFilter, CHECKS_NOT_LAST>
    ServerCompressFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ServerCompressFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ServerCompressFilter, SendEmptyMetadata);
// This cmake target is disabled for now because it depends on OpenCensus, which
// is Bazel-only.
// typedef Fixture<&grpc_server_load_reporting_filter, CHECKS_NOT_LAST>
//    LoadReportingFilter;
// BENCHMARK_TEMPLATE(BM_IsolatedFilter, LoadReportingFilter, NoOp);
// BENCHMARK_TEMPLATE(BM_IsolatedFilter, LoadReportingFilter,
// SendEmptyMetadata);

////////////////////////////////////////////////////////////////////////////////
// Benchmarks isolating grpc_call

namespace isolated_call_filter {

typedef struct {
  grpc_core::CallCombiner* call_combiner;
} call_data;

static void StartTransportStreamOp(grpc_call_element* elem,
                                   grpc_transport_stream_op_batch* op) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // Construct list of closures to return.
  grpc_core::CallCombinerClosureList closures;
  if (op->recv_initial_metadata) {
    closures.Add(op->payload->recv_initial_metadata.recv_initial_metadata_ready,
                 absl::OkStatus(), "recv_initial_metadata");
  }
  if (op->recv_message) {
    closures.Add(op->payload->recv_message.recv_message_ready, absl::OkStatus(),
                 "recv_message");
  }
  if (op->recv_trailing_metadata) {
    closures.Add(
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready,
        absl::OkStatus(), "recv_trailing_metadata");
  }
  if (op->on_complete != nullptr) {
    closures.Add(op->on_complete, absl::OkStatus(), "on_complete");
  }
  // Execute closures.
  closures.RunClosures(calld->call_combiner);
}

static void StartTransportOp(grpc_channel_element* /*elem*/,
                             grpc_transport_op* op) {
  if (!op->disconnect_with_error.ok()) {
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
}

static grpc_error_handle InitCallElem(grpc_call_element* elem,
                                      const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->call_combiner = args->call_combiner;
  return absl::OkStatus();
}

static void SetPollsetOrPollsetSet(grpc_call_element* /*elem*/,
                                   grpc_polling_entity* /*pollent*/) {}

static void DestroyCallElem(grpc_call_element* /*elem*/,
                            const grpc_call_final_info* /*final_info*/,
                            grpc_closure* then_sched_closure) {
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, then_sched_closure, absl::OkStatus());
}

grpc_error_handle InitChannelElem(grpc_channel_element* /*elem*/,
                                  grpc_channel_element_args* /*args*/) {
  return absl::OkStatus();
}

void DestroyChannelElem(grpc_channel_element* /*elem*/) {}

void GetChannelInfo(grpc_channel_element* /*elem*/,
                    const grpc_channel_info* /*channel_info*/) {}

static const grpc_channel_filter isolated_call_filter = {
    StartTransportStreamOp, nullptr,
    StartTransportOp,       sizeof(call_data),
    InitCallElem,           SetPollsetOrPollsetSet,
    DestroyCallElem,        0,
    InitChannelElem,        grpc_channel_stack_no_post_init,
    DestroyChannelElem,     GetChannelInfo,
    "isolated_call_filter"};
}  // namespace isolated_call_filter

class IsolatedCallFixture {
 public:
  IsolatedCallFixture() {
    // We are calling grpc_channel_stack_builder_create() instead of
    // grpc_channel_create() here, which means we're not getting the
    // grpc_init() called by grpc_channel_create(), but we are getting
    // the grpc_shutdown() run by grpc_channel_destroy().  So we need to
    // call grpc_init() manually here to balance things out.
    grpc_init();
    grpc_core::ChannelStackBuilderImpl builder(
        "phony", GRPC_CLIENT_CHANNEL,
        grpc_core::CoreConfiguration::Get()
            .channel_args_preconditioning()
            .PreconditionChannelArgs(nullptr));
    builder.SetTarget("phony_target");
    builder.AppendFilter(&isolated_call_filter::isolated_call_filter);
    {
      grpc_core::ExecCtx exec_ctx;
      channel_ =
          grpc_core::Channel::CreateWithBuilder(&builder)->release()->c_ptr();
    }
    cq_ = grpc_completion_queue_create_for_next(nullptr);
  }

  void Finish(benchmark::State&) {
    grpc_completion_queue_destroy(cq_);
    grpc_channel_destroy(channel_);
  }

  grpc_channel* channel() const { return channel_; }
  grpc_completion_queue* cq() const { return cq_; }

 private:
  grpc_completion_queue* cq_;
  grpc_channel* channel_;
};

static void BM_IsolatedCall_NoOp(benchmark::State& state) {
  IsolatedCallFixture fixture;
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  void* method_hdl = grpc_channel_register_call(fixture.channel(), "/foo/bar",
                                                nullptr, nullptr);
  for (auto _ : state) {
    grpc_call_unref(grpc_channel_create_registered_call(
        fixture.channel(), nullptr, GRPC_PROPAGATE_DEFAULTS, fixture.cq(),
        method_hdl, deadline, nullptr));
  }
  fixture.Finish(state);
}
BENCHMARK(BM_IsolatedCall_NoOp);

static void BM_IsolatedCall_Unary(benchmark::State& state) {
  IsolatedCallFixture fixture;
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  void* method_hdl = grpc_channel_register_call(fixture.channel(), "/foo/bar",
                                                nullptr, nullptr);
  grpc_slice slice = grpc_slice_from_static_string("hello world");
  grpc_byte_buffer* send_message = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_byte_buffer* recv_message = nullptr;
  grpc_status_code status_code;
  grpc_slice status_details = grpc_empty_slice();
  grpc_metadata_array recv_initial_metadata;
  grpc_metadata_array_init(&recv_initial_metadata);
  grpc_metadata_array recv_trailing_metadata;
  grpc_metadata_array_init(&recv_trailing_metadata);
  grpc_op ops[6];
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  ops[1].op = GRPC_OP_SEND_MESSAGE;
  ops[1].data.send_message.send_message = send_message;
  ops[2].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  ops[3].op = GRPC_OP_RECV_INITIAL_METADATA;
  ops[3].data.recv_initial_metadata.recv_initial_metadata =
      &recv_initial_metadata;
  ops[4].op = GRPC_OP_RECV_MESSAGE;
  ops[4].data.recv_message.recv_message = &recv_message;
  ops[5].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  ops[5].data.recv_status_on_client.status = &status_code;
  ops[5].data.recv_status_on_client.status_details = &status_details;
  ops[5].data.recv_status_on_client.trailing_metadata = &recv_trailing_metadata;
  for (auto _ : state) {
    grpc_call* call = grpc_channel_create_registered_call(
        fixture.channel(), nullptr, GRPC_PROPAGATE_DEFAULTS, fixture.cq(),
        method_hdl, deadline, nullptr);
    grpc_call_start_batch(call, ops, 6, tag(1), nullptr);
    grpc_completion_queue_next(fixture.cq(),
                               gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
    grpc_call_unref(call);
  }
  fixture.Finish(state);
  grpc_metadata_array_destroy(&recv_initial_metadata);
  grpc_metadata_array_destroy(&recv_trailing_metadata);
  grpc_byte_buffer_destroy(send_message);
}
BENCHMARK(BM_IsolatedCall_Unary);

static void BM_IsolatedCall_StreamingSend(benchmark::State& state) {
  IsolatedCallFixture fixture;
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  void* method_hdl = grpc_channel_register_call(fixture.channel(), "/foo/bar",
                                                nullptr, nullptr);
  grpc_slice slice = grpc_slice_from_static_string("hello world");
  grpc_byte_buffer* send_message = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_metadata_array recv_initial_metadata;
  grpc_metadata_array_init(&recv_initial_metadata);
  grpc_metadata_array recv_trailing_metadata;
  grpc_metadata_array_init(&recv_trailing_metadata);
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  ops[1].op = GRPC_OP_RECV_INITIAL_METADATA;
  ops[1].data.recv_initial_metadata.recv_initial_metadata =
      &recv_initial_metadata;
  grpc_call* call = grpc_channel_create_registered_call(
      fixture.channel(), nullptr, GRPC_PROPAGATE_DEFAULTS, fixture.cq(),
      method_hdl, deadline, nullptr);
  grpc_call_start_batch(call, ops, 2, tag(1), nullptr);
  grpc_completion_queue_next(fixture.cq(), gpr_inf_future(GPR_CLOCK_MONOTONIC),
                             nullptr);
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_MESSAGE;
  ops[0].data.send_message.send_message = send_message;
  for (auto _ : state) {
    grpc_call_start_batch(call, ops, 1, tag(2), nullptr);
    grpc_completion_queue_next(fixture.cq(),
                               gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  }
  grpc_call_unref(call);
  fixture.Finish(state);
  grpc_metadata_array_destroy(&recv_initial_metadata);
  grpc_metadata_array_destroy(&recv_trailing_metadata);
  grpc_byte_buffer_destroy(send_message);
}
BENCHMARK(BM_IsolatedCall_StreamingSend);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
