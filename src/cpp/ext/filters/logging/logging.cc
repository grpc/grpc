#include <mutex>
#include <grpcpp/ext/logging.h>
#include "cpp/common/channel_filter.h"
#include "core/lib/security/transport/auth_filters.h"
#include "core/lib/surface/call.h"
#include "core/lib/slice/slice_hash_table.h"

std::once_flag loggingOnceFlag;

namespace grpc {
namespace logging {

class LoggingChannelData : public grpc::ChannelData{};

class LoggingServerCallData : public grpc::CallData {
public:
  LoggingServerCallData()
    : recv_initial_metadata_(nullptr)
    , initial_on_done_recv_initial_metadata_(nullptr)
  {}

  grpc_error* Init(grpc_call_element* elem, 
                   const grpc_call_element_args* args) override {
    path_ = grpc_empty_slice();
    GRPC_CLOSURE_INIT(&on_done_recv_initial_metadata_, 
                      OnDoneRecvInitialMetadataCb, elem, 
                      grpc_schedule_on_exec_ctx);
    return GRPC_ERROR_NONE;
  }

  void Destroy(grpc_call_element* elem, const grpc_call_final_info* final_info, 
               grpc_closure* then_call_closure) override {
    grpc_slice_unref_internal(path_);
  }

  void StartTransportStreamOpBatch(grpc_call_element* elem, 
                                   grpc::TransportStreamOpBatch* op) override {
    if (op->recv_initial_metadata() != nullptr) {
      recv_initial_metadata_ = op->recv_initial_metadata()->batch();
      initial_on_done_recv_initial_metadata_ = 
        op->recv_initial_metadata_ready();
      op->set_recv_initial_metadata_ready(&on_done_recv_initial_metadata_);
    }
    grpc_call_next_op(elem, op->op());
  }

  static void OnDoneRecvInitialMetadataCb(void* user_data, grpc_error* error) {
    grpc_call_element* elem = reinterpret_cast<grpc_call_element*>(user_data);
    LoggingServerCallData* calld =
      reinterpret_cast<LoggingServerCallData*>(elem->call_data);
    GPR_ASSERT(calld != nullptr);
    size_t serviceMethodSplitIdx = -1;
    uint8_t* ptr = nullptr;
    size_t length = 0;
    if (error == GRPC_ERROR_NONE) {
      calld->path_ = grpc_slice_ref_internal(
        GRPC_MDVALUE(calld->recv_initial_metadata_->idx.named.path->md)
      );
    }
    if (!GRPC_SLICE_IS_EMPTY(calld->path_)) {
      length = GPR_SLICE_LENGTH(calld->path_);
      ptr = GPR_SLICE_START_PTR(calld->path_);
      for(size_t i=1; i<length; i++) {
        if (ptr[i] == '/') {
          serviceMethodSplitIdx = i;
          break;
        }
      }
    }
    if (error == GRPC_ERROR_NONE && serviceMethodSplitIdx > 0) {
      error = grpc_metadata_batch_add_tail(
        calld->recv_initial_metadata_, 
        &calld->md_links[0], 
        grpc_mdelem_create(
          grpc_slice_from_static_string("grpc.service"),
          grpc_slice_new(ptr + 1, 
                         serviceMethodSplitIdx - 1, 
                         dummy_grpc_slice_destory),
          nullptr
        ));
    }
    if (error == GRPC_ERROR_NONE && serviceMethodSplitIdx > 0) {
      error = grpc_metadata_batch_add_tail(
        calld->recv_initial_metadata_,
        &calld->md_links[1],
        grpc_mdelem_create(
          grpc_slice_from_static_string("grpc.method"),
          grpc_slice_new(ptr + serviceMethodSplitIdx + 1, 
                         length - serviceMethodSplitIdx - 1, 
                          dummy_grpc_slice_destory),
          nullptr
        ));
    }
    GRPC_CLOSURE_RUN(calld->initial_on_done_recv_initial_metadata_, 
                     GRPC_ERROR_REF(error));
  }
private:
  static void dummy_grpc_slice_destory(void*){}

  grpc_closure on_done_recv_initial_metadata_;

  grpc_metadata_batch* recv_initial_metadata_;
  grpc_closure* initial_on_done_recv_initial_metadata_;

  grpc_linked_mdelem md_links[2];

  grpc_slice path_;
};

void registerLoggingPlugin() {
  grpc::RegisterChannelFilter<LoggingChannelData, LoggingServerCallData>(
    "logging_server", GRPC_SERVER_CHANNEL, INT_MAX, nullptr
    );
}

void probe_logging_field_to_clientmeta() {
  std::call_once(loggingOnceFlag, registerLoggingPlugin);
}

}
}