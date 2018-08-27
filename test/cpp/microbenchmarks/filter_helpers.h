/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport_impl.h"

#include "test/cpp/microbenchmarks/helpers.h"

#define CALL_ELEMS_FROM_STACK(stk)     \
  ((grpc_call_element*)((char*)(stk) + \
                        ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(grpc_call_stack))))

/* Given a size, round up to the next multiple of sizeof(void*) */
#define ROUND_UP_TO_ALIGNMENT_SIZE(x) \
  (((x) + GPR_MAX_ALIGNMENT - 1u) & ~(GPR_MAX_ALIGNMENT - 1u))

static void FilterDestroy(void* arg, grpc_error* error) { gpr_free(arg); }

static void DoNothing(void* arg, grpc_error* error) {}

class FakeClientChannelFactory : public grpc_client_channel_factory {
 public:
  FakeClientChannelFactory() { vtable = &vtable_; }

 private:
  static void NoRef(grpc_client_channel_factory* factory_) {}
  static void NoUnref(grpc_client_channel_factory* factory_) {}
  static grpc_subchannel* CreateSubchannel(grpc_client_channel_factory* factory_,
                                           const grpc_subchannel_args* args_) {
    return nullptr;
  }
  static grpc_channel* CreateClientChannel(grpc_client_channel_factory* factory_,
                                           const char* target,
                                           grpc_client_channel_type type,
                                           const grpc_channel_args* args_) {
    return nullptr;
  }

  static const grpc_client_channel_factory_vtable vtable_;
};

const grpc_client_channel_factory_vtable FakeClientChannelFactory::vtable_ = {
    NoRef, NoUnref, CreateSubchannel, CreateClientChannel};

static grpc_arg StringArg(const char* key, const char* value) {
  grpc_arg a;
  a.type = GRPC_ARG_STRING;
  a.key = const_cast<char*>(key);
  a.value.string = const_cast<char*>(value);
  return a;
}

namespace dummy_filter {

static void StartTransportStreamOp(grpc_call_element* elem,
                                   grpc_transport_stream_op_batch* op) {}

static void StartTransportOp(grpc_channel_element* elem,
                             grpc_transport_op* op) {}

static grpc_error* InitCallElem(grpc_call_element* elem,
                                const grpc_call_element_args* args_) {
  return GRPC_ERROR_NONE;
}

static void SetPollsetOrPollsetSet(grpc_call_element* elem,
                                   grpc_polling_entity* pollent) {}

static void DestroyCallElem(grpc_call_element* elem,
                            const grpc_call_final_info* final_info,
                            grpc_closure* then_sched_closure) {}

grpc_error* InitChannelElem(grpc_channel_element* elem,
                            grpc_channel_element_args* args_) {
  return GRPC_ERROR_NONE;
}

void DestroyChannelElem(grpc_channel_element* elem) {}

void GetChannelInfo(grpc_channel_element* elem,
                    const grpc_channel_info* channel_info) {}

static const grpc_channel_filter dummy_filter = {StartTransportStreamOp,
                                                 StartTransportOp,
                                                 0,
                                                 InitCallElem,
                                                 SetPollsetOrPollsetSet,
                                                 DestroyCallElem,
                                                 0,
                                                 InitChannelElem,
                                                 DestroyChannelElem,
                                                 GetChannelInfo,
                                                 "dummy_filter"};

}  // namespace dummy_filter

namespace dummy_transport {

/* Memory required for a single stream element - this is allocated by upper
   layers and initialized by the transport */
size_t sizeof_stream; /* = sizeof(transport stream) */

/* name of this transport implementation */
const char* name;

/* implementation of grpc_transport_init_stream */
int InitStream(grpc_transport* self, grpc_stream* stream,
               grpc_stream_refcount* refcount, const void* server_data,
               gpr_arena* arena) {
  return 0;
}

/* implementation of grpc_transport_set_pollset */
void SetPollset(grpc_transport* self, grpc_stream* stream,
                grpc_pollset* pollset) {}

/* implementation of grpc_transport_set_pollset */
void SetPollsetSet(grpc_transport* self, grpc_stream* stream,
                   grpc_pollset_set* pollset_set) {}

/* implementation of grpc_transport_perform_stream_op */
void PerformStreamOp(grpc_transport* self, grpc_stream* stream,
                     grpc_transport_stream_op_batch* op) {
  GRPC_CLOSURE_SCHED(op->on_complete, GRPC_ERROR_NONE);
}

/* implementation of grpc_transport_perform_op */
void PerformOp(grpc_transport* self, grpc_transport_op* op) {}

/* implementation of grpc_transport_destroy_stream */
void DestroyStream(grpc_transport* self, grpc_stream* stream,
                   grpc_closure* then_sched_closure) {}

/* implementation of grpc_transport_destroy */
void Destroy(grpc_transport* self) {}

/* implementation of grpc_transport_get_endpoint */
grpc_endpoint* GetEndpoint(grpc_transport* self) { return nullptr; }

static const grpc_transport_vtable dummy_transport_vtable = {
    0,          "dummy_http2", InitStream,
    SetPollset, SetPollsetSet, PerformStreamOp,
    PerformOp,  DestroyStream, Destroy,
    GetEndpoint};

static grpc_transport dummy_transport = {&dummy_transport_vtable};

}  // namespace dummy_transport

grpc_channel_args CreateFakeChannelArgs(std::vector<grpc_arg>* args_,
                                        FakeClientChannelFactory* factory_) {
  args_->push_back(grpc_client_channel_factory_create_channel_arg(factory_));
  args_->push_back(StringArg(GRPC_ARG_SERVER_URI, "localhost"));
  return {args_->size(), args_->data()};
}

enum FilterFixtureFlags : uint32_t {
  CHECKS_NOT_LAST = 1,
  REQUIRES_TRANSPORT = 2,
};

template <const grpc_channel_filter* kFilter, uint32_t kFlags>
struct FilterFixture {
  const grpc_channel_filter* filter = kFilter;
  const uint32_t flags = kFlags;
};

template <class FilterFixture>
class FilterBM {
 public:
  // Creates necessary data and structures for the filter microbenchmark
  FilterBM()
      : fixture(), args_(), exec_ctx_(), factory_(), channel_args_(), call_args_() {
        channel_args_ = CreateFakeChannelArgs();
        MaybeAddFilterToStack();
        channel_stack_ =
            ConstructChannelStack();
        call_stack_ = static_cast<grpc_call_stack*>(
            gpr_zalloc(channel_stack_->call_stack_size));
        SetCallArgs(&call_args_, call_stack_);
      }

  ~FilterBM() {
    gpr_arena_destroy(call_args_.arena);
    grpc_channel_stack_destroy(channel_stack_);

    gpr_free(channel_stack_);
    gpr_free(call_stack_);
  }

  grpc_channel_stack* GetChannelStack() {
    return channel_stack_;
  }

  grpc_call_element_args* GetCallArgs() {
    return &call_args_;
  }

  grpc_call_stack* GetCallStack() {
    return call_stack_;
  }

  const std::vector<const grpc_channel_filter*> GetFilters() {
    return filters_;
  }

  FilterFixture fixture;
  const int kArenaSize = 4096;

 private:
  // Caller is responsible for freeing the returned grpc_channel_stack*
  grpc_channel_stack* ConstructChannelStack() {
    size_t channel_size =
        grpc_channel_stack_size(filters_.data(), filters_.size());
    grpc_channel_stack* channel_stack_ =
        static_cast<grpc_channel_stack*>(gpr_zalloc(channel_size));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "channel_stack_init",
        grpc_channel_stack_init(1, FilterDestroy, channel_stack_,
                                filters_.data(), filters_.size(), &channel_args_,
                                fixture.flags & REQUIRES_TRANSPORT
                                    ? &dummy_transport::dummy_transport
                                    : nullptr,
                                "CHANNEL", channel_stack_)));
    grpc_core::ExecCtx::Get()->Flush();
    return channel_stack_;
  }

  grpc_channel_args CreateFakeChannelArgs() {
    args_.push_back(grpc_client_channel_factory_create_channel_arg(&factory_));
    args_.push_back(StringArg(GRPC_ARG_SERVER_URI, "localhost"));
    return {args_.size(), args_.data()};
  }

  // If the filter in FilterFixture is not null, then add it to the filter stack
  // and add a dummy filter to the appropriate place in the stack.
  void MaybeAddFilterToStack() {
    if (fixture.filter == nullptr) {
      return;
    }
    filters_.push_back(fixture.filter);
    if (fixture.flags & CHECKS_NOT_LAST) {
      // This filter cannot be last in the stack, so we must append a dummy
      // filter after it to appease it.
      filters_.push_back(&dummy_filter::dummy_filter);
    } else {
      // This filter must be last on the stack. In order to be consistent with
      // the other benchmarked filters_, we add a dummy filter onto the stack.
      filters_.insert(filters_.begin(), &dummy_filter::dummy_filter);
    }
  }

  void SetCallArgs(grpc_call_element_args* call_args_,
                   grpc_call_stack* call_stack_) {
    grpc_millis deadline = GRPC_MILLIS_INF_FUTURE;
    gpr_timespec start_time = gpr_now(GPR_CLOCK_MONOTONIC);
    grpc_slice method = grpc_slice_from_static_string("/foo/bar");

    call_args_->call_stack = call_stack_;
    call_args_->server_transport_data = nullptr;
    call_args_->context = nullptr;
    call_args_->path = method;
    call_args_->start_time = start_time;
    call_args_->deadline = deadline;
    call_args_->arena = gpr_arena_create(kArenaSize);
  }

  std::vector<grpc_arg> args_;
  grpc_core::ExecCtx exec_ctx_;
  FakeClientChannelFactory factory_;
  std::vector<const grpc_channel_filter*> filters_;
  grpc_channel_args channel_args_;
  grpc_channel_stack* channel_stack_;
  grpc_call_stack* call_stack_;
  grpc_call_element_args call_args_;
};

// Generic data needed for batch payloads in these microbenchmarks
struct PayloadData {
  grpc_metadata_batch metadata_batch_send_init;
  grpc_metadata_batch metadata_batch_recv_init;
  grpc_metadata_batch metadata_batch_send_trailing;
  grpc_metadata_batch metadata_batch_recv_trailing;

  gpr_atm peer_address_atm;

  uint32_t recv_flags;

  grpc_core::OrphanablePtr<grpc_core::ByteStream> op;

  grpc_transport_stream_stats stats;

  grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream>
      byte_stream_send;
  grpc_slice_buffer slice_buffer_send;
  grpc_slice_buffer slice_buffer_recv;

  grpc_transport_stream_op_batch_payload payload;
};

// Initializes the payload such that filters_ won't complain when we send
// all 6 ops down the filter stack
void CreatePayloadForAllOps(struct PayloadData* data) {
  grpc_transport_stream_op_batch_payload* payload = &data->payload;

  memset(data, 0, sizeof(struct PayloadData));

  grpc_metadata_batch_init(&data->metadata_batch_send_init);
  grpc_metadata_batch_init(&data->metadata_batch_recv_init);
  grpc_metadata_batch_init(&data->metadata_batch_send_trailing);
  grpc_metadata_batch_init(&data->metadata_batch_recv_trailing);
  payload->send_initial_metadata.send_initial_metadata =
      &data->metadata_batch_send_init;
  payload->send_trailing_metadata.send_trailing_metadata =
      &data->metadata_batch_send_trailing;
  payload->recv_initial_metadata.recv_initial_metadata =
      &data->metadata_batch_recv_init;
  payload->recv_trailing_metadata.recv_trailing_metadata =
      &data->metadata_batch_recv_trailing;

  payload->recv_initial_metadata.recv_flags = &data->recv_flags;
  payload->recv_initial_metadata.peer_string = &data->peer_address_atm;
  payload->recv_message.recv_message = &data->op;

  payload->collect_stats.collect_stats = &data->stats;

  grpc_slice_buffer_init(&data->slice_buffer_send);
  data->byte_stream_send.Init(&data->slice_buffer_send, 0);
  payload->send_message.send_message.reset(data->byte_stream_send.get());

  grpc_slice_buffer_init(&data->slice_buffer_recv);
}

// Creates a new batch with all 6 ops
void CreateBatchWithAllOps(grpc_transport_stream_op_batch* batch,
                           grpc_transport_stream_op_batch_payload* payload) {
  memset(batch, 0, sizeof(grpc_transport_stream_op_batch));
  batch->payload = payload;
  batch->send_initial_metadata = true;
  batch->send_trailing_metadata = true;
  batch->send_message = true;
  batch->recv_initial_metadata = true;
  batch->recv_message = true;
  batch->recv_trailing_metadata = true;
  batch->collect_stats = true;
}
