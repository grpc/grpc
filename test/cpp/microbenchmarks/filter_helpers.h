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
  static void NoRef(grpc_client_channel_factory* factory) {}
  static void NoUnref(grpc_client_channel_factory* factory) {}
  static grpc_subchannel* CreateSubchannel(grpc_client_channel_factory* factory,
                                           const grpc_subchannel_args* args) {
    return nullptr;
  }
  static grpc_channel* CreateClientChannel(grpc_client_channel_factory* factory,
                                           const char* target,
                                           grpc_client_channel_type type,
                                           const grpc_channel_args* args) {
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
                                const grpc_call_element_args* args) {
  return GRPC_ERROR_NONE;
}

static void SetPollsetOrPollsetSet(grpc_call_element* elem,
                                   grpc_polling_entity* pollent) {}

static void DestroyCallElem(grpc_call_element* elem,
                            const grpc_call_final_info* final_info,
                            grpc_closure* then_sched_closure) {}

grpc_error* InitChannelElem(grpc_channel_element* elem,
                            grpc_channel_element_args* args) {
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

grpc_channel_args CreateFakeChannelArgs(std::vector<grpc_arg>* args,
                                        FakeClientChannelFactory* factory) {
  args->push_back(grpc_client_channel_factory_create_channel_arg(factory));
  args->push_back(StringArg(GRPC_ARG_SERVER_URI, "localhost"));
  return {args->size(), args->data()};
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

// Generic data needed for each filter microbenchmark
struct DataForFilterBM {
  std::vector<const grpc_channel_filter*> filters;
  grpc_channel_args channel_args;
  grpc_channel_stack* channel_stack;
  grpc_call_stack* call_stack;
  grpc_call_element_args call_args;
};

template <class FilterFixture>
class FilterBM {
 public:
  FilterBM()
      : track_counters(), label(), args(), exec_ctx(), factory(), fixture() {}

  // Creates all necessary data and structures for the filter microbenchmark
  void Setup(struct DataForFilterBM* data) {
    data->channel_args = CreateFakeChannelArgs();
    MaybeAddFilterToStack(&data->filters);
    grpc_channel_stack* channel_stack =
        ConstructChannelStack(&data->filters, &data->channel_args);
    data->call_stack =
        static_cast<grpc_call_stack*>(gpr_zalloc(channel_stack->call_stack_size));
    SetCallArgs(&data->call_args, data->call_stack);
  }

  // Call this at the end of the benchmark for cleanup
  void Destroy(struct DataForFilterBM data, benchmark::State& state) {
    gpr_arena_destroy(data->call_args.arena);
    grpc_channel_stack_destroy(data->channel_stack);

    gpr_free(data->channel_stack);
    gpr_free(data->call_stack);

    state.SetLabel(label.str());
    track_counters.Finish(state);
  }

  const int kArenaSize = 4096;
  TrackCounters track_counters;
  std::ostringstream label;

  private:
    // Caller is responsible for freeing the returned grpc_channel_stack*
    grpc_channel_stack* ConstructChannelStack(
        std::vector<const grpc_channel_filter*>* filters,
        grpc_channel_args* channel_args) {
      size_t channel_size =
          grpc_channel_stack_size(filters->data(), filters->size());
      grpc_channel_stack* channel_stack =
          static_cast<grpc_channel_stack*>(gpr_zalloc(channel_size));
      GPR_ASSERT(GRPC_LOG_IF_ERROR(
          "channel_stack_init",
          grpc_channel_stack_init(1, FilterDestroy, channel_stack,
                                  filters->data(), filters->size(), channel_args,
                                  fixture.flags & REQUIRES_TRANSPORT
                                      ? &dummy_transport::dummy_transport
                                      : nullptr,
                                  "CHANNEL", channel_stack)));
      grpc_core::ExecCtx::Get()->Flush();
      return channel_stack;
    }

    grpc_channel_args CreateFakeChannelArgs() {
      args.push_back(grpc_client_channel_factory_create_channel_arg(&factory));
      args.push_back(StringArg(GRPC_ARG_SERVER_URI, "localhost"));
      return {args.size(), args.data()};
    }

    // If the filter in FilterFixture is not null, then add it to the filter stack
    // and add a dummy filter to the appropriate place in the stack.
    void MaybeAddFilterToStack(
        std::vector<const grpc_channel_filter*>* filter_stack) {
      if (fixture.filter == nullptr) {
        return;
      }
      filter_stack->push_back(fixture.filter);
      if (fixture.flags & CHECKS_NOT_LAST) {
        // This filter cannot be last in the stack, so we must append a dummy
        // filter after it to appease it.
        filter_stack->push_back(&dummy_filter::dummy_filter);
      } else {
        // This filter must be last on the stack. In order to be consistent with
        // the other benchmarked filters, we add a dummy filter onto the stack.
        filter_stack->insert(filter_stack->begin(), &dummy_filter::dummy_filter);
      }
    }

    void SetCallArgs(grpc_call_element_args* call_args,
                     grpc_call_stack* call_stack) {
      grpc_millis deadline = GRPC_MILLIS_INF_FUTURE;
      gpr_timespec start_time = gpr_now(GPR_CLOCK_MONOTONIC);
      grpc_slice method = grpc_slice_from_static_string("/foo/bar");

      call_args->call_stack = call_stack;
      call_args->server_transport_data = nullptr;
      call_args->context = nullptr;
      call_args->path = method;
      call_args->start_time = start_time;
      call_args->deadline = deadline;
      call_args->arena = gpr_arena_create(kArenaSize);
    }

    std::vector<grpc_arg> args;
    grpc_core::ExecCtx exec_ctx;
    FakeClientChannelFactory factory;
    FilterFixture fixture;
};
