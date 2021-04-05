//
// Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/retry_filter.h"

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "absl/strings/strip.h"

#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/retry_service_config.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/service_config_call_data.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_metadata.h"
#include "src/core/lib/uri/uri_parser.h"

//
// Retry filter
//

// This filter is intended to be used in the DynamicFilter stack in the
// client channel, which is situated between the name resolver and the
// LB policy.  Normally, the last filter in the DynamicFilter stack is
// the DynamicTerminationFilter (see client_channel.cc), which creates a
// LoadBalancedCall and delegates to it.  However, when retries are
// enabled, this filter is used instead of the DynamicTerminationFilter.
//
// In order to support retries, we act as a proxy for stream op batches.
// When we get a batch from the surface, we add it to our list of pending
// batches, and we then use those batches to construct separate "child"
// batches to be started on the subchannel call.  When the child batches
// return, we then decide which pending batches have been completed and
// schedule their callbacks accordingly.  If a subchannel call fails and
// we want to retry it, we do a new pick and start again, constructing
// new "child" batches for the new subchannel call.
//
// Note that retries are committed when receiving data from the server
// (except for Trailers-Only responses).  However, there may be many
// send ops started before receiving any data, so we may have already
// completed some number of send ops (and returned the completions up to
// the surface) by the time we realize that we need to retry.  To deal
// with this, we cache data for send ops, so that we can replay them on a
// different subchannel call even after we have completed the original
// batches.
//
// There are two sets of data to maintain:
// - In call_data (in the parent channel), we maintain a list of pending
//   ops and cached data for send ops.
// - In the subchannel call, we maintain state to indicate what ops have
//   already been sent down to that call.
//
// When constructing the "child" batches, we compare those two sets of
// data to see which batches need to be sent to the subchannel call.

// TODO(roth): In subsequent PRs:
// - add support for transparent retries (including initial metadata)
// - figure out how to record stats in census for retries
//   (census filter is on top of this one)
// - add census stats for retries

// By default, we buffer 256 KiB per RPC for retries.
// TODO(roth): Do we have any data to suggest a better value?
#define DEFAULT_PER_RPC_RETRY_BUFFER_SIZE (256 << 10)

// This value was picked arbitrarily.  It can be changed if there is
// any even moderately compelling reason to do so.
#define RETRY_BACKOFF_JITTER 0.2

namespace grpc_core {

namespace {

using internal::RetryGlobalConfig;
using internal::RetryMethodConfig;
using internal::RetryServiceConfigParser;
using internal::ServerRetryThrottleData;

TraceFlag grpc_retry_trace(false, "retry");

//
// RetryFilter
//

class RetryFilter {
 public:
  class CallData;

  static grpc_error* Init(grpc_channel_element* elem,
                          grpc_channel_element_args* args) {
    GPR_ASSERT(args->is_last);
    GPR_ASSERT(elem->filter == &kRetryFilterVtable);
    grpc_error* error = GRPC_ERROR_NONE;
    new (elem->channel_data) RetryFilter(args->channel_args, &error);
    return error;
  }

  static void Destroy(grpc_channel_element* elem) {
    auto* chand = static_cast<RetryFilter*>(elem->channel_data);
    chand->~RetryFilter();
  }

  // Will never be called.
  static void StartTransportOp(grpc_channel_element* /*elem*/,
                               grpc_transport_op* /*op*/) {}
  static void GetChannelInfo(grpc_channel_element* /*elem*/,
                             const grpc_channel_info* /*info*/) {}

 private:
  static size_t GetMaxPerRpcRetryBufferSize(const grpc_channel_args* args) {
    return static_cast<size_t>(grpc_channel_args_find_integer(
        args, GRPC_ARG_PER_RPC_RETRY_BUFFER_SIZE,
        {DEFAULT_PER_RPC_RETRY_BUFFER_SIZE, 0, INT_MAX}));
  }

  RetryFilter(const grpc_channel_args* args, grpc_error** error)
      : client_channel_(grpc_channel_args_find_pointer<ClientChannel>(
            args, GRPC_ARG_CLIENT_CHANNEL)),
        per_rpc_retry_buffer_size_(GetMaxPerRpcRetryBufferSize(args)) {
    // Get retry throttling parameters from service config.
    auto* service_config = grpc_channel_args_find_pointer<ServiceConfig>(
        args, GRPC_ARG_SERVICE_CONFIG_OBJ);
    if (service_config == nullptr) return;
    const auto* config = static_cast<const RetryGlobalConfig*>(
        service_config->GetGlobalParsedConfig(
            RetryServiceConfigParser::ParserIndex()));
    if (config == nullptr) return;
    // Get server name from target URI.
    const char* server_uri =
        grpc_channel_args_find_string(args, GRPC_ARG_SERVER_URI);
    if (server_uri == nullptr) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "server URI channel arg missing or wrong type in client channel "
          "filter");
      return;
    }
    absl::StatusOr<URI> uri = URI::Parse(server_uri);
    if (!uri.ok() || uri->path().empty()) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "could not extract server name from target URI");
      return;
    }
    std::string server_name(absl::StripPrefix(uri->path(), "/"));
    // Get throttling config for server_name.
    retry_throttle_data_ = internal::ServerRetryThrottleMap::GetDataForServer(
        server_name, config->max_milli_tokens(), config->milli_token_ratio());
  }

  ClientChannel* client_channel_;
  size_t per_rpc_retry_buffer_size_;
  RefCountedPtr<ServerRetryThrottleData> retry_throttle_data_;
};

//
// RetryFilter::CallData
//

class RetryFilter::CallData {
 public:
  static grpc_error* Init(grpc_call_element* elem,
                          const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* /*final_info*/,
                      grpc_closure* then_schedule_closure);
  static void StartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);
  static void SetPollent(grpc_call_element* elem, grpc_polling_entity* pollent);

 private:
  CallData(RetryFilter* chand, const grpc_call_element_args& args);
  ~CallData();

  // State used for starting a retryable batch on a subchannel call.
  // This provides its own grpc_transport_stream_op_batch and other data
  // structures needed to populate the ops in the batch.
  // We allocate one struct on the arena for each attempt at starting a
  // batch on a given subchannel call.
  struct SubchannelCallBatchData {
    // Creates a SubchannelCallBatchData object on the call's arena with the
    // specified refcount.  If set_on_complete is true, the batch's
    // on_complete callback will be set to point to on_complete();
    // otherwise, the batch's on_complete callback will be null.
    static SubchannelCallBatchData* Create(CallData* call, int refcount,
                                           bool set_on_complete);

    void Unref() {
      if (gpr_unref(&refs)) Destroy();
    }

    SubchannelCallBatchData(CallData* call, int refcount, bool set_on_complete);
    // All dtor code must be added in `Destroy()`. This is because we may
    // call closures in `SubchannelCallBatchData` after they are unrefed by
    // `Unref()`, and msan would complain about accessing this class
    // after calling dtor. As a result we cannot call the `dtor` in `Unref()`.
    // TODO(soheil): We should try to call the dtor in `Unref()`.
    ~SubchannelCallBatchData() { Destroy(); }
    void Destroy();

    gpr_refcount refs;
    grpc_call_element* elem;
    CallData* call;
    RefCountedPtr<ClientChannel::LoadBalancedCall> lb_call;
    // The batch to use in the subchannel call.
    // Its payload field points to SubchannelCallRetryState::batch_payload.
    grpc_transport_stream_op_batch batch;
    // For intercepting on_complete.
    grpc_closure on_complete;
  };

  // Retry state associated with a subchannel call.
  // Stored in the parent_data of the subchannel call object.
  // TODO(roth): As part of implementing hedging, we'll need to store a
  // ref to the LB call in this struct instead of doing the parent_data
  // hack, since there will be multiple LB calls in flight at once.
  // We will also need to maintain a list of all pending attempts, so that
  // we can cancel them all if the call gets cancelled.
  struct SubchannelCallRetryState {
    explicit SubchannelCallRetryState(grpc_call_context_element* context)
        : batch_payload(context),
          started_send_initial_metadata(false),
          completed_send_initial_metadata(false),
          started_send_trailing_metadata(false),
          completed_send_trailing_metadata(false),
          started_recv_initial_metadata(false),
          completed_recv_initial_metadata(false),
          started_recv_trailing_metadata(false),
          completed_recv_trailing_metadata(false),
          retry_dispatched(false) {}

    // SubchannelCallBatchData.batch.payload points to this.
    grpc_transport_stream_op_batch_payload batch_payload;
    // For send_initial_metadata.
    // Note that we need to make a copy of the initial metadata for each
    // subchannel call instead of just referring to the copy in call_data,
    // because filters in the subchannel stack will probably add entries,
    // so we need to start in a pristine state for each attempt of the call.
    grpc_linked_mdelem* send_initial_metadata_storage;
    grpc_metadata_batch send_initial_metadata;
    // For send_message.
    // TODO(roth): Restructure this to eliminate use of ManualConstructor.
    ManualConstructor<ByteStreamCache::CachingByteStream> send_message;
    // For send_trailing_metadata.
    grpc_linked_mdelem* send_trailing_metadata_storage;
    grpc_metadata_batch send_trailing_metadata;
    // For intercepting recv_initial_metadata.
    grpc_metadata_batch recv_initial_metadata;
    grpc_closure recv_initial_metadata_ready;
    bool trailing_metadata_available = false;
    // For intercepting recv_message.
    grpc_closure recv_message_ready;
    OrphanablePtr<ByteStream> recv_message;
    // For intercepting recv_trailing_metadata.
    grpc_metadata_batch recv_trailing_metadata;
    grpc_transport_stream_stats collect_stats;
    grpc_closure recv_trailing_metadata_ready;
    // These fields indicate which ops have been started and completed on
    // this subchannel call.
    size_t started_send_message_count = 0;
    size_t completed_send_message_count = 0;
    size_t started_recv_message_count = 0;
    size_t completed_recv_message_count = 0;
    bool started_send_initial_metadata : 1;
    bool completed_send_initial_metadata : 1;
    bool started_send_trailing_metadata : 1;
    bool completed_send_trailing_metadata : 1;
    bool started_recv_initial_metadata : 1;
    bool completed_recv_initial_metadata : 1;
    bool started_recv_trailing_metadata : 1;
    bool completed_recv_trailing_metadata : 1;
    // State for callback processing.
    SubchannelCallBatchData* recv_initial_metadata_ready_deferred_batch =
        nullptr;
    grpc_error* recv_initial_metadata_error = GRPC_ERROR_NONE;
    SubchannelCallBatchData* recv_message_ready_deferred_batch = nullptr;
    grpc_error* recv_message_error = GRPC_ERROR_NONE;
    SubchannelCallBatchData* recv_trailing_metadata_internal_batch = nullptr;
    // NOTE: Do not move this next to the metadata bitfields above. That would
    //       save space but will also result in a data race because compiler
    //       will generate a 2 byte store which overwrites the meta-data
    //       fields upon setting this field.
    bool retry_dispatched : 1;
  };

  // Pending batches stored in call data.
  struct PendingBatch {
    // The pending batch.  If nullptr, this slot is empty.
    grpc_transport_stream_op_batch* batch = nullptr;
    // Indicates whether payload for send ops has been cached in CallData.
    bool send_ops_cached = false;
  };

  void StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch);

  // Caches data for send ops so that it can be retried later, if not
  // already cached.
  void MaybeCacheSendOpsForBatch(PendingBatch* pending);
  void FreeCachedSendInitialMetadata();
  // Frees cached send_message at index idx.
  void FreeCachedSendMessage(size_t idx);
  void FreeCachedSendTrailingMetadata();
  // Frees cached send ops that have already been completed after
  // committing the call.
  void FreeCachedSendOpDataAfterCommit(SubchannelCallRetryState* retry_state);
  // Frees cached send ops that were completed by the completed batch in
  // batch_data.  Used when batches are completed after the call is committed.
  void FreeCachedSendOpDataForCompletedBatch(
      SubchannelCallBatchData* batch_data,
      SubchannelCallRetryState* retry_state);

  // Returns the index into pending_batches_ to be used for batch.
  static size_t GetBatchIndex(grpc_transport_stream_op_batch* batch);
  void PendingBatchesAdd(grpc_transport_stream_op_batch* batch);
  void PendingBatchClear(PendingBatch* pending);
  void MaybeClearPendingBatch(PendingBatch* pending);
  static void FailPendingBatchInCallCombiner(void* arg, grpc_error* error);
  // A predicate type and some useful implementations for PendingBatchesFail().
  typedef bool (*YieldCallCombinerPredicate)(
      const CallCombinerClosureList& closures);
  static bool YieldCallCombiner(const CallCombinerClosureList& /*closures*/) {
    return true;
  }
  static bool NoYieldCallCombiner(const CallCombinerClosureList& /*closures*/) {
    return false;
  }
  static bool YieldCallCombinerIfPendingBatchesFound(
      const CallCombinerClosureList& closures) {
    return closures.size() > 0;
  }
  // Fails all pending batches.
  // If yield_call_combiner_predicate returns true, assumes responsibility for
  // yielding the call combiner.
  void PendingBatchesFail(
      grpc_error* error,
      YieldCallCombinerPredicate yield_call_combiner_predicate);
  static void ResumePendingBatchInCallCombiner(void* arg, grpc_error* ignored);
  // Resumes all pending batches on lb_call_.
  void PendingBatchesResume();
  // Returns a pointer to the first pending batch for which predicate(batch)
  // returns true, or null if not found.
  template <typename Predicate>
  PendingBatch* PendingBatchFind(const char* log_message, Predicate predicate);

  // Commits the call so that no further retry attempts will be performed.
  void RetryCommit(SubchannelCallRetryState* retry_state);
  // Starts a retry after appropriate back-off.
  void DoRetry(SubchannelCallRetryState* retry_state,
               grpc_millis server_pushback_ms);
  // Returns true if the call is being retried.
  bool MaybeRetry(SubchannelCallBatchData* batch_data, grpc_status_code status,
                  grpc_mdelem* server_pushback_md, bool is_lb_drop);

  // Invokes recv_initial_metadata_ready for a subchannel batch.
  static void InvokeRecvInitialMetadataCallback(void* arg, grpc_error* error);
  // Intercepts recv_initial_metadata_ready callback for retries.
  // Commits the call and returns the initial metadata up the stack.
  static void RecvInitialMetadataReady(void* arg, grpc_error* error);

  // Invokes recv_message_ready for a subchannel batch.
  static void InvokeRecvMessageCallback(void* arg, grpc_error* error);
  // Intercepts recv_message_ready callback for retries.
  // Commits the call and returns the message up the stack.
  static void RecvMessageReady(void* arg, grpc_error* error);

  // Sets *status, *server_pushback_md, and *is_lb_drop based on md_batch
  // and error.
  void GetCallStatus(grpc_metadata_batch* md_batch, grpc_error* error,
                     grpc_status_code* status, grpc_mdelem** server_pushback_md,
                     bool* is_lb_drop);
  // Adds recv_trailing_metadata_ready closure to closures.
  void AddClosureForRecvTrailingMetadataReady(
      SubchannelCallBatchData* batch_data, grpc_error* error,
      CallCombinerClosureList* closures);
  // Adds any necessary closures for deferred recv_initial_metadata and
  // recv_message callbacks to closures.
  static void AddClosuresForDeferredRecvCallbacks(
      SubchannelCallBatchData* batch_data,
      SubchannelCallRetryState* retry_state, CallCombinerClosureList* closures);
  // Returns true if any op in the batch was not yet started.
  // Only looks at send ops, since recv ops are always started immediately.
  bool PendingBatchIsUnstarted(PendingBatch* pending,
                               SubchannelCallRetryState* retry_state);
  // For any pending batch containing an op that has not yet been started,
  // adds the pending batch's completion closures to closures.
  void AddClosuresToFailUnstartedPendingBatches(
      SubchannelCallRetryState* retry_state, grpc_error* error,
      CallCombinerClosureList* closures);
  // Runs necessary closures upon completion of a call attempt.
  void RunClosuresForCompletedCall(SubchannelCallBatchData* batch_data,
                                   grpc_error* error);
  // Intercepts recv_trailing_metadata_ready callback for retries.
  // Commits the call and returns the trailing metadata up the stack.
  static void RecvTrailingMetadataReady(void* arg, grpc_error* error);

  // Adds the on_complete closure for the pending batch completed in
  // batch_data to closures.
  void AddClosuresForCompletedPendingBatch(SubchannelCallBatchData* batch_data,
                                           grpc_error* error,
                                           CallCombinerClosureList* closures);

  // If there are any cached ops to replay or pending ops to start on the
  // subchannel call, adds a closure to closures to invoke
  // StartRetriableSubchannelBatches().
  void AddClosuresForReplayOrPendingSendOps(
      SubchannelCallBatchData* batch_data,
      SubchannelCallRetryState* retry_state, CallCombinerClosureList* closures);

  // Callback used to intercept on_complete from subchannel calls.
  // Called only when retries are enabled.
  static void OnComplete(void* arg, grpc_error* error);

  static void StartBatchInCallCombiner(void* arg, grpc_error* ignored);
  // Adds a closure to closures that will execute batch in the call combiner.
  void AddClosureForSubchannelBatch(grpc_transport_stream_op_batch* batch,
                                    CallCombinerClosureList* closures);
  // Adds retriable send_initial_metadata op to batch_data.
  void AddRetriableSendInitialMetadataOp(SubchannelCallRetryState* retry_state,
                                         SubchannelCallBatchData* batch_data);
  // Adds retriable send_message op to batch_data.
  void AddRetriableSendMessageOp(SubchannelCallRetryState* retry_state,
                                 SubchannelCallBatchData* batch_data);
  // Adds retriable send_trailing_metadata op to batch_data.
  void AddRetriableSendTrailingMetadataOp(SubchannelCallRetryState* retry_state,
                                          SubchannelCallBatchData* batch_data);
  // Adds retriable recv_initial_metadata op to batch_data.
  void AddRetriableRecvInitialMetadataOp(SubchannelCallRetryState* retry_state,
                                         SubchannelCallBatchData* batch_data);
  // Adds retriable recv_message op to batch_data.
  void AddRetriableRecvMessageOp(SubchannelCallRetryState* retry_state,
                                 SubchannelCallBatchData* batch_data);
  // Adds retriable recv_trailing_metadata op to batch_data.
  void AddRetriableRecvTrailingMetadataOp(SubchannelCallRetryState* retry_state,
                                          SubchannelCallBatchData* batch_data);
  // Helper function used to start a recv_trailing_metadata batch.  This
  // is used in the case where a recv_initial_metadata or recv_message
  // op fails in a way that we know the call is over but when the application
  // has not yet started its own recv_trailing_metadata op.
  void StartInternalRecvTrailingMetadata();
  // If there are any cached send ops that need to be replayed on the
  // current subchannel call, creates and returns a new subchannel batch
  // to replay those ops.  Otherwise, returns nullptr.
  SubchannelCallBatchData* MaybeCreateSubchannelBatchForReplay(
      SubchannelCallRetryState* retry_state);
  // Adds subchannel batches for pending batches to closures.
  void AddSubchannelBatchesForPendingBatches(
      SubchannelCallRetryState* retry_state, CallCombinerClosureList* closures);
  // Constructs and starts whatever subchannel batches are needed on the
  // subchannel call.
  static void StartRetriableSubchannelBatches(void* arg, grpc_error* ignored);

  static void CreateLbCall(void* arg, grpc_error* error);

  RetryFilter* chand_;
  grpc_polling_entity* pollent_;
  RefCountedPtr<ServerRetryThrottleData> retry_throttle_data_;
  const RetryMethodConfig* retry_policy_ = nullptr;
  BackOff retry_backoff_;

  grpc_slice path_;  // Request path.
  gpr_cycle_counter call_start_time_;
  grpc_millis deadline_;
  Arena* arena_;
  grpc_call_stack* owning_call_;
  CallCombiner* call_combiner_;
  grpc_call_context_element* call_context_;

  grpc_closure retry_closure_;

  // TODO(roth): Move this into the SubchannelCallRetryState struct as
  // part of implementing hedging.
  RefCountedPtr<ClientChannel::LoadBalancedCall> lb_call_;

  // Batches are added to this list when received from above.
  // They are removed when we are done handling the batch (i.e., when
  // either we have invoked all of the batch's callbacks or we have
  // passed the batch down to the LB call and are not intercepting any of
  // its callbacks).
  // TODO(roth): Now that the retry code is split out into its own call
  // object, revamp this to work in a cleaner way, since we no longer need
  // for batches to ever wait for name resolution or LB picks.
  PendingBatch pending_batches_[MAX_PENDING_BATCHES];
  bool pending_send_initial_metadata_ : 1;
  bool pending_send_message_ : 1;
  bool pending_send_trailing_metadata_ : 1;

  // Set when we get a cancel_stream op.
  grpc_error* cancel_error_ = GRPC_ERROR_NONE;

  // Retry state.
  bool enable_retries_ : 1;
  bool retry_committed_ : 1;
  bool last_attempt_got_server_pushback_ : 1;
  int num_attempts_completed_ = 0;
  size_t bytes_buffered_for_retry_ = 0;
  grpc_timer retry_timer_;

  // The number of pending retriable subchannel batches containing send ops.
  // We hold a ref to the call stack while this is non-zero, since replay
  // batches may not complete until after all callbacks have been returned
  // to the surface, and we need to make sure that the call is not destroyed
  // until all of these batches have completed.
  // Note that we actually only need to track replay batches, but it's
  // easier to track all batches with send ops.
  int num_pending_retriable_subchannel_send_batches_ = 0;

  // Cached data for retrying send ops.
  // send_initial_metadata
  bool seen_send_initial_metadata_ = false;
  grpc_linked_mdelem* send_initial_metadata_storage_ = nullptr;
  grpc_metadata_batch send_initial_metadata_;
  uint32_t send_initial_metadata_flags_;
  gpr_atm* peer_string_;
  // send_message
  // When we get a send_message op, we replace the original byte stream
  // with a CachingByteStream that caches the slices to a local buffer for
  // use in retries.
  // Note: We inline the cache for the first 3 send_message ops and use
  // dynamic allocation after that.  This number was essentially picked
  // at random; it could be changed in the future to tune performance.
  absl::InlinedVector<ByteStreamCache*, 3> send_messages_;
  // send_trailing_metadata
  bool seen_send_trailing_metadata_ = false;
  grpc_linked_mdelem* send_trailing_metadata_storage_ = nullptr;
  grpc_metadata_batch send_trailing_metadata_;
};

//
// CallData vtable functions
//

grpc_error* RetryFilter::CallData::Init(grpc_call_element* elem,
                                        const grpc_call_element_args* args) {
  auto* chand = static_cast<RetryFilter*>(elem->channel_data);
  new (elem->call_data) CallData(chand, *args);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p: created call=%p", chand, elem->call_data);
  }
  return GRPC_ERROR_NONE;
}

void RetryFilter::CallData::Destroy(grpc_call_element* elem,
                                    const grpc_call_final_info* /*final_info*/,
                                    grpc_closure* then_schedule_closure) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  RefCountedPtr<SubchannelCall> subchannel_call;
  if (GPR_LIKELY(calld->lb_call_ != nullptr)) {
    subchannel_call = calld->lb_call_->subchannel_call();
  }
  calld->~CallData();
  if (GPR_LIKELY(subchannel_call != nullptr)) {
    subchannel_call->SetAfterCallStackDestroy(then_schedule_closure);
  } else {
    // TODO(yashkt) : This can potentially be a Closure::Run
    ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, GRPC_ERROR_NONE);
  }
}

void RetryFilter::CallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  calld->StartTransportStreamOpBatch(batch);
}

void RetryFilter::CallData::SetPollent(grpc_call_element* elem,
                                       grpc_polling_entity* pollent) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  calld->pollent_ = pollent;
}

//
// CallData implementation
//

const RetryMethodConfig* GetRetryPolicy(
    const grpc_call_context_element* context) {
  if (context == nullptr) return nullptr;
  auto* svc_cfg_call_data = static_cast<ServiceConfigCallData*>(
      context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
  if (svc_cfg_call_data == nullptr) return nullptr;
  return static_cast<const RetryMethodConfig*>(
      svc_cfg_call_data->GetMethodParsedConfig(
          RetryServiceConfigParser::ParserIndex()));
}

RetryFilter::CallData::CallData(RetryFilter* chand,
                                const grpc_call_element_args& args)
    : chand_(chand),
      retry_throttle_data_(chand->retry_throttle_data_),
      retry_policy_(GetRetryPolicy(args.context)),
      retry_backoff_(
          BackOff::Options()
              .set_initial_backoff(retry_policy_ == nullptr
                                       ? 0
                                       : retry_policy_->initial_backoff())
              .set_multiplier(retry_policy_ == nullptr
                                  ? 0
                                  : retry_policy_->backoff_multiplier())
              .set_jitter(RETRY_BACKOFF_JITTER)
              .set_max_backoff(
                  retry_policy_ == nullptr ? 0 : retry_policy_->max_backoff())),
      path_(grpc_slice_ref_internal(args.path)),
      call_start_time_(args.start_time),
      deadline_(args.deadline),
      arena_(args.arena),
      owning_call_(args.call_stack),
      call_combiner_(args.call_combiner),
      call_context_(args.context),
      pending_send_initial_metadata_(false),
      pending_send_message_(false),
      pending_send_trailing_metadata_(false),
      enable_retries_(true),
      retry_committed_(false),
      last_attempt_got_server_pushback_(false) {}

RetryFilter::CallData::~CallData() {
  grpc_slice_unref_internal(path_);
  GRPC_ERROR_UNREF(cancel_error_);
  // Make sure there are no remaining pending batches.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    GPR_ASSERT(pending_batches_[i].batch == nullptr);
  }
}

void RetryFilter::CallData::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  // If we've previously been cancelled, immediately fail any new batches.
  if (GPR_UNLIKELY(cancel_error_ != GRPC_ERROR_NONE)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: failing batch with error: %s",
              chand_, this, grpc_error_string(cancel_error_));
    }
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(cancel_error_), call_combiner_);
    return;
  }
  // Handle cancellation.
  if (GPR_UNLIKELY(batch->cancel_stream)) {
    // Stash a copy of cancel_error in our call data, so that we can use
    // it for subsequent operations.  This ensures that if the call is
    // cancelled before any batches are passed down (e.g., if the deadline
    // is in the past when the call starts), we can return the right
    // error to the caller when the first batch does get passed down.
    GRPC_ERROR_UNREF(cancel_error_);
    cancel_error_ = GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: recording cancel_error=%s", chand_,
              this, grpc_error_string(cancel_error_));
    }
    // If we do not have an LB call (i.e., a pick has not yet been started),
    // fail all pending batches.  Otherwise, send the cancellation down to the
    // LB call.
    if (lb_call_ == nullptr) {
      // TODO(roth): If there is a pending retry callback, do we need to
      // cancel it here?
      PendingBatchesFail(GRPC_ERROR_REF(cancel_error_), NoYieldCallCombiner);
      // Note: This will release the call combiner.
      grpc_transport_stream_op_batch_finish_with_failure(
          batch, GRPC_ERROR_REF(cancel_error_), call_combiner_);
    } else {
      // Note: This will release the call combiner.
      lb_call_->StartTransportStreamOpBatch(batch);
    }
    return;
  }
  // Add the batch to the pending list.
  PendingBatchesAdd(batch);
  // Create LB call if needed.
  // TODO(roth): If we get a new batch from the surface after the
  // initial retry attempt has failed, while the retry timer is pending,
  // we should queue the batch and not try to send it immediately.
  if (lb_call_ == nullptr) {
    // We do not yet have an LB call, so create one.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: creating LB call", chand_, this);
    }
    CreateLbCall(this, GRPC_ERROR_NONE);
    return;
  }
  // Send batches to LB call.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: starting batch on lb_call=%p", chand_,
            this, lb_call_.get());
  }
  PendingBatchesResume();
}

//
// send op data caching
//

void RetryFilter::CallData::MaybeCacheSendOpsForBatch(PendingBatch* pending) {
  if (pending->send_ops_cached) return;
  pending->send_ops_cached = true;
  grpc_transport_stream_op_batch* batch = pending->batch;
  // Save a copy of metadata for send_initial_metadata ops.
  if (batch->send_initial_metadata) {
    seen_send_initial_metadata_ = true;
    GPR_ASSERT(send_initial_metadata_storage_ == nullptr);
    grpc_metadata_batch* send_initial_metadata =
        batch->payload->send_initial_metadata.send_initial_metadata;
    send_initial_metadata_storage_ =
        static_cast<grpc_linked_mdelem*>(arena_->Alloc(
            sizeof(grpc_linked_mdelem) * send_initial_metadata->list.count));
    grpc_metadata_batch_copy(send_initial_metadata, &send_initial_metadata_,
                             send_initial_metadata_storage_);
    send_initial_metadata_flags_ =
        batch->payload->send_initial_metadata.send_initial_metadata_flags;
    peer_string_ = batch->payload->send_initial_metadata.peer_string;
  }
  // Set up cache for send_message ops.
  if (batch->send_message) {
    ByteStreamCache* cache = arena_->New<ByteStreamCache>(
        std::move(batch->payload->send_message.send_message));
    send_messages_.push_back(cache);
  }
  // Save metadata batch for send_trailing_metadata ops.
  if (batch->send_trailing_metadata) {
    seen_send_trailing_metadata_ = true;
    GPR_ASSERT(send_trailing_metadata_storage_ == nullptr);
    grpc_metadata_batch* send_trailing_metadata =
        batch->payload->send_trailing_metadata.send_trailing_metadata;
    send_trailing_metadata_storage_ =
        static_cast<grpc_linked_mdelem*>(arena_->Alloc(
            sizeof(grpc_linked_mdelem) * send_trailing_metadata->list.count));
    grpc_metadata_batch_copy(send_trailing_metadata, &send_trailing_metadata_,
                             send_trailing_metadata_storage_);
  }
}

void RetryFilter::CallData::FreeCachedSendInitialMetadata() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: destroying send_initial_metadata",
            chand_, this);
  }
  grpc_metadata_batch_destroy(&send_initial_metadata_);
}

void RetryFilter::CallData::FreeCachedSendMessage(size_t idx) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: destroying send_messages[%" PRIuPTR "]", chand_,
            this, idx);
  }
  send_messages_[idx]->Destroy();
}

void RetryFilter::CallData::FreeCachedSendTrailingMetadata() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand_=%p calld=%p: destroying send_trailing_metadata",
            chand_, this);
  }
  grpc_metadata_batch_destroy(&send_trailing_metadata_);
}

void RetryFilter::CallData::FreeCachedSendOpDataAfterCommit(
    SubchannelCallRetryState* retry_state) {
  if (retry_state->completed_send_initial_metadata) {
    FreeCachedSendInitialMetadata();
  }
  for (size_t i = 0; i < retry_state->completed_send_message_count; ++i) {
    FreeCachedSendMessage(i);
  }
  if (retry_state->completed_send_trailing_metadata) {
    FreeCachedSendTrailingMetadata();
  }
}

void RetryFilter::CallData::FreeCachedSendOpDataForCompletedBatch(
    SubchannelCallBatchData* batch_data,
    SubchannelCallRetryState* retry_state) {
  if (batch_data->batch.send_initial_metadata) {
    FreeCachedSendInitialMetadata();
  }
  if (batch_data->batch.send_message) {
    FreeCachedSendMessage(retry_state->completed_send_message_count - 1);
  }
  if (batch_data->batch.send_trailing_metadata) {
    FreeCachedSendTrailingMetadata();
  }
}

//
// pending_batches management
//

size_t RetryFilter::CallData::GetBatchIndex(
    grpc_transport_stream_op_batch* batch) {
  // Note: It is important the send_initial_metadata be the first entry
  // here, since the code in pick_subchannel_locked() assumes it will be.
  if (batch->send_initial_metadata) return 0;
  if (batch->send_message) return 1;
  if (batch->send_trailing_metadata) return 2;
  if (batch->recv_initial_metadata) return 3;
  if (batch->recv_message) return 4;
  if (batch->recv_trailing_metadata) return 5;
  GPR_UNREACHABLE_CODE(return (size_t)-1);
}

// This is called via the call combiner, so access to calld is synchronized.
void RetryFilter::CallData::PendingBatchesAdd(
    grpc_transport_stream_op_batch* batch) {
  const size_t idx = GetBatchIndex(batch);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand_=%p calld=%p: adding pending batch at index %" PRIuPTR,
            chand_, this, idx);
  }
  PendingBatch* pending = &pending_batches_[idx];
  GPR_ASSERT(pending->batch == nullptr);
  pending->batch = batch;
  pending->send_ops_cached = false;
  if (enable_retries_) {
    // Update state in calld about pending batches.
    // Also check if the batch takes us over the retry buffer limit.
    // Note: We don't check the size of trailing metadata here, because
    // gRPC clients do not send trailing metadata.
    if (batch->send_initial_metadata) {
      pending_send_initial_metadata_ = true;
      bytes_buffered_for_retry_ += grpc_metadata_batch_size(
          batch->payload->send_initial_metadata.send_initial_metadata);
    }
    if (batch->send_message) {
      pending_send_message_ = true;
      bytes_buffered_for_retry_ +=
          batch->payload->send_message.send_message->length();
    }
    if (batch->send_trailing_metadata) {
      pending_send_trailing_metadata_ = true;
    }
    if (GPR_UNLIKELY(bytes_buffered_for_retry_ >
                     chand_->per_rpc_retry_buffer_size_)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: exceeded retry buffer size, committing",
                chand_, this);
      }
      SubchannelCallRetryState* retry_state =
          lb_call_ == nullptr ? nullptr
                              : static_cast<SubchannelCallRetryState*>(
                                    lb_call_->GetParentData());
      RetryCommit(retry_state);
      // If we are not going to retry and have not yet started, pretend
      // retries are disabled so that we don't bother with retry overhead.
      if (num_attempts_completed_ == 0) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
          gpr_log(GPR_INFO,
                  "chand=%p calld=%p: disabling retries before first "
                  "attempt",
                  chand_, this);
        }
        // TODO(roth): Treat this as a commit?
        enable_retries_ = false;
      }
    }
  }
}

void RetryFilter::CallData::PendingBatchClear(PendingBatch* pending) {
  if (enable_retries_) {
    if (pending->batch->send_initial_metadata) {
      pending_send_initial_metadata_ = false;
    }
    if (pending->batch->send_message) {
      pending_send_message_ = false;
    }
    if (pending->batch->send_trailing_metadata) {
      pending_send_trailing_metadata_ = false;
    }
  }
  pending->batch = nullptr;
}

void RetryFilter::CallData::MaybeClearPendingBatch(PendingBatch* pending) {
  grpc_transport_stream_op_batch* batch = pending->batch;
  // We clear the pending batch if all of its callbacks have been
  // scheduled and reset to nullptr.
  if (batch->on_complete == nullptr &&
      (!batch->recv_initial_metadata ||
       batch->payload->recv_initial_metadata.recv_initial_metadata_ready ==
           nullptr) &&
      (!batch->recv_message ||
       batch->payload->recv_message.recv_message_ready == nullptr) &&
      (!batch->recv_trailing_metadata ||
       batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready ==
           nullptr)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: clearing pending batch", chand_,
              this);
    }
    PendingBatchClear(pending);
  }
}

// This is called via the call combiner, so access to calld is synchronized.
void RetryFilter::CallData::FailPendingBatchInCallCombiner(void* arg,
                                                           grpc_error* error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  CallData* call = static_cast<CallData*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_transport_stream_op_batch_finish_with_failure(
      batch, GRPC_ERROR_REF(error), call->call_combiner_);
}

// This is called via the call combiner, so access to calld is synchronized.
void RetryFilter::CallData::PendingBatchesFail(
    grpc_error* error,
    YieldCallCombinerPredicate yield_call_combiner_predicate) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i].batch != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: failing %" PRIuPTR " pending batches: %s",
            chand_, this, num_batches, grpc_error_string(error));
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    PendingBatch* pending = &pending_batches_[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr) {
      batch->handler_private.extra_arg = this;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        FailPendingBatchInCallCombiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, GRPC_ERROR_REF(error),
                   "PendingBatchesFail");
      PendingBatchClear(pending);
    }
  }
  if (yield_call_combiner_predicate(closures)) {
    closures.RunClosures(call_combiner_);
  } else {
    closures.RunClosuresWithoutYielding(call_combiner_);
  }
  GRPC_ERROR_UNREF(error);
}

// This is called via the call combiner, so access to calld is synchronized.
void RetryFilter::CallData::ResumePendingBatchInCallCombiner(
    void* arg, grpc_error* /*ignored*/) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  auto* lb_call = static_cast<ClientChannel::LoadBalancedCall*>(
      batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  lb_call->StartTransportStreamOpBatch(batch);
}

// This is called via the call combiner, so access to calld is synchronized.
void RetryFilter::CallData::PendingBatchesResume() {
  if (enable_retries_) {
    StartRetriableSubchannelBatches(this, GRPC_ERROR_NONE);
    return;
  }
  // Retries not enabled; send down batches as-is.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i].batch != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " pending batches on lb_call=%p",
            chand_, this, num_batches, lb_call_.get());
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    PendingBatch* pending = &pending_batches_[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr) {
      batch->handler_private.extra_arg = lb_call_.get();
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        ResumePendingBatchInCallCombiner, batch, nullptr);
      closures.Add(&batch->handler_private.closure, GRPC_ERROR_NONE,
                   "PendingBatchesResume");
      PendingBatchClear(pending);
    }
  }
  // Note: This will release the call combiner.
  closures.RunClosures(call_combiner_);
}

template <typename Predicate>
RetryFilter::CallData::PendingBatch* RetryFilter::CallData::PendingBatchFind(
    const char* log_message, Predicate predicate) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    PendingBatch* pending = &pending_batches_[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr && predicate(batch)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: %s pending batch at index %" PRIuPTR,
                chand_, this, log_message, i);
      }
      return pending;
    }
  }
  return nullptr;
}

//
// retry code
//

void RetryFilter::CallData::RetryCommit(SubchannelCallRetryState* retry_state) {
  if (retry_committed_) return;
  retry_committed_ = true;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: committing retries", chand_, this);
  }
  if (retry_state != nullptr) {
    FreeCachedSendOpDataAfterCommit(retry_state);
  }
}

void RetryFilter::CallData::DoRetry(SubchannelCallRetryState* retry_state,
                                    grpc_millis server_pushback_ms) {
  GPR_ASSERT(retry_policy_ != nullptr);
  // Reset LB call.
  lb_call_.reset();
  // Compute backoff delay.
  grpc_millis next_attempt_time;
  if (server_pushback_ms >= 0) {
    next_attempt_time = ExecCtx::Get()->Now() + server_pushback_ms;
    last_attempt_got_server_pushback_ = true;
  } else {
    if (num_attempts_completed_ == 1 || last_attempt_got_server_pushback_) {
      last_attempt_got_server_pushback_ = false;
    }
    next_attempt_time = retry_backoff_.NextAttemptTime();
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: retrying failed call in %" PRId64 " ms", chand_,
            this, next_attempt_time - ExecCtx::Get()->Now());
  }
  // Schedule retry after computed delay.
  GRPC_CLOSURE_INIT(&retry_closure_, CreateLbCall, this, nullptr);
  grpc_timer_init(&retry_timer_, next_attempt_time, &retry_closure_);
  // Update bookkeeping.
  if (retry_state != nullptr) retry_state->retry_dispatched = true;
}

bool RetryFilter::CallData::MaybeRetry(SubchannelCallBatchData* batch_data,
                                       grpc_status_code status,
                                       grpc_mdelem* server_pushback_md,
                                       bool is_lb_drop) {
  // LB drops always inhibit retries.
  if (is_lb_drop) return false;
  // Get retry policy.
  if (retry_policy_ == nullptr) return false;
  // If we've already dispatched a retry from this call, return true.
  // This catches the case where the batch has multiple callbacks
  // (i.e., it includes either recv_message or recv_initial_metadata).
  SubchannelCallRetryState* retry_state = nullptr;
  if (batch_data != nullptr) {
    retry_state = static_cast<SubchannelCallRetryState*>(
        batch_data->lb_call->GetParentData());
    if (retry_state->retry_dispatched) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: retry already dispatched", chand_,
                this);
      }
      return true;
    }
  }
  // Check status.
  if (GPR_LIKELY(status == GRPC_STATUS_OK)) {
    if (retry_throttle_data_ != nullptr) {
      retry_throttle_data_->RecordSuccess();
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: call succeeded", chand_, this);
    }
    return false;
  }
  // Status is not OK.  Check whether the status is retryable.
  if (!retry_policy_->retryable_status_codes().Contains(status)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: status %s not configured as retryable",
              chand_, this, grpc_status_code_to_string(status));
    }
    return false;
  }
  // Record the failure and check whether retries are throttled.
  // Note that it's important for this check to come after the status
  // code check above, since we should only record failures whose statuses
  // match the configured retryable status codes, so that we don't count
  // things like failures due to malformed requests (INVALID_ARGUMENT).
  // Conversely, it's important for this to come before the remaining
  // checks, so that we don't fail to record failures due to other factors.
  if (retry_throttle_data_ != nullptr &&
      !retry_throttle_data_->RecordFailure()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retries throttled", chand_, this);
    }
    return false;
  }
  // Check whether the call is committed.
  if (retry_committed_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retries already committed", chand_,
              this);
    }
    return false;
  }
  // Check whether we have retries remaining.
  ++num_attempts_completed_;
  if (num_attempts_completed_ >= retry_policy_->max_attempts()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: exceeded %d retry attempts", chand_,
              this, retry_policy_->max_attempts());
    }
    return false;
  }
  // If the call was cancelled from the surface, don't retry.
  if (cancel_error_ != GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: call cancelled from surface, not "
              "retrying",
              chand_, this);
    }
    return false;
  }
  // Check server push-back.
  grpc_millis server_pushback_ms = -1;
  if (server_pushback_md != nullptr) {
    // If the value is "-1" or any other unparseable string, we do not retry.
    uint32_t ms;
    if (!grpc_parse_slice_to_uint32(GRPC_MDVALUE(*server_pushback_md), &ms)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: not retrying due to server push-back",
                chand_, this);
      }
      return false;
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: server push-back: retry in %u ms",
                chand_, this, ms);
      }
      server_pushback_ms = static_cast<grpc_millis>(ms);
    }
  }
  DoRetry(retry_state, server_pushback_ms);
  return true;
}

//
// RetryFilter::CallData::SubchannelCallBatchData
//

RetryFilter::CallData::SubchannelCallBatchData*
RetryFilter::CallData::SubchannelCallBatchData::Create(CallData* call,
                                                       int refcount,
                                                       bool set_on_complete) {
  return call->arena_->New<SubchannelCallBatchData>(call, refcount,
                                                    set_on_complete);
}

RetryFilter::CallData::SubchannelCallBatchData::SubchannelCallBatchData(
    CallData* call, int refcount, bool set_on_complete)
    : call(call), lb_call(call->lb_call_) {
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(lb_call->GetParentData());
  batch.payload = &retry_state->batch_payload;
  gpr_ref_init(&refs, refcount);
  if (set_on_complete) {
    GRPC_CLOSURE_INIT(&on_complete, RetryFilter::CallData::OnComplete, this,
                      grpc_schedule_on_exec_ctx);
    batch.on_complete = &on_complete;
  }
  GRPC_CALL_STACK_REF(call->owning_call_, "batch_data");
}

void RetryFilter::CallData::SubchannelCallBatchData::Destroy() {
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(lb_call->GetParentData());
  if (batch.send_initial_metadata) {
    grpc_metadata_batch_destroy(&retry_state->send_initial_metadata);
  }
  if (batch.send_trailing_metadata) {
    grpc_metadata_batch_destroy(&retry_state->send_trailing_metadata);
  }
  if (batch.recv_initial_metadata) {
    grpc_metadata_batch_destroy(&retry_state->recv_initial_metadata);
  }
  if (batch.recv_trailing_metadata) {
    grpc_metadata_batch_destroy(&retry_state->recv_trailing_metadata);
  }
  lb_call.reset();
  GRPC_CALL_STACK_UNREF(call->owning_call_, "batch_data");
}

//
// recv_initial_metadata callback handling
//

void RetryFilter::CallData::InvokeRecvInitialMetadataCallback(
    void* arg, grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  // Find pending batch.
  PendingBatch* pending = batch_data->call->PendingBatchFind(
      "invoking recv_initial_metadata_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_initial_metadata &&
               batch->payload->recv_initial_metadata
                       .recv_initial_metadata_ready != nullptr;
      });
  GPR_ASSERT(pending != nullptr);
  // Return metadata.
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->lb_call->GetParentData());
  grpc_metadata_batch_move(
      &retry_state->recv_initial_metadata,
      pending->batch->payload->recv_initial_metadata.recv_initial_metadata);
  // Update bookkeeping.
  // Note: Need to do this before invoking the callback, since invoking
  // the callback will result in yielding the call combiner.
  grpc_closure* recv_initial_metadata_ready =
      pending->batch->payload->recv_initial_metadata
          .recv_initial_metadata_ready;
  pending->batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
      nullptr;
  batch_data->call->MaybeClearPendingBatch(pending);
  batch_data->Unref();
  // Invoke callback.
  Closure::Run(DEBUG_LOCATION, recv_initial_metadata_ready,
               GRPC_ERROR_REF(error));
}

void RetryFilter::CallData::RecvInitialMetadataReady(void* arg,
                                                     grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  CallData* call = batch_data->call;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: got recv_initial_metadata_ready, error=%s",
            call->chand_, call, grpc_error_string(error));
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->lb_call->GetParentData());
  retry_state->completed_recv_initial_metadata = true;
  // If a retry was already dispatched, then we're not going to use the
  // result of this recv_initial_metadata op, so do nothing.
  if (retry_state->retry_dispatched) {
    GRPC_CALL_COMBINER_STOP(
        call->call_combiner_,
        "recv_initial_metadata_ready after retry dispatched");
    return;
  }
  // If we got an error or a Trailers-Only response and have not yet gotten
  // the recv_trailing_metadata_ready callback, then defer propagating this
  // callback back to the surface.  We can evaluate whether to retry when
  // recv_trailing_metadata comes back.
  if (GPR_UNLIKELY((retry_state->trailing_metadata_available ||
                    error != GRPC_ERROR_NONE) &&
                   !retry_state->completed_recv_trailing_metadata)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: deferring recv_initial_metadata_ready "
              "(Trailers-Only)",
              call->chand_, call);
    }
    retry_state->recv_initial_metadata_ready_deferred_batch = batch_data;
    retry_state->recv_initial_metadata_error = GRPC_ERROR_REF(error);
    if (!retry_state->started_recv_trailing_metadata) {
      // recv_trailing_metadata not yet started by application; start it
      // ourselves to get status.
      call->StartInternalRecvTrailingMetadata();
    } else {
      GRPC_CALL_COMBINER_STOP(
          call->call_combiner_,
          "recv_initial_metadata_ready trailers-only or error");
    }
    return;
  }
  // Received valid initial metadata, so commit the call.
  call->RetryCommit(retry_state);
  // Invoke the callback to return the result to the surface.
  // Manually invoking a callback function; it does not take ownership of error.
  call->InvokeRecvInitialMetadataCallback(batch_data, error);
}

//
// recv_message callback handling
//

void RetryFilter::CallData::InvokeRecvMessageCallback(void* arg,
                                                      grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  CallData* call = batch_data->call;
  // Find pending op.
  PendingBatch* pending = call->PendingBatchFind(
      "invoking recv_message_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_message &&
               batch->payload->recv_message.recv_message_ready != nullptr;
      });
  GPR_ASSERT(pending != nullptr);
  // Return payload.
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->lb_call->GetParentData());
  *pending->batch->payload->recv_message.recv_message =
      std::move(retry_state->recv_message);
  // Update bookkeeping.
  // Note: Need to do this before invoking the callback, since invoking
  // the callback will result in yielding the call combiner.
  grpc_closure* recv_message_ready =
      pending->batch->payload->recv_message.recv_message_ready;
  pending->batch->payload->recv_message.recv_message_ready = nullptr;
  call->MaybeClearPendingBatch(pending);
  batch_data->Unref();
  // Invoke callback.
  Closure::Run(DEBUG_LOCATION, recv_message_ready, GRPC_ERROR_REF(error));
}

void RetryFilter::CallData::RecvMessageReady(void* arg, grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  CallData* call = batch_data->call;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: got recv_message_ready, error=%s",
            call->chand_, call, grpc_error_string(error));
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->lb_call->GetParentData());
  ++retry_state->completed_recv_message_count;
  // If a retry was already dispatched, then we're not going to use the
  // result of this recv_message op, so do nothing.
  if (retry_state->retry_dispatched) {
    GRPC_CALL_COMBINER_STOP(call->call_combiner_,
                            "recv_message_ready after retry dispatched");
    return;
  }
  // If we got an error or the payload was nullptr and we have not yet gotten
  // the recv_trailing_metadata_ready callback, then defer propagating this
  // callback back to the surface.  We can evaluate whether to retry when
  // recv_trailing_metadata comes back.
  if (GPR_UNLIKELY(
          (retry_state->recv_message == nullptr || error != GRPC_ERROR_NONE) &&
          !retry_state->completed_recv_trailing_metadata)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: deferring recv_message_ready (nullptr "
              "message and recv_trailing_metadata pending)",
              call->chand_, call);
    }
    retry_state->recv_message_ready_deferred_batch = batch_data;
    retry_state->recv_message_error = GRPC_ERROR_REF(error);
    if (!retry_state->started_recv_trailing_metadata) {
      // recv_trailing_metadata not yet started by application; start it
      // ourselves to get status.
      call->StartInternalRecvTrailingMetadata();
    } else {
      GRPC_CALL_COMBINER_STOP(call->call_combiner_, "recv_message_ready null");
    }
    return;
  }
  // Received a valid message, so commit the call.
  call->RetryCommit(retry_state);
  // Invoke the callback to return the result to the surface.
  // Manually invoking a callback function; it does not take ownership of error.
  call->InvokeRecvMessageCallback(batch_data, error);
}

//
// recv_trailing_metadata handling
//

void RetryFilter::CallData::GetCallStatus(grpc_metadata_batch* md_batch,
                                          grpc_error* error,
                                          grpc_status_code* status,
                                          grpc_mdelem** server_pushback_md,
                                          bool* is_lb_drop) {
  if (error != GRPC_ERROR_NONE) {
    grpc_error_get_status(error, deadline_, status, nullptr, nullptr, nullptr);
    intptr_t value = 0;
    if (grpc_error_get_int(error, GRPC_ERROR_INT_LB_POLICY_DROP, &value) &&
        value != 0) {
      *is_lb_drop = true;
    }
  } else {
    GPR_ASSERT(md_batch->idx.named.grpc_status != nullptr);
    *status =
        grpc_get_status_code_from_metadata(md_batch->idx.named.grpc_status->md);
    if (md_batch->idx.named.grpc_retry_pushback_ms != nullptr) {
      *server_pushback_md = &md_batch->idx.named.grpc_retry_pushback_ms->md;
    }
  }
  GRPC_ERROR_UNREF(error);
}

void RetryFilter::CallData::AddClosureForRecvTrailingMetadataReady(
    SubchannelCallBatchData* batch_data, grpc_error* error,
    CallCombinerClosureList* closures) {
  // Find pending batch.
  PendingBatch* pending = PendingBatchFind(
      "invoking recv_trailing_metadata for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_trailing_metadata &&
               batch->payload->recv_trailing_metadata
                       .recv_trailing_metadata_ready != nullptr;
      });
  // If we generated the recv_trailing_metadata op internally via
  // StartInternalRecvTrailingMetadata(), then there will be no pending batch.
  if (pending == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  // Return metadata.
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->lb_call->GetParentData());
  grpc_metadata_batch_move(
      &retry_state->recv_trailing_metadata,
      pending->batch->payload->recv_trailing_metadata.recv_trailing_metadata);
  // Add closure.
  closures->Add(pending->batch->payload->recv_trailing_metadata
                    .recv_trailing_metadata_ready,
                error, "recv_trailing_metadata_ready for pending batch");
  // Update bookkeeping.
  pending->batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      nullptr;
  MaybeClearPendingBatch(pending);
}

void RetryFilter::CallData::AddClosuresForDeferredRecvCallbacks(
    SubchannelCallBatchData* batch_data, SubchannelCallRetryState* retry_state,
    CallCombinerClosureList* closures) {
  if (batch_data->batch.recv_trailing_metadata) {
    // Add closure for deferred recv_initial_metadata_ready.
    if (GPR_UNLIKELY(retry_state->recv_initial_metadata_ready_deferred_batch !=
                     nullptr)) {
      GRPC_CLOSURE_INIT(&retry_state->recv_initial_metadata_ready,
                        InvokeRecvInitialMetadataCallback,
                        retry_state->recv_initial_metadata_ready_deferred_batch,
                        grpc_schedule_on_exec_ctx);
      closures->Add(&retry_state->recv_initial_metadata_ready,
                    retry_state->recv_initial_metadata_error,
                    "resuming recv_initial_metadata_ready");
      retry_state->recv_initial_metadata_ready_deferred_batch = nullptr;
    }
    // Add closure for deferred recv_message_ready.
    if (GPR_UNLIKELY(retry_state->recv_message_ready_deferred_batch !=
                     nullptr)) {
      GRPC_CLOSURE_INIT(&retry_state->recv_message_ready,
                        InvokeRecvMessageCallback,
                        retry_state->recv_message_ready_deferred_batch,
                        grpc_schedule_on_exec_ctx);
      closures->Add(&retry_state->recv_message_ready,
                    retry_state->recv_message_error,
                    "resuming recv_message_ready");
      retry_state->recv_message_ready_deferred_batch = nullptr;
    }
  }
}

bool RetryFilter::CallData::PendingBatchIsUnstarted(
    PendingBatch* pending, SubchannelCallRetryState* retry_state) {
  if (pending->batch == nullptr || pending->batch->on_complete == nullptr) {
    return false;
  }
  if (pending->batch->send_initial_metadata &&
      !retry_state->started_send_initial_metadata) {
    return true;
  }
  if (pending->batch->send_message &&
      retry_state->started_send_message_count < send_messages_.size()) {
    return true;
  }
  if (pending->batch->send_trailing_metadata &&
      !retry_state->started_send_trailing_metadata) {
    return true;
  }
  return false;
}

void RetryFilter::CallData::AddClosuresToFailUnstartedPendingBatches(
    SubchannelCallRetryState* retry_state, grpc_error* error,
    CallCombinerClosureList* closures) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    PendingBatch* pending = &pending_batches_[i];
    if (PendingBatchIsUnstarted(pending, retry_state)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: failing unstarted pending batch at "
                "index %" PRIuPTR,
                chand_, this, i);
      }
      closures->Add(pending->batch->on_complete, GRPC_ERROR_REF(error),
                    "failing on_complete for pending batch");
      pending->batch->on_complete = nullptr;
      MaybeClearPendingBatch(pending);
    }
  }
  GRPC_ERROR_UNREF(error);
}

void RetryFilter::CallData::RunClosuresForCompletedCall(
    SubchannelCallBatchData* batch_data, grpc_error* error) {
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->lb_call->GetParentData());
  // Construct list of closures to execute.
  CallCombinerClosureList closures;
  // First, add closure for recv_trailing_metadata_ready.
  AddClosureForRecvTrailingMetadataReady(batch_data, GRPC_ERROR_REF(error),
                                         &closures);
  // If there are deferred recv_initial_metadata_ready or recv_message_ready
  // callbacks, add them to closures.
  AddClosuresForDeferredRecvCallbacks(batch_data, retry_state, &closures);
  // Add closures to fail any pending batches that have not yet been started.
  AddClosuresToFailUnstartedPendingBatches(retry_state, GRPC_ERROR_REF(error),
                                           &closures);
  // Don't need batch_data anymore.
  batch_data->Unref();
  // Schedule all of the closures identified above.
  // Note: This will release the call combiner.
  closures.RunClosures(call_combiner_);
  GRPC_ERROR_UNREF(error);
}

void RetryFilter::CallData::RecvTrailingMetadataReady(void* arg,
                                                      grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  CallData* call = batch_data->call;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: got recv_trailing_metadata_ready, error=%s",
            call->chand_, call, grpc_error_string(error));
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->lb_call->GetParentData());
  retry_state->completed_recv_trailing_metadata = true;
  // Get the call's status and check for server pushback metadata.
  grpc_status_code status = GRPC_STATUS_OK;
  grpc_mdelem* server_pushback_md = nullptr;
  grpc_metadata_batch* md_batch =
      batch_data->batch.payload->recv_trailing_metadata.recv_trailing_metadata;
  bool is_lb_drop = false;
  call->GetCallStatus(md_batch, GRPC_ERROR_REF(error), &status,
                      &server_pushback_md, &is_lb_drop);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: call finished, status=%s is_lb_drop=%d",
            call->chand_, call, grpc_status_code_to_string(status), is_lb_drop);
  }
  // Check if we should retry.
  if (call->MaybeRetry(batch_data, status, server_pushback_md, is_lb_drop)) {
    // Unref batch_data for deferred recv_initial_metadata_ready or
    // recv_message_ready callbacks, if any.
    if (retry_state->recv_initial_metadata_ready_deferred_batch != nullptr) {
      batch_data->Unref();
      GRPC_ERROR_UNREF(retry_state->recv_initial_metadata_error);
    }
    if (retry_state->recv_message_ready_deferred_batch != nullptr) {
      batch_data->Unref();
      GRPC_ERROR_UNREF(retry_state->recv_message_error);
    }
    batch_data->Unref();
    return;
  }
  // Not retrying, so commit the call.
  call->RetryCommit(retry_state);
  // Run any necessary closures.
  call->RunClosuresForCompletedCall(batch_data, GRPC_ERROR_REF(error));
}

//
// on_complete callback handling
//

void RetryFilter::CallData::AddClosuresForCompletedPendingBatch(
    SubchannelCallBatchData* batch_data, grpc_error* error,
    CallCombinerClosureList* closures) {
  PendingBatch* pending = PendingBatchFind(
      "completed", [batch_data](grpc_transport_stream_op_batch* batch) {
        // Match the pending batch with the same set of send ops as the
        // subchannel batch we've just completed.
        return batch->on_complete != nullptr &&
               batch_data->batch.send_initial_metadata ==
                   batch->send_initial_metadata &&
               batch_data->batch.send_message == batch->send_message &&
               batch_data->batch.send_trailing_metadata ==
                   batch->send_trailing_metadata;
      });
  // If batch_data is a replay batch, then there will be no pending
  // batch to complete.
  if (pending == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  // Add closure.
  closures->Add(pending->batch->on_complete, error,
                "on_complete for pending batch");
  pending->batch->on_complete = nullptr;
  MaybeClearPendingBatch(pending);
}

void RetryFilter::CallData::AddClosuresForReplayOrPendingSendOps(
    SubchannelCallBatchData* batch_data, SubchannelCallRetryState* retry_state,
    CallCombinerClosureList* closures) {
  bool have_pending_send_message_ops =
      retry_state->started_send_message_count < send_messages_.size();
  bool have_pending_send_trailing_metadata_op =
      seen_send_trailing_metadata_ &&
      !retry_state->started_send_trailing_metadata;
  if (!have_pending_send_message_ops &&
      !have_pending_send_trailing_metadata_op) {
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      PendingBatch* pending = &pending_batches_[i];
      grpc_transport_stream_op_batch* batch = pending->batch;
      if (batch == nullptr || pending->send_ops_cached) continue;
      if (batch->send_message) have_pending_send_message_ops = true;
      if (batch->send_trailing_metadata) {
        have_pending_send_trailing_metadata_op = true;
      }
    }
  }
  if (have_pending_send_message_ops || have_pending_send_trailing_metadata_op) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: starting next batch for pending send op(s)",
              chand_, this);
    }
    GRPC_CLOSURE_INIT(&batch_data->batch.handler_private.closure,
                      StartRetriableSubchannelBatches, this,
                      grpc_schedule_on_exec_ctx);
    closures->Add(&batch_data->batch.handler_private.closure, GRPC_ERROR_NONE,
                  "starting next batch for send_* op(s)");
  }
}

void RetryFilter::CallData::OnComplete(void* arg, grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  CallData* call = batch_data->call;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: got on_complete, error=%s, batch=%s",
            call->chand_, call, grpc_error_string(error),
            grpc_transport_stream_op_batch_string(&batch_data->batch).c_str());
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->lb_call->GetParentData());
  // Update bookkeeping in retry_state.
  if (batch_data->batch.send_initial_metadata) {
    retry_state->completed_send_initial_metadata = true;
  }
  if (batch_data->batch.send_message) {
    ++retry_state->completed_send_message_count;
  }
  if (batch_data->batch.send_trailing_metadata) {
    retry_state->completed_send_trailing_metadata = true;
  }
  // If the call is committed, free cached data for send ops that we've just
  // completed.
  if (call->retry_committed_) {
    call->FreeCachedSendOpDataForCompletedBatch(batch_data, retry_state);
  }
  // Construct list of closures to execute.
  CallCombinerClosureList closures;
  // If a retry was already dispatched, that means we saw
  // recv_trailing_metadata before this, so we do nothing here.
  // Otherwise, invoke the callback to return the result to the surface.
  if (!retry_state->retry_dispatched) {
    // Add closure for the completed pending batch, if any.
    call->AddClosuresForCompletedPendingBatch(batch_data, GRPC_ERROR_REF(error),
                                              &closures);
    // If needed, add a callback to start any replay or pending send ops on
    // the subchannel call.
    if (!retry_state->completed_recv_trailing_metadata) {
      call->AddClosuresForReplayOrPendingSendOps(batch_data, retry_state,
                                                 &closures);
    }
  }
  // Track number of pending subchannel send batches and determine if this
  // was the last one.
  --call->num_pending_retriable_subchannel_send_batches_;
  const bool last_send_batch_complete =
      call->num_pending_retriable_subchannel_send_batches_ == 0;
  // Don't need batch_data anymore.
  batch_data->Unref();
  // Schedule all of the closures identified above.
  // Note: This yeilds the call combiner.
  closures.RunClosures(call->call_combiner_);
  // If this was the last subchannel send batch, unref the call stack.
  if (last_send_batch_complete) {
    GRPC_CALL_STACK_UNREF(call->owning_call_, "subchannel_send_batches");
  }
}

//
// subchannel batch construction
//

void RetryFilter::CallData::StartBatchInCallCombiner(void* arg,
                                                     grpc_error* /*ignored*/) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  auto* lb_call = static_cast<ClientChannel::LoadBalancedCall*>(
      batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  lb_call->StartTransportStreamOpBatch(batch);
}

void RetryFilter::CallData::AddClosureForSubchannelBatch(
    grpc_transport_stream_op_batch* batch, CallCombinerClosureList* closures) {
  batch->handler_private.extra_arg = lb_call_.get();
  GRPC_CLOSURE_INIT(&batch->handler_private.closure, StartBatchInCallCombiner,
                    batch, grpc_schedule_on_exec_ctx);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: starting subchannel batch: %s",
            chand_, this, grpc_transport_stream_op_batch_string(batch).c_str());
  }
  closures->Add(&batch->handler_private.closure, GRPC_ERROR_NONE,
                "start_subchannel_batch");
}

void RetryFilter::CallData::AddRetriableSendInitialMetadataOp(
    SubchannelCallRetryState* retry_state,
    SubchannelCallBatchData* batch_data) {
  // Maps the number of retries to the corresponding metadata value slice.
  const grpc_slice* retry_count_strings[] = {&GRPC_MDSTR_1, &GRPC_MDSTR_2,
                                             &GRPC_MDSTR_3, &GRPC_MDSTR_4};
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  //
  // If we've already completed one or more attempts, add the
  // grpc-retry-attempts header.
  retry_state->send_initial_metadata_storage =
      static_cast<grpc_linked_mdelem*>(arena_->Alloc(
          sizeof(grpc_linked_mdelem) *
          (send_initial_metadata_.list.count + (num_attempts_completed_ > 0))));
  grpc_metadata_batch_copy(&send_initial_metadata_,
                           &retry_state->send_initial_metadata,
                           retry_state->send_initial_metadata_storage);
  if (GPR_UNLIKELY(retry_state->send_initial_metadata.idx.named
                       .grpc_previous_rpc_attempts != nullptr)) {
    grpc_metadata_batch_remove(&retry_state->send_initial_metadata,
                               GRPC_BATCH_GRPC_PREVIOUS_RPC_ATTEMPTS);
  }
  if (GPR_UNLIKELY(num_attempts_completed_ > 0)) {
    grpc_mdelem retry_md = grpc_mdelem_create(
        GRPC_MDSTR_GRPC_PREVIOUS_RPC_ATTEMPTS,
        *retry_count_strings[num_attempts_completed_ - 1], nullptr);
    grpc_error* error = grpc_metadata_batch_add_tail(
        &retry_state->send_initial_metadata,
        &retry_state
             ->send_initial_metadata_storage[send_initial_metadata_.list.count],
        retry_md, GRPC_BATCH_GRPC_PREVIOUS_RPC_ATTEMPTS);
    if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
      gpr_log(GPR_ERROR, "error adding retry metadata: %s",
              grpc_error_string(error));
      GPR_ASSERT(false);
    }
  }
  retry_state->started_send_initial_metadata = true;
  batch_data->batch.send_initial_metadata = true;
  batch_data->batch.payload->send_initial_metadata.send_initial_metadata =
      &retry_state->send_initial_metadata;
  batch_data->batch.payload->send_initial_metadata.send_initial_metadata_flags =
      send_initial_metadata_flags_;
  batch_data->batch.payload->send_initial_metadata.peer_string = peer_string_;
}

void RetryFilter::CallData::AddRetriableSendMessageOp(
    SubchannelCallRetryState* retry_state,
    SubchannelCallBatchData* batch_data) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting calld->send_messages[%" PRIuPTR "]",
            chand_, this, retry_state->started_send_message_count);
  }
  ByteStreamCache* cache =
      send_messages_[retry_state->started_send_message_count];
  ++retry_state->started_send_message_count;
  retry_state->send_message.Init(cache);
  batch_data->batch.send_message = true;
  batch_data->batch.payload->send_message.send_message.reset(
      retry_state->send_message.get());
}

void RetryFilter::CallData::AddRetriableSendTrailingMetadataOp(
    SubchannelCallRetryState* retry_state,
    SubchannelCallBatchData* batch_data) {
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  retry_state->send_trailing_metadata_storage =
      static_cast<grpc_linked_mdelem*>(arena_->Alloc(
          sizeof(grpc_linked_mdelem) * send_trailing_metadata_.list.count));
  grpc_metadata_batch_copy(&send_trailing_metadata_,
                           &retry_state->send_trailing_metadata,
                           retry_state->send_trailing_metadata_storage);
  retry_state->started_send_trailing_metadata = true;
  batch_data->batch.send_trailing_metadata = true;
  batch_data->batch.payload->send_trailing_metadata.send_trailing_metadata =
      &retry_state->send_trailing_metadata;
}

void RetryFilter::CallData::AddRetriableRecvInitialMetadataOp(
    SubchannelCallRetryState* retry_state,
    SubchannelCallBatchData* batch_data) {
  retry_state->started_recv_initial_metadata = true;
  batch_data->batch.recv_initial_metadata = true;
  grpc_metadata_batch_init(&retry_state->recv_initial_metadata);
  batch_data->batch.payload->recv_initial_metadata.recv_initial_metadata =
      &retry_state->recv_initial_metadata;
  batch_data->batch.payload->recv_initial_metadata.trailing_metadata_available =
      &retry_state->trailing_metadata_available;
  GRPC_CLOSURE_INIT(&retry_state->recv_initial_metadata_ready,
                    RecvInitialMetadataReady, batch_data,
                    grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_initial_metadata.recv_initial_metadata_ready =
      &retry_state->recv_initial_metadata_ready;
}

void RetryFilter::CallData::AddRetriableRecvMessageOp(
    SubchannelCallRetryState* retry_state,
    SubchannelCallBatchData* batch_data) {
  ++retry_state->started_recv_message_count;
  batch_data->batch.recv_message = true;
  batch_data->batch.payload->recv_message.recv_message =
      &retry_state->recv_message;
  GRPC_CLOSURE_INIT(&retry_state->recv_message_ready, RecvMessageReady,
                    batch_data, grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_message.recv_message_ready =
      &retry_state->recv_message_ready;
}

void RetryFilter::CallData::AddRetriableRecvTrailingMetadataOp(
    SubchannelCallRetryState* retry_state,
    SubchannelCallBatchData* batch_data) {
  retry_state->started_recv_trailing_metadata = true;
  batch_data->batch.recv_trailing_metadata = true;
  grpc_metadata_batch_init(&retry_state->recv_trailing_metadata);
  batch_data->batch.payload->recv_trailing_metadata.recv_trailing_metadata =
      &retry_state->recv_trailing_metadata;
  batch_data->batch.payload->recv_trailing_metadata.collect_stats =
      &retry_state->collect_stats;
  GRPC_CLOSURE_INIT(&retry_state->recv_trailing_metadata_ready,
                    RecvTrailingMetadataReady, batch_data,
                    grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_trailing_metadata
      .recv_trailing_metadata_ready =
      &retry_state->recv_trailing_metadata_ready;
}

void RetryFilter::CallData::StartInternalRecvTrailingMetadata() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: call failed but recv_trailing_metadata not "
            "started; starting it internally",
            chand_, this);
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(lb_call_->GetParentData());
  // Create batch_data with 2 refs, since this batch will be unreffed twice:
  // once for the recv_trailing_metadata_ready callback when the subchannel
  // batch returns, and again when we actually get a recv_trailing_metadata
  // op from the surface.
  SubchannelCallBatchData* batch_data =
      SubchannelCallBatchData::Create(this, 2, false /* set_on_complete */);
  AddRetriableRecvTrailingMetadataOp(retry_state, batch_data);
  retry_state->recv_trailing_metadata_internal_batch = batch_data;
  // Note: This will release the call combiner.
  lb_call_->StartTransportStreamOpBatch(&batch_data->batch);
}

// If there are any cached send ops that need to be replayed on the
// current subchannel call, creates and returns a new subchannel batch
// to replay those ops.  Otherwise, returns nullptr.
RetryFilter::CallData::SubchannelCallBatchData*
RetryFilter::CallData::MaybeCreateSubchannelBatchForReplay(
    SubchannelCallRetryState* retry_state) {
  SubchannelCallBatchData* replay_batch_data = nullptr;
  // send_initial_metadata.
  if (seen_send_initial_metadata_ &&
      !retry_state->started_send_initial_metadata &&
      !pending_send_initial_metadata_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_initial_metadata op",
              chand_, this);
    }
    replay_batch_data =
        SubchannelCallBatchData::Create(this, 1, true /* set_on_complete */);
    AddRetriableSendInitialMetadataOp(retry_state, replay_batch_data);
  }
  // send_message.
  // Note that we can only have one send_message op in flight at a time.
  if (retry_state->started_send_message_count < send_messages_.size() &&
      retry_state->started_send_message_count ==
          retry_state->completed_send_message_count &&
      !pending_send_message_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_message op",
              chand_, this);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data =
          SubchannelCallBatchData::Create(this, 1, true /* set_on_complete */);
    }
    AddRetriableSendMessageOp(retry_state, replay_batch_data);
  }
  // send_trailing_metadata.
  // Note that we only add this op if we have no more send_message ops
  // to start, since we can't send down any more send_message ops after
  // send_trailing_metadata.
  if (seen_send_trailing_metadata_ &&
      retry_state->started_send_message_count == send_messages_.size() &&
      !retry_state->started_send_trailing_metadata &&
      !pending_send_trailing_metadata_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_trailing_metadata op",
              chand_, this);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data =
          SubchannelCallBatchData::Create(this, 1, true /* set_on_complete */);
    }
    AddRetriableSendTrailingMetadataOp(retry_state, replay_batch_data);
  }
  return replay_batch_data;
}

void RetryFilter::CallData::AddSubchannelBatchesForPendingBatches(
    SubchannelCallRetryState* retry_state, CallCombinerClosureList* closures) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    PendingBatch* pending = &pending_batches_[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch == nullptr) continue;
    // Skip any batch that either (a) has already been started on this
    // subchannel call or (b) we can't start yet because we're still
    // replaying send ops that need to be completed first.
    // TODO(roth): Note that if any one op in the batch can't be sent
    // yet due to ops that we're replaying, we don't start any of the ops
    // in the batch.  This is probably okay, but it could conceivably
    // lead to increased latency in some cases -- e.g., we could delay
    // starting a recv op due to it being in the same batch with a send
    // op.  If/when we revamp the callback protocol in
    // transport_stream_op_batch, we may be able to fix this.
    if (batch->send_initial_metadata &&
        retry_state->started_send_initial_metadata) {
      continue;
    }
    if (batch->send_message && retry_state->completed_send_message_count <
                                   retry_state->started_send_message_count) {
      continue;
    }
    // Note that we only start send_trailing_metadata if we have no more
    // send_message ops to start, since we can't send down any more
    // send_message ops after send_trailing_metadata.
    if (batch->send_trailing_metadata &&
        (retry_state->started_send_message_count + batch->send_message <
             send_messages_.size() ||
         retry_state->started_send_trailing_metadata)) {
      continue;
    }
    if (batch->recv_initial_metadata &&
        retry_state->started_recv_initial_metadata) {
      continue;
    }
    if (batch->recv_message && retry_state->completed_recv_message_count <
                                   retry_state->started_recv_message_count) {
      continue;
    }
    if (batch->recv_trailing_metadata &&
        retry_state->started_recv_trailing_metadata) {
      // If we previously completed a recv_trailing_metadata op
      // initiated by StartInternalRecvTrailingMetadata(), use the
      // result of that instead of trying to re-start this op.
      if (GPR_UNLIKELY((retry_state->recv_trailing_metadata_internal_batch !=
                        nullptr))) {
        // If the batch completed, then trigger the completion callback
        // directly, so that we return the previously returned results to
        // the application.  Otherwise, just unref the internally
        // started subchannel batch, since we'll propagate the
        // completion when it completes.
        if (retry_state->completed_recv_trailing_metadata) {
          // Batches containing recv_trailing_metadata always succeed.
          closures->Add(
              &retry_state->recv_trailing_metadata_ready, GRPC_ERROR_NONE,
              "re-executing recv_trailing_metadata_ready to propagate "
              "internally triggered result");
        } else {
          retry_state->recv_trailing_metadata_internal_batch->Unref();
        }
        retry_state->recv_trailing_metadata_internal_batch = nullptr;
      }
      continue;
    }
    // If we're not retrying, just send the batch as-is.
    // TODO(roth): This condition doesn't seem exactly right -- maybe need a
    // notion of "draining" once we've committed and are done replaying?
    if (retry_policy_ == nullptr || retry_committed_) {
      AddClosureForSubchannelBatch(batch, closures);
      PendingBatchClear(pending);
      continue;
    }
    // Create batch with the right number of callbacks.
    const bool has_send_ops = batch->send_initial_metadata ||
                              batch->send_message ||
                              batch->send_trailing_metadata;
    const int num_callbacks = has_send_ops + batch->recv_initial_metadata +
                              batch->recv_message +
                              batch->recv_trailing_metadata;
    SubchannelCallBatchData* batch_data = SubchannelCallBatchData::Create(
        this, num_callbacks, has_send_ops /* set_on_complete */);
    // Cache send ops if needed.
    MaybeCacheSendOpsForBatch(pending);
    // send_initial_metadata.
    if (batch->send_initial_metadata) {
      AddRetriableSendInitialMetadataOp(retry_state, batch_data);
    }
    // send_message.
    if (batch->send_message) {
      AddRetriableSendMessageOp(retry_state, batch_data);
    }
    // send_trailing_metadata.
    if (batch->send_trailing_metadata) {
      AddRetriableSendTrailingMetadataOp(retry_state, batch_data);
    }
    // recv_initial_metadata.
    if (batch->recv_initial_metadata) {
      // recv_flags is only used on the server side.
      GPR_ASSERT(batch->payload->recv_initial_metadata.recv_flags == nullptr);
      AddRetriableRecvInitialMetadataOp(retry_state, batch_data);
    }
    // recv_message.
    if (batch->recv_message) {
      AddRetriableRecvMessageOp(retry_state, batch_data);
    }
    // recv_trailing_metadata.
    if (batch->recv_trailing_metadata) {
      AddRetriableRecvTrailingMetadataOp(retry_state, batch_data);
    }
    AddClosureForSubchannelBatch(&batch_data->batch, closures);
    // Track number of pending subchannel send batches.
    // If this is the first one, take a ref to the call stack.
    if (batch->send_initial_metadata || batch->send_message ||
        batch->send_trailing_metadata) {
      if (num_pending_retriable_subchannel_send_batches_ == 0) {
        GRPC_CALL_STACK_REF(owning_call_, "subchannel_send_batches");
      }
      ++num_pending_retriable_subchannel_send_batches_;
    }
  }
}

void RetryFilter::CallData::StartRetriableSubchannelBatches(
    void* arg, grpc_error* /*ignored*/) {
  CallData* call = static_cast<CallData*>(arg);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: constructing retriable batches",
            call->chand_, call);
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(call->lb_call_->GetParentData());
  // Construct list of closures to execute, one for each pending batch.
  CallCombinerClosureList closures;
  // Replay previously-returned send_* ops if needed.
  SubchannelCallBatchData* replay_batch_data =
      call->MaybeCreateSubchannelBatchForReplay(retry_state);
  if (replay_batch_data != nullptr) {
    call->AddClosureForSubchannelBatch(&replay_batch_data->batch, &closures);
    // Track number of pending subchannel send batches.
    // If this is the first one, take a ref to the call stack.
    if (call->num_pending_retriable_subchannel_send_batches_ == 0) {
      GRPC_CALL_STACK_REF(call->owning_call_, "subchannel_send_batches");
    }
    ++call->num_pending_retriable_subchannel_send_batches_;
  }
  // Now add pending batches.
  call->AddSubchannelBatchesForPendingBatches(retry_state, &closures);
  // Start batches on subchannel call.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " retriable batches on lb_call=%p",
            call->chand_, call, closures.size(), call->lb_call_.get());
  }
  // Note: This will yield the call combiner.
  closures.RunClosures(call->call_combiner_);
}

void RetryFilter::CallData::CreateLbCall(void* arg, grpc_error* /*error*/) {
  auto* calld = static_cast<CallData*>(arg);
  const size_t parent_data_size =
      calld->enable_retries_ ? sizeof(SubchannelCallRetryState) : 0;
  grpc_call_element_args args = {
      calld->owning_call_,     nullptr,
      calld->call_context_,    calld->path_,
      calld->call_start_time_, calld->deadline_,
      calld->arena_,           calld->call_combiner_};
  calld->lb_call_ = calld->chand_->client_channel_->CreateLoadBalancedCall(
      args, calld->pollent_, parent_data_size);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: create lb_call=%p", calld->chand_,
            calld, calld->lb_call_.get());
  }
  if (parent_data_size > 0) {
    new (calld->lb_call_->GetParentData())
        SubchannelCallRetryState(calld->call_context_);
  }
  calld->PendingBatchesResume();
}

}  // namespace

const grpc_channel_filter kRetryFilterVtable = {
    RetryFilter::CallData::StartTransportStreamOpBatch,
    RetryFilter::StartTransportOp,
    sizeof(RetryFilter::CallData),
    RetryFilter::CallData::Init,
    RetryFilter::CallData::SetPollent,
    RetryFilter::CallData::Destroy,
    sizeof(RetryFilter),
    RetryFilter::Init,
    RetryFilter::Destroy,
    RetryFilter::GetChannelInfo,
    "retry_filter",
};

}  // namespace grpc_core
