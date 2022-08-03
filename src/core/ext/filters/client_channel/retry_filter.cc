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

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>

#include <memory>
#include <new>
#include <string>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "absl/utility/utility.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/ext/filters/client_channel/retry_service_config.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
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
// batches to be started on an LB call.  When the child batches return, we
// then decide which pending batches have been completed and schedule their
// callbacks accordingly.  If a call attempt fails and we want to retry it,
// we create a new LB call and start again, constructing new "child" batches
// for the new LB call.
//
// Note that retries are committed when receiving data from the server
// (except for Trailers-Only responses).  However, there may be many
// send ops started before receiving any data, so we may have already
// completed some number of send ops (and returned the completions up to
// the surface) by the time we realize that we need to retry.  To deal
// with this, we cache data for send ops, so that we can replay them on a
// different LB call even after we have completed the original batches.
//
// The code is structured as follows:
// - In CallData (in the parent channel), we maintain a list of pending
//   ops and cached data for send ops.
// - There is a CallData::CallAttempt object for each retry attempt.
//   This object contains the LB call for that attempt and state to indicate
//   which ops from the CallData object have already been sent down to that
//   LB call.
// - There is a CallData::CallAttempt::BatchData object for each "child"
//   batch sent on the LB call.
//
// When constructing the "child" batches, we compare the state in the
// CallAttempt object against the state in the CallData object to see
// which batches need to be sent on the LB call for a given attempt.

// TODO(roth): In subsequent PRs:
// - implement hedging

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

  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args) {
    GPR_ASSERT(args->is_last);
    GPR_ASSERT(elem->filter == &kRetryFilterVtable);
    grpc_error_handle error = GRPC_ERROR_NONE;
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

  RetryFilter(const grpc_channel_args* args, grpc_error_handle* error)
      : client_channel_(grpc_channel_args_find_pointer<ClientChannel>(
            args, GRPC_ARG_CLIENT_CHANNEL)),
        per_rpc_retry_buffer_size_(GetMaxPerRpcRetryBufferSize(args)),
        service_config_parser_index_(
            internal::RetryServiceConfigParser::ParserIndex()) {
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
    retry_throttle_data_ =
        internal::ServerRetryThrottleMap::Get()->GetDataForServer(
            server_name, config->max_milli_tokens(),
            config->milli_token_ratio());
  }

  const RetryMethodConfig* GetRetryPolicy(
      const grpc_call_context_element* context);

  ClientChannel* client_channel_;
  size_t per_rpc_retry_buffer_size_;
  RefCountedPtr<ServerRetryThrottleData> retry_throttle_data_;
  const size_t service_config_parser_index_;
};

//
// RetryFilter::CallData
//

class RetryFilter::CallData {
 public:
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* /*final_info*/,
                      grpc_closure* then_schedule_closure);
  static void StartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);
  static void SetPollent(grpc_call_element* elem, grpc_polling_entity* pollent);

 private:
  class CallStackDestructionBarrier;

  // Pending batches stored in call data.
  struct PendingBatch {
    // The pending batch.  If nullptr, this slot is empty.
    grpc_transport_stream_op_batch* batch = nullptr;
    // Indicates whether payload for send ops has been cached in CallData.
    bool send_ops_cached = false;
  };

  // State associated with each call attempt.
  class CallAttempt : public RefCounted<CallAttempt> {
   public:
    CallAttempt(CallData* calld, bool is_transparent_retry);
    ~CallAttempt() override;

    bool lb_call_committed() const { return lb_call_committed_; }

    // Constructs and starts whatever batches are needed on this call
    // attempt.
    void StartRetriableBatches();

    // Frees cached send ops that have already been completed after
    // committing the call.
    void FreeCachedSendOpDataAfterCommit();

    // Cancels the call attempt.
    void CancelFromSurface(grpc_transport_stream_op_batch* cancel_batch);

   private:
    // State used for starting a retryable batch on the call attempt's LB call.
    // This provides its own grpc_transport_stream_op_batch and other data
    // structures needed to populate the ops in the batch.
    // We allocate one struct on the arena for each attempt at starting a
    // batch on a given LB call.
    class BatchData
        : public RefCounted<BatchData, PolymorphicRefCount, kUnrefCallDtor> {
     public:
      BatchData(RefCountedPtr<CallAttempt> call_attempt, int refcount,
                bool set_on_complete);
      ~BatchData() override;

      grpc_transport_stream_op_batch* batch() { return &batch_; }

      // Adds retriable send_initial_metadata op.
      void AddRetriableSendInitialMetadataOp();
      // Adds retriable send_message op.
      void AddRetriableSendMessageOp();
      // Adds retriable send_trailing_metadata op.
      void AddRetriableSendTrailingMetadataOp();
      // Adds retriable recv_initial_metadata op.
      void AddRetriableRecvInitialMetadataOp();
      // Adds retriable recv_message op.
      void AddRetriableRecvMessageOp();
      // Adds retriable recv_trailing_metadata op.
      void AddRetriableRecvTrailingMetadataOp();
      // Adds cancel_stream op.
      void AddCancelStreamOp(grpc_error_handle error);

     private:
      // Frees cached send ops that were completed by the completed batch in
      // batch_data.  Used when batches are completed after the call is
      // committed.
      void FreeCachedSendOpDataForCompletedBatch();

      // If there is a pending recv_initial_metadata op, adds a closure
      // to closures for recv_initial_metadata_ready.
      void MaybeAddClosureForRecvInitialMetadataCallback(
          grpc_error_handle error, CallCombinerClosureList* closures);
      // Intercepts recv_initial_metadata_ready callback for retries.
      // Commits the call and returns the initial metadata up the stack.
      static void RecvInitialMetadataReady(void* arg, grpc_error_handle error);

      // If there is a pending recv_message op, adds a closure to closures
      // for recv_message_ready.
      void MaybeAddClosureForRecvMessageCallback(
          grpc_error_handle error, CallCombinerClosureList* closures);
      // Intercepts recv_message_ready callback for retries.
      // Commits the call and returns the message up the stack.
      static void RecvMessageReady(void* arg, grpc_error_handle error);

      // If there is a pending recv_trailing_metadata op, adds a closure to
      // closures for recv_trailing_metadata_ready.
      void MaybeAddClosureForRecvTrailingMetadataReady(
          grpc_error_handle error, CallCombinerClosureList* closures);
      // Adds any necessary closures for deferred batch completion
      // callbacks to closures.
      void AddClosuresForDeferredCompletionCallbacks(
          CallCombinerClosureList* closures);
      // For any pending batch containing an op that has not yet been started,
      // adds the pending batch's completion closures to closures.
      void AddClosuresToFailUnstartedPendingBatches(
          grpc_error_handle error, CallCombinerClosureList* closures);
      // Runs necessary closures upon completion of a call attempt.
      void RunClosuresForCompletedCall(grpc_error_handle error);
      // Intercepts recv_trailing_metadata_ready callback for retries.
      // Commits the call and returns the trailing metadata up the stack.
      static void RecvTrailingMetadataReady(void* arg, grpc_error_handle error);

      // Adds the on_complete closure for the pending batch completed in
      // batch_data to closures.
      void AddClosuresForCompletedPendingBatch(
          grpc_error_handle error, CallCombinerClosureList* closures);

      // If there are any cached ops to replay or pending ops to start on the
      // LB call, adds them to closures.
      void AddClosuresForReplayOrPendingSendOps(
          CallCombinerClosureList* closures);

      // Callback used to intercept on_complete from LB calls.
      static void OnComplete(void* arg, grpc_error_handle error);

      // Callback used to handle on_complete for internally generated
      // cancel_stream op.
      static void OnCompleteForCancelOp(void* arg, grpc_error_handle error);

      RefCountedPtr<CallAttempt> call_attempt_;
      // The batch to use in the LB call.
      // Its payload field points to CallAttempt::batch_payload_.
      grpc_transport_stream_op_batch batch_;
      // For intercepting on_complete.
      grpc_closure on_complete_;
    };

    class AttemptDispatchController
        : public ConfigSelector::CallDispatchController {
     public:
      explicit AttemptDispatchController(CallAttempt* call_attempt)
          : call_attempt_(call_attempt) {}

      // Will never be called.
      bool ShouldRetry() override { return false; }

      void Commit() override {
        call_attempt_->lb_call_committed_ = true;
        auto* calld = call_attempt_->calld_;
        if (calld->retry_committed_) {
          auto* service_config_call_data =
              static_cast<ClientChannelServiceConfigCallData*>(
                  calld->call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA]
                      .value);
          service_config_call_data->call_dispatch_controller()->Commit();
        }
      }

     private:
      CallAttempt* call_attempt_;
    };

    // Creates a BatchData object on the call's arena with the
    // specified refcount.  If set_on_complete is true, the batch's
    // on_complete callback will be set to point to on_complete();
    // otherwise, the batch's on_complete callback will be null.
    BatchData* CreateBatch(int refcount, bool set_on_complete) {
      return calld_->arena_->New<BatchData>(Ref(DEBUG_LOCATION, "CreateBatch"),
                                            refcount, set_on_complete);
    }

    // If there are any cached send ops that need to be replayed on this
    // call attempt, creates and returns a new batch to replay those ops.
    // Otherwise, returns nullptr.
    BatchData* MaybeCreateBatchForReplay();

    // Adds a closure to closures that will execute batch in the call combiner.
    void AddClosureForBatch(grpc_transport_stream_op_batch* batch,
                            const char* reason,
                            CallCombinerClosureList* closures);

    // Helper function used to start a recv_trailing_metadata batch.  This
    // is used in the case where a recv_initial_metadata or recv_message
    // op fails in a way that we know the call is over but when the application
    // has not yet started its own recv_trailing_metadata op.
    void AddBatchForInternalRecvTrailingMetadata(
        CallCombinerClosureList* closures);

    // Adds a batch to closures to cancel this call attempt, if
    // cancellation has not already been sent on the LB call.
    void MaybeAddBatchForCancelOp(grpc_error_handle error,
                                  CallCombinerClosureList* closures);

    // Adds batches for pending batches to closures.
    void AddBatchesForPendingBatches(CallCombinerClosureList* closures);

    // Adds whatever batches are needed on this attempt to closures.
    void AddRetriableBatches(CallCombinerClosureList* closures);

    // Returns true if any send op in the batch was not yet started on this
    // attempt.
    bool PendingBatchContainsUnstartedSendOps(PendingBatch* pending);

    // Returns true if there are cached send ops to replay.
    bool HaveSendOpsToReplay();

    // If our retry state is no longer needed, switch to fast path by moving
    // our LB call into calld_->committed_call_ and having calld_ drop
    // its ref to us.
    void MaybeSwitchToFastPath();

    // Returns true if the call should be retried.
    bool ShouldRetry(absl::optional<grpc_status_code> status,
                     absl::optional<Duration> server_pushback_ms);

    // Abandons the call attempt.  Unrefs any deferred batches.
    void Abandon();

    static void OnPerAttemptRecvTimer(void* arg, grpc_error_handle error);
    static void OnPerAttemptRecvTimerLocked(void* arg, grpc_error_handle error);
    void MaybeCancelPerAttemptRecvTimer();

    CallData* calld_;
    AttemptDispatchController attempt_dispatch_controller_;
    OrphanablePtr<ClientChannel::LoadBalancedCall> lb_call_;
    bool lb_call_committed_ = false;

    grpc_timer per_attempt_recv_timer_;
    grpc_closure on_per_attempt_recv_timer_;
    bool per_attempt_recv_timer_pending_ = false;

    // BatchData.batch.payload points to this.
    grpc_transport_stream_op_batch_payload batch_payload_;
    // For send_initial_metadata.
    grpc_metadata_batch send_initial_metadata_{calld_->arena_};
    // For send_trailing_metadata.
    grpc_metadata_batch send_trailing_metadata_{calld_->arena_};
    // For intercepting recv_initial_metadata.
    grpc_metadata_batch recv_initial_metadata_{calld_->arena_};
    grpc_closure recv_initial_metadata_ready_;
    bool trailing_metadata_available_ = false;
    // For intercepting recv_message.
    grpc_closure recv_message_ready_;
    absl::optional<SliceBuffer> recv_message_;
    uint32_t recv_message_flags_;
    // For intercepting recv_trailing_metadata.
    grpc_metadata_batch recv_trailing_metadata_{calld_->arena_};
    grpc_transport_stream_stats collect_stats_;
    grpc_closure recv_trailing_metadata_ready_;
    // These fields indicate which ops have been started and completed on
    // this call attempt.
    size_t started_send_message_count_ = 0;
    size_t completed_send_message_count_ = 0;
    size_t started_recv_message_count_ = 0;
    size_t completed_recv_message_count_ = 0;
    bool started_send_initial_metadata_ : 1;
    bool completed_send_initial_metadata_ : 1;
    bool started_send_trailing_metadata_ : 1;
    bool completed_send_trailing_metadata_ : 1;
    bool started_recv_initial_metadata_ : 1;
    bool completed_recv_initial_metadata_ : 1;
    bool started_recv_trailing_metadata_ : 1;
    bool completed_recv_trailing_metadata_ : 1;
    bool sent_cancel_stream_ : 1;
    // State for callback processing.
    RefCountedPtr<BatchData> recv_initial_metadata_ready_deferred_batch_;
    grpc_error_handle recv_initial_metadata_error_ = GRPC_ERROR_NONE;
    RefCountedPtr<BatchData> recv_message_ready_deferred_batch_;
    grpc_error_handle recv_message_error_ = GRPC_ERROR_NONE;
    struct OnCompleteDeferredBatch {
      OnCompleteDeferredBatch(RefCountedPtr<BatchData> batch,
                              grpc_error_handle error)
          : batch(std::move(batch)), error(error) {}
      RefCountedPtr<BatchData> batch;
      grpc_error_handle error;
    };
    // There cannot be more than 3 pending send op batches at a time.
    absl::InlinedVector<OnCompleteDeferredBatch, 3>
        on_complete_deferred_batches_;
    RefCountedPtr<BatchData> recv_trailing_metadata_internal_batch_;
    grpc_error_handle recv_trailing_metadata_error_ = GRPC_ERROR_NONE;
    bool seen_recv_trailing_metadata_from_surface_ : 1;
    // NOTE: Do not move this next to the metadata bitfields above. That would
    //       save space but will also result in a data race because compiler
    //       will generate a 2 byte store which overwrites the meta-data
    //       fields upon setting this field.
    bool abandoned_ : 1;
  };

  CallData(RetryFilter* chand, const grpc_call_element_args& args);
  ~CallData();

  void StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch);

  // Returns the index into pending_batches_ to be used for batch.
  static size_t GetBatchIndex(grpc_transport_stream_op_batch* batch);
  PendingBatch* PendingBatchesAdd(grpc_transport_stream_op_batch* batch);
  void PendingBatchClear(PendingBatch* pending);
  void MaybeClearPendingBatch(PendingBatch* pending);
  static void FailPendingBatchInCallCombiner(void* arg,
                                             grpc_error_handle error);
  // Fails all pending batches.  Does NOT yield call combiner.
  void PendingBatchesFail(grpc_error_handle error);
  // Returns a pointer to the first pending batch for which predicate(batch)
  // returns true, or null if not found.
  template <typename Predicate>
  PendingBatch* PendingBatchFind(const char* log_message, Predicate predicate);

  // Caches data for send ops so that it can be retried later, if not
  // already cached.
  void MaybeCacheSendOpsForBatch(PendingBatch* pending);
  void FreeCachedSendInitialMetadata();
  // Frees cached send_message at index idx.
  void FreeCachedSendMessage(size_t idx);
  void FreeCachedSendTrailingMetadata();
  void FreeAllCachedSendOpData();

  // Commits the call so that no further retry attempts will be performed.
  void RetryCommit(CallAttempt* call_attempt);

  // Starts a timer to retry after appropriate back-off.
  // If server_pushback is nullopt, retry_backoff_ is used.
  void StartRetryTimer(absl::optional<Duration> server_pushback);

  static void OnRetryTimer(void* arg, grpc_error_handle error);
  static void OnRetryTimerLocked(void* arg, grpc_error_handle error);

  // Adds a closure to closures to start a transparent retry.
  void AddClosureToStartTransparentRetry(CallCombinerClosureList* closures);
  static void StartTransparentRetry(void* arg, grpc_error_handle error);

  OrphanablePtr<ClientChannel::LoadBalancedCall> CreateLoadBalancedCall(
      ConfigSelector::CallDispatchController* call_dispatch_controller,
      bool is_transparent_retry);

  void CreateCallAttempt(bool is_transparent_retry);

  RetryFilter* chand_;
  grpc_polling_entity* pollent_;
  RefCountedPtr<ServerRetryThrottleData> retry_throttle_data_;
  const RetryMethodConfig* retry_policy_ = nullptr;
  BackOff retry_backoff_;

  grpc_slice path_;  // Request path.
  Timestamp deadline_;
  Arena* arena_;
  grpc_call_stack* owning_call_;
  CallCombiner* call_combiner_;
  grpc_call_context_element* call_context_;

  grpc_error_handle cancelled_from_surface_ = GRPC_ERROR_NONE;

  RefCountedPtr<CallStackDestructionBarrier> call_stack_destruction_barrier_;

  // TODO(roth): As part of implementing hedging, we will need to maintain a
  // list of all pending attempts, so that we can cancel them all if the call
  // gets cancelled.
  RefCountedPtr<CallAttempt> call_attempt_;

  // LB call used when we've committed to a call attempt and the retry
  // state for that attempt is no longer needed.  This provides a fast
  // path for long-running streaming calls that minimizes overhead.
  OrphanablePtr<ClientChannel::LoadBalancedCall> committed_call_;

  // When are are not yet fully committed to a particular call (i.e.,
  // either we might still retry or we have committed to the call but
  // there are still some cached ops to be replayed on the call),
  // batches received from above will be added to this list, and they
  // will not be removed until we have invoked their completion callbacks.
  size_t bytes_buffered_for_retry_ = 0;
  PendingBatch pending_batches_[MAX_PENDING_BATCHES];
  bool pending_send_initial_metadata_ : 1;
  bool pending_send_message_ : 1;
  bool pending_send_trailing_metadata_ : 1;

  // Retry state.
  bool retry_committed_ : 1;
  bool retry_timer_pending_ : 1;
  bool retry_codepath_started_ : 1;
  bool sent_transparent_retry_not_seen_by_server_ : 1;
  int num_attempts_completed_ = 0;
  grpc_timer retry_timer_;
  grpc_closure retry_closure_;

  // Cached data for retrying send ops.
  // send_initial_metadata
  bool seen_send_initial_metadata_ = false;
  grpc_metadata_batch send_initial_metadata_{arena_};
  // TODO(roth): As part of implementing hedging, we'll probably need to
  // have the LB call set a value in CallAttempt and then propagate it
  // from CallAttempt to the parent call when we commit.  Otherwise, we
  // may leave this with a value for a peer other than the one we
  // actually commit to.  Alternatively, maybe see if there's a way to
  // change the surface API such that the peer isn't available until
  // after initial metadata is received?  (Could even change the
  // transport API to return this with the recv_initial_metadata op.)
  gpr_atm* peer_string_;
  // send_message
  // When we get a send_message op, we replace the original byte stream
  // with a CachingByteStream that caches the slices to a local buffer for
  // use in retries.
  // Note: We inline the cache for the first 3 send_message ops and use
  // dynamic allocation after that.  This number was essentially picked
  // at random; it could be changed in the future to tune performance.
  struct CachedSendMessage {
    SliceBuffer* slices;
    uint32_t flags;
  };
  absl::InlinedVector<CachedSendMessage, 3> send_messages_;
  // send_trailing_metadata
  bool seen_send_trailing_metadata_ = false;
  grpc_metadata_batch send_trailing_metadata_{arena_};
};

//
// RetryFilter::CallData::CallStackDestructionBarrier
//

// A class to track the existence of LoadBalancedCall call stacks that
// we've created.  We wait until all such call stacks have been
// destroyed before we return the on_call_stack_destruction closure up
// to the surface.
//
// The parent RetryFilter::CallData object holds a ref to this object.
// When it is destroyed, it will store the on_call_stack_destruction
// closure from the surface in this object and then release its ref.
// We also take a ref to this object for each LB call we create, and
// those refs are not released until the LB call stack is destroyed.
// When this object is destroyed, it will invoke the
// on_call_stack_destruction closure from the surface.
class RetryFilter::CallData::CallStackDestructionBarrier
    : public RefCounted<CallStackDestructionBarrier, PolymorphicRefCount,
                        kUnrefCallDtor> {
 public:
  CallStackDestructionBarrier() {}

  ~CallStackDestructionBarrier() override {
    // TODO(yashkt) : This can potentially be a Closure::Run
    ExecCtx::Run(DEBUG_LOCATION, on_call_stack_destruction_, GRPC_ERROR_NONE);
  }

  // Set the closure from the surface.  This closure will be invoked
  // when this object is destroyed.
  void set_on_call_stack_destruction(grpc_closure* on_call_stack_destruction) {
    on_call_stack_destruction_ = on_call_stack_destruction;
  }

  // Invoked to get an on_call_stack_destruction closure for a new LB call.
  grpc_closure* MakeLbCallDestructionClosure(CallData* calld) {
    Ref().release();  // Ref held by callback.
    grpc_closure* on_lb_call_destruction_complete =
        calld->arena_->New<grpc_closure>();
    GRPC_CLOSURE_INIT(on_lb_call_destruction_complete,
                      OnLbCallDestructionComplete, this, nullptr);
    return on_lb_call_destruction_complete;
  }

 private:
  static void OnLbCallDestructionComplete(void* arg,
                                          grpc_error_handle /*error*/) {
    auto* self = static_cast<CallStackDestructionBarrier*>(arg);
    self->Unref();
  }

  grpc_closure* on_call_stack_destruction_ = nullptr;
};

//
// RetryFilter::CallData::CallAttempt
//

RetryFilter::CallData::CallAttempt::CallAttempt(CallData* calld,
                                                bool is_transparent_retry)
    : RefCounted(GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace) ? "CallAttempt"
                                                           : nullptr),
      calld_(calld),
      attempt_dispatch_controller_(this),
      batch_payload_(calld->call_context_),
      started_send_initial_metadata_(false),
      completed_send_initial_metadata_(false),
      started_send_trailing_metadata_(false),
      completed_send_trailing_metadata_(false),
      started_recv_initial_metadata_(false),
      completed_recv_initial_metadata_(false),
      started_recv_trailing_metadata_(false),
      completed_recv_trailing_metadata_(false),
      sent_cancel_stream_(false),
      seen_recv_trailing_metadata_from_surface_(false),
      abandoned_(false) {
  lb_call_ = calld->CreateLoadBalancedCall(&attempt_dispatch_controller_,
                                           is_transparent_retry);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p: created attempt, lb_call=%p",
            calld->chand_, calld, this, lb_call_.get());
  }
  // If per_attempt_recv_timeout is set, start a timer.
  if (calld->retry_policy_ != nullptr &&
      calld->retry_policy_->per_attempt_recv_timeout().has_value()) {
    Timestamp per_attempt_recv_deadline =
        ExecCtx::Get()->Now() +
        *calld->retry_policy_->per_attempt_recv_timeout();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p attempt=%p: per-attempt timeout in %" PRId64
              " ms",
              calld->chand_, calld, this,
              calld->retry_policy_->per_attempt_recv_timeout()->millis());
    }
    // Schedule retry after computed delay.
    GRPC_CLOSURE_INIT(&on_per_attempt_recv_timer_, OnPerAttemptRecvTimer, this,
                      nullptr);
    GRPC_CALL_STACK_REF(calld->owning_call_, "OnPerAttemptRecvTimer");
    Ref(DEBUG_LOCATION, "OnPerAttemptRecvTimer").release();
    per_attempt_recv_timer_pending_ = true;
    grpc_timer_init(&per_attempt_recv_timer_, per_attempt_recv_deadline,
                    &on_per_attempt_recv_timer_);
  }
}

RetryFilter::CallData::CallAttempt::~CallAttempt() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p attempt=%p: destroying call attempt",
            calld_->chand_, calld_, this);
  }
}

void RetryFilter::CallData::CallAttempt::FreeCachedSendOpDataAfterCommit() {
  // TODO(roth): When we implement hedging, this logic will need to get
  // a bit more complex, because there may be other (now abandoned) call
  // attempts still using this data.  We may need to do some sort of
  // ref-counting instead.
  if (completed_send_initial_metadata_) {
    calld_->FreeCachedSendInitialMetadata();
  }
  for (size_t i = 0; i < completed_send_message_count_; ++i) {
    calld_->FreeCachedSendMessage(i);
  }
  if (completed_send_trailing_metadata_) {
    calld_->FreeCachedSendTrailingMetadata();
  }
}

bool RetryFilter::CallData::CallAttempt::PendingBatchContainsUnstartedSendOps(
    PendingBatch* pending) {
  if (pending->batch->on_complete == nullptr) return false;
  if (pending->batch->send_initial_metadata &&
      !started_send_initial_metadata_) {
    return true;
  }
  if (pending->batch->send_message &&
      started_send_message_count_ < calld_->send_messages_.size()) {
    return true;
  }
  if (pending->batch->send_trailing_metadata &&
      !started_send_trailing_metadata_) {
    return true;
  }
  return false;
}

bool RetryFilter::CallData::CallAttempt::HaveSendOpsToReplay() {
  // We don't check send_initial_metadata here, because that op will always
  // be started as soon as it is received from the surface, so it will
  // never need to be started at this point.
  return started_send_message_count_ < calld_->send_messages_.size() ||
         (calld_->seen_send_trailing_metadata_ &&
          !started_send_trailing_metadata_);
}

void RetryFilter::CallData::CallAttempt::MaybeSwitchToFastPath() {
  // If we're not yet committed, we can't switch yet.
  // TODO(roth): As part of implementing hedging, this logic needs to
  // check that *this* call attempt is the one that we've committed to.
  // Might need to replace abandoned_ with an enum indicating whether we're
  // in flight, abandoned, or the winning call attempt.
  if (!calld_->retry_committed_) return;
  // If we've already switched to fast path, there's nothing to do here.
  if (calld_->committed_call_ != nullptr) return;
  // If the perAttemptRecvTimeout timer is pending, we can't switch yet.
  if (per_attempt_recv_timer_pending_) return;
  // If there are still send ops to replay, we can't switch yet.
  if (HaveSendOpsToReplay()) return;
  // If we started an internal batch for recv_trailing_metadata but have not
  // yet seen that op from the surface, we can't switch yet.
  if (recv_trailing_metadata_internal_batch_ != nullptr) return;
  // Switch to fast path.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p: retry state no longer needed; "
            "moving LB call to parent and unreffing the call attempt",
            calld_->chand_, calld_, this);
  }
  calld_->committed_call_ = std::move(lb_call_);
  calld_->call_attempt_.reset(DEBUG_LOCATION, "MaybeSwitchToFastPath");
}

// If there are any cached send ops that need to be replayed on the
// current call attempt, creates and returns a new batch to replay those ops.
// Otherwise, returns nullptr.
RetryFilter::CallData::CallAttempt::BatchData*
RetryFilter::CallData::CallAttempt::MaybeCreateBatchForReplay() {
  BatchData* replay_batch_data = nullptr;
  // send_initial_metadata.
  if (calld_->seen_send_initial_metadata_ && !started_send_initial_metadata_ &&
      !calld_->pending_send_initial_metadata_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p attempt=%p: replaying previously completed "
              "send_initial_metadata op",
              calld_->chand_, calld_, this);
    }
    replay_batch_data = CreateBatch(1, true /* set_on_complete */);
    replay_batch_data->AddRetriableSendInitialMetadataOp();
  }
  // send_message.
  // Note that we can only have one send_message op in flight at a time.
  if (started_send_message_count_ < calld_->send_messages_.size() &&
      started_send_message_count_ == completed_send_message_count_ &&
      !calld_->pending_send_message_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p attempt=%p: replaying previously completed "
              "send_message op",
              calld_->chand_, calld_, this);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data = CreateBatch(1, true /* set_on_complete */);
    }
    replay_batch_data->AddRetriableSendMessageOp();
  }
  // send_trailing_metadata.
  // Note that we only add this op if we have no more send_message ops
  // to start, since we can't send down any more send_message ops after
  // send_trailing_metadata.
  if (calld_->seen_send_trailing_metadata_ &&
      started_send_message_count_ == calld_->send_messages_.size() &&
      !started_send_trailing_metadata_ &&
      !calld_->pending_send_trailing_metadata_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p attempt=%p: replaying previously completed "
              "send_trailing_metadata op",
              calld_->chand_, calld_, this);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data = CreateBatch(1, true /* set_on_complete */);
    }
    replay_batch_data->AddRetriableSendTrailingMetadataOp();
  }
  return replay_batch_data;
}

namespace {

void StartBatchInCallCombiner(void* arg, grpc_error_handle /*ignored*/) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  auto* lb_call = static_cast<ClientChannel::LoadBalancedCall*>(
      batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  lb_call->StartTransportStreamOpBatch(batch);
}

}  // namespace

void RetryFilter::CallData::CallAttempt::AddClosureForBatch(
    grpc_transport_stream_op_batch* batch, const char* reason,
    CallCombinerClosureList* closures) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p attempt=%p: adding batch (%s): %s",
            calld_->chand_, calld_, this, reason,
            grpc_transport_stream_op_batch_string(batch).c_str());
  }
  batch->handler_private.extra_arg = lb_call_.get();
  GRPC_CLOSURE_INIT(&batch->handler_private.closure, StartBatchInCallCombiner,
                    batch, grpc_schedule_on_exec_ctx);
  closures->Add(&batch->handler_private.closure, GRPC_ERROR_NONE, reason);
}

void RetryFilter::CallData::CallAttempt::
    AddBatchForInternalRecvTrailingMetadata(CallCombinerClosureList* closures) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p: call failed but "
            "recv_trailing_metadata not started; starting it internally",
            calld_->chand_, calld_, this);
  }
  // Create batch_data with 2 refs, since this batch will be unreffed twice:
  // once for the recv_trailing_metadata_ready callback when the batch
  // completes, and again when we actually get a recv_trailing_metadata
  // op from the surface.
  BatchData* batch_data = CreateBatch(2, false /* set_on_complete */);
  batch_data->AddRetriableRecvTrailingMetadataOp();
  recv_trailing_metadata_internal_batch_.reset(batch_data);
  AddClosureForBatch(batch_data->batch(),
                     "starting internal recv_trailing_metadata", closures);
}

void RetryFilter::CallData::CallAttempt::MaybeAddBatchForCancelOp(
    grpc_error_handle error, CallCombinerClosureList* closures) {
  if (sent_cancel_stream_) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  sent_cancel_stream_ = true;
  BatchData* cancel_batch_data = CreateBatch(1, /*set_on_complete=*/true);
  cancel_batch_data->AddCancelStreamOp(error);
  AddClosureForBatch(cancel_batch_data->batch(),
                     "start cancellation batch on call attempt", closures);
}

void RetryFilter::CallData::CallAttempt::AddBatchesForPendingBatches(
    CallCombinerClosureList* closures) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld_->pending_batches_); ++i) {
    PendingBatch* pending = &calld_->pending_batches_[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch == nullptr) continue;
    bool has_send_ops = false;
    // Skip any batch that either (a) has already been started on this
    // call attempt or (b) we can't start yet because we're still
    // replaying send ops that need to be completed first.
    // TODO(roth): Note that if any one op in the batch can't be sent
    // yet due to ops that we're replaying, we don't start any of the ops
    // in the batch.  This is probably okay, but it could conceivably
    // lead to increased latency in some cases -- e.g., we could delay
    // starting a recv op due to it being in the same batch with a send
    // op.  If/when we revamp the callback protocol in
    // transport_stream_op_batch, we may be able to fix this.
    if (batch->send_initial_metadata) {
      if (started_send_initial_metadata_) continue;
      has_send_ops = true;
    }
    if (batch->send_message) {
      // Cases where we can't start this send_message op:
      // - We are currently replaying a previous cached send_message op.
      // - We have already replayed all send_message ops, including this
      //   one.  (This can happen if a send_message op is in the same
      //   batch as a recv op, the send_message op has already completed
      //   but the recv op hasn't, and then a subsequent batch with another
      //   recv op is started from the surface.)
      if (completed_send_message_count_ < started_send_message_count_ ||
          completed_send_message_count_ ==
              (calld_->send_messages_.size() + !pending->send_ops_cached)) {
        continue;
      }
      has_send_ops = true;
    }
    // Note that we only start send_trailing_metadata if we have no more
    // send_message ops to start, since we can't send down any more
    // send_message ops after send_trailing_metadata.
    if (batch->send_trailing_metadata) {
      if (started_send_message_count_ + batch->send_message <
              calld_->send_messages_.size() ||
          started_send_trailing_metadata_) {
        continue;
      }
      has_send_ops = true;
    }
    int num_callbacks = has_send_ops;  // All send ops share one callback.
    if (batch->recv_initial_metadata) {
      if (started_recv_initial_metadata_) continue;
      ++num_callbacks;
    }
    if (batch->recv_message) {
      // Skip if the op is already in flight, or if it has already completed
      // but the completion has not yet been sent to the surface.
      if (completed_recv_message_count_ < started_recv_message_count_ ||
          recv_message_ready_deferred_batch_ != nullptr) {
        continue;
      }
      ++num_callbacks;
    }
    if (batch->recv_trailing_metadata) {
      if (started_recv_trailing_metadata_) {
        seen_recv_trailing_metadata_from_surface_ = true;
        // If we previously completed a recv_trailing_metadata op
        // initiated by AddBatchForInternalRecvTrailingMetadata(), use the
        // result of that instead of trying to re-start this op.
        if (GPR_UNLIKELY(recv_trailing_metadata_internal_batch_ != nullptr)) {
          // If the batch completed, then trigger the completion callback
          // directly, so that we return the previously returned results to
          // the application.  Otherwise, just unref the internally started
          // batch, since we'll propagate the completion when it completes.
          if (completed_recv_trailing_metadata_) {
            closures->Add(
                &recv_trailing_metadata_ready_, recv_trailing_metadata_error_,
                "re-executing recv_trailing_metadata_ready to propagate "
                "internally triggered result");
            // Ref will be released by callback.
            recv_trailing_metadata_internal_batch_.release();
          } else {
            recv_trailing_metadata_internal_batch_.reset(
                DEBUG_LOCATION,
                "internally started recv_trailing_metadata batch pending and "
                "recv_trailing_metadata started from surface");
            GRPC_ERROR_UNREF(recv_trailing_metadata_error_);
          }
          recv_trailing_metadata_error_ = GRPC_ERROR_NONE;
        }
        // We don't want the fact that we've already started this op internally
        // to prevent us from adding a batch that may contain other ops.
        // Instead, we'll just skip adding this op below.
        if (num_callbacks == 0) continue;
      } else {
        ++num_callbacks;
      }
    }
    // If we're already committed and the following conditions are met,
    // just send the batch down as-is:
    // - The batch contains no cached send ops.  (If it does, we need
    //   the logic below to use the cached payloads.)
    // - The batch does not contain recv_trailing_metadata when we have
    //   already started an internal recv_trailing_metadata batch.  (If
    //   we've already started an internal recv_trailing_metadata batch,
    //   then we need the logic below to send all ops in the batch
    //   *except* the recv_trailing_metadata op.)
    if (calld_->retry_committed_ && !pending->send_ops_cached &&
        (!batch->recv_trailing_metadata || !started_recv_trailing_metadata_)) {
      AddClosureForBatch(
          batch,
          "start non-replayable pending batch on call attempt after commit",
          closures);
      calld_->PendingBatchClear(pending);
      continue;
    }
    // Create batch with the right number of callbacks.
    BatchData* batch_data =
        CreateBatch(num_callbacks, has_send_ops /* set_on_complete */);
    // Cache send ops if needed.
    calld_->MaybeCacheSendOpsForBatch(pending);
    // send_initial_metadata.
    if (batch->send_initial_metadata) {
      batch_data->AddRetriableSendInitialMetadataOp();
    }
    // send_message.
    if (batch->send_message) {
      batch_data->AddRetriableSendMessageOp();
    }
    // send_trailing_metadata.
    if (batch->send_trailing_metadata) {
      batch_data->AddRetriableSendTrailingMetadataOp();
    }
    // recv_initial_metadata.
    if (batch->recv_initial_metadata) {
      batch_data->AddRetriableRecvInitialMetadataOp();
    }
    // recv_message.
    if (batch->recv_message) {
      batch_data->AddRetriableRecvMessageOp();
    }
    // recv_trailing_metadata.
    if (batch->recv_trailing_metadata && !started_recv_trailing_metadata_) {
      batch_data->AddRetriableRecvTrailingMetadataOp();
    }
    AddClosureForBatch(batch_data->batch(),
                       "start replayable pending batch on call attempt",
                       closures);
  }
}

void RetryFilter::CallData::CallAttempt::AddRetriableBatches(
    CallCombinerClosureList* closures) {
  // Replay previously-returned send_* ops if needed.
  BatchData* replay_batch_data = MaybeCreateBatchForReplay();
  if (replay_batch_data != nullptr) {
    AddClosureForBatch(replay_batch_data->batch(),
                       "start replay batch on call attempt", closures);
  }
  // Now add pending batches.
  AddBatchesForPendingBatches(closures);
}

void RetryFilter::CallData::CallAttempt::StartRetriableBatches() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p: constructing retriable batches",
            calld_->chand_, calld_, this);
  }
  // Construct list of closures to execute, one for each pending batch.
  CallCombinerClosureList closures;
  AddRetriableBatches(&closures);
  // Note: This will yield the call combiner.
  // Start batches on LB call.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p: starting %" PRIuPTR
            " retriable batches on lb_call=%p",
            calld_->chand_, calld_, this, closures.size(), lb_call_.get());
  }
  closures.RunClosures(calld_->call_combiner_);
}

void RetryFilter::CallData::CallAttempt::CancelFromSurface(
    grpc_transport_stream_op_batch* cancel_batch) {
  MaybeCancelPerAttemptRecvTimer();
  Abandon();
  // Propagate cancellation to LB call.
  lb_call_->StartTransportStreamOpBatch(cancel_batch);
}

bool RetryFilter::CallData::CallAttempt::ShouldRetry(
    absl::optional<grpc_status_code> status,
    absl::optional<Duration> server_pushback) {
  // If no retry policy, don't retry.
  if (calld_->retry_policy_ == nullptr) return false;
  // Check status.
  if (status.has_value()) {
    if (GPR_LIKELY(*status == GRPC_STATUS_OK)) {
      if (calld_->retry_throttle_data_ != nullptr) {
        calld_->retry_throttle_data_->RecordSuccess();
      }
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p attempt=%p: call succeeded",
                calld_->chand_, calld_, this);
      }
      return false;
    }
    // Status is not OK.  Check whether the status is retryable.
    if (!calld_->retry_policy_->retryable_status_codes().Contains(*status)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p attempt=%p: status %s not configured as "
                "retryable",
                calld_->chand_, calld_, this,
                grpc_status_code_to_string(*status));
      }
      return false;
    }
  }
  // Record the failure and check whether retries are throttled.
  // Note that it's important for this check to come after the status
  // code check above, since we should only record failures whose statuses
  // match the configured retryable status codes, so that we don't count
  // things like failures due to malformed requests (INVALID_ARGUMENT).
  // Conversely, it's important for this to come before the remaining
  // checks, so that we don't fail to record failures due to other factors.
  if (calld_->retry_throttle_data_ != nullptr &&
      !calld_->retry_throttle_data_->RecordFailure()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p attempt=%p: retries throttled",
              calld_->chand_, calld_, this);
    }
    return false;
  }
  // Check whether the call is committed.
  if (calld_->retry_committed_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p attempt=%p: retries already committed",
              calld_->chand_, calld_, this);
    }
    return false;
  }
  // Check whether we have retries remaining.
  ++calld_->num_attempts_completed_;
  if (calld_->num_attempts_completed_ >=
      calld_->retry_policy_->max_attempts()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(
          GPR_INFO, "chand=%p calld=%p attempt=%p: exceeded %d retry attempts",
          calld_->chand_, calld_, this, calld_->retry_policy_->max_attempts());
    }
    return false;
  }
  // Check server push-back.
  if (server_pushback.has_value()) {
    if (*server_pushback < Duration::Zero()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p attempt=%p: not retrying due to server "
                "push-back",
                calld_->chand_, calld_, this);
      }
      return false;
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(
            GPR_INFO,
            "chand=%p calld=%p attempt=%p: server push-back: retry in %" PRIu64
            " ms",
            calld_->chand_, calld_, this, server_pushback->millis());
      }
    }
  }
  // Check with call dispatch controller.
  auto* service_config_call_data =
      static_cast<ClientChannelServiceConfigCallData*>(
          calld_->call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
  if (!service_config_call_data->call_dispatch_controller()->ShouldRetry()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(
          GPR_INFO,
          "chand=%p calld=%p attempt=%p: call dispatch controller denied retry",
          calld_->chand_, calld_, this);
    }
    return false;
  }
  // We should retry.
  return true;
}

void RetryFilter::CallData::CallAttempt::Abandon() {
  abandoned_ = true;
  // Unref batches for deferred completion callbacks that will now never
  // be invoked.
  if (started_recv_trailing_metadata_ &&
      !seen_recv_trailing_metadata_from_surface_) {
    recv_trailing_metadata_internal_batch_.reset(
        DEBUG_LOCATION,
        "unref internal recv_trailing_metadata_ready batch; attempt abandoned");
  }
  GRPC_ERROR_UNREF(recv_trailing_metadata_error_);
  recv_trailing_metadata_error_ = GRPC_ERROR_NONE;
  recv_initial_metadata_ready_deferred_batch_.reset(
      DEBUG_LOCATION,
      "unref deferred recv_initial_metadata_ready batch; attempt abandoned");
  GRPC_ERROR_UNREF(recv_initial_metadata_error_);
  recv_initial_metadata_error_ = GRPC_ERROR_NONE;
  recv_message_ready_deferred_batch_.reset(
      DEBUG_LOCATION,
      "unref deferred recv_message_ready batch; attempt abandoned");
  GRPC_ERROR_UNREF(recv_message_error_);
  recv_message_error_ = GRPC_ERROR_NONE;
  for (auto& on_complete_deferred_batch : on_complete_deferred_batches_) {
    on_complete_deferred_batch.batch.reset(
        DEBUG_LOCATION, "unref deferred on_complete batch; attempt abandoned");
    GRPC_ERROR_UNREF(on_complete_deferred_batch.error);
  }
  on_complete_deferred_batches_.clear();
}

void RetryFilter::CallData::CallAttempt::OnPerAttemptRecvTimer(
    void* arg, grpc_error_handle error) {
  auto* call_attempt = static_cast<CallAttempt*>(arg);
  GRPC_CLOSURE_INIT(&call_attempt->on_per_attempt_recv_timer_,
                    OnPerAttemptRecvTimerLocked, call_attempt, nullptr);
  GRPC_CALL_COMBINER_START(call_attempt->calld_->call_combiner_,
                           &call_attempt->on_per_attempt_recv_timer_,
                           GRPC_ERROR_REF(error), "per-attempt timer fired");
}

void RetryFilter::CallData::CallAttempt::OnPerAttemptRecvTimerLocked(
    void* arg, grpc_error_handle error) {
  auto* call_attempt = static_cast<CallAttempt*>(arg);
  auto* calld = call_attempt->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p: perAttemptRecvTimeout timer fired: "
            "error=%s, per_attempt_recv_timer_pending_=%d",
            calld->chand_, calld, call_attempt,
            grpc_error_std_string(error).c_str(),
            call_attempt->per_attempt_recv_timer_pending_);
  }
  CallCombinerClosureList closures;
  if (GRPC_ERROR_IS_NONE(error) &&
      call_attempt->per_attempt_recv_timer_pending_) {
    call_attempt->per_attempt_recv_timer_pending_ = false;
    // Cancel this attempt.
    // TODO(roth): When implementing hedging, we should not cancel the
    // current attempt.
    call_attempt->MaybeAddBatchForCancelOp(
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                               "retry perAttemptRecvTimeout exceeded"),
                           GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_CANCELLED),
        &closures);
    // Check whether we should retry.
    if (call_attempt->ShouldRetry(/*status=*/absl::nullopt,
                                  /*server_pushback_ms=*/absl::nullopt)) {
      // Mark current attempt as abandoned.
      call_attempt->Abandon();
      // We are retrying.  Start backoff timer.
      calld->StartRetryTimer(/*server_pushback=*/absl::nullopt);
    } else {
      // Not retrying, so commit the call.
      calld->RetryCommit(call_attempt);
      // If retry state is no longer needed, switch to fast path for
      // subsequent batches.
      call_attempt->MaybeSwitchToFastPath();
    }
  }
  closures.RunClosures(calld->call_combiner_);
  call_attempt->Unref(DEBUG_LOCATION, "OnPerAttemptRecvTimer");
  GRPC_CALL_STACK_UNREF(calld->owning_call_, "OnPerAttemptRecvTimer");
}

void RetryFilter::CallData::CallAttempt::MaybeCancelPerAttemptRecvTimer() {
  if (per_attempt_recv_timer_pending_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p attempt=%p: cancelling "
              "perAttemptRecvTimeout timer",
              calld_->chand_, calld_, this);
    }
    per_attempt_recv_timer_pending_ = false;
    grpc_timer_cancel(&per_attempt_recv_timer_);
  }
}

//
// RetryFilter::CallData::CallAttempt::BatchData
//

RetryFilter::CallData::CallAttempt::BatchData::BatchData(
    RefCountedPtr<CallAttempt> attempt, int refcount, bool set_on_complete)
    : RefCounted(
          GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace) ? "BatchData" : nullptr,
          refcount),
      call_attempt_(std::move(attempt)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p attempt=%p: creating batch %p",
            call_attempt_->calld_->chand_, call_attempt_->calld_,
            call_attempt_.get(), this);
  }
  // We hold a ref to the call stack for every batch sent on a call attempt.
  // This is because some batches on the call attempt may not complete
  // until after all of the batches are completed at the surface (because
  // each batch that is pending at the surface holds a ref).  This
  // can happen for replayed send ops, and it can happen for
  // recv_initial_metadata and recv_message ops on a call attempt that has
  // been abandoned.
  GRPC_CALL_STACK_REF(call_attempt_->calld_->owning_call_, "Retry BatchData");
  batch_.payload = &call_attempt_->batch_payload_;
  if (set_on_complete) {
    GRPC_CLOSURE_INIT(&on_complete_, OnComplete, this, nullptr);
    batch_.on_complete = &on_complete_;
  }
}

RetryFilter::CallData::CallAttempt::BatchData::~BatchData() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p attempt=%p: destroying batch %p",
            call_attempt_->calld_->chand_, call_attempt_->calld_,
            call_attempt_.get(), this);
  }
  GRPC_CALL_STACK_UNREF(call_attempt_->calld_->owning_call_, "Retry BatchData");
  call_attempt_.reset(DEBUG_LOCATION, "~BatchData");
}

void RetryFilter::CallData::CallAttempt::BatchData::
    FreeCachedSendOpDataForCompletedBatch() {
  auto* calld = call_attempt_->calld_;
  // TODO(roth): When we implement hedging, this logic will need to get
  // a bit more complex, because there may be other (now abandoned) call
  // attempts still using this data.  We may need to do some sort of
  // ref-counting instead.
  if (batch_.send_initial_metadata) {
    calld->FreeCachedSendInitialMetadata();
  }
  if (batch_.send_message) {
    calld->FreeCachedSendMessage(call_attempt_->completed_send_message_count_ -
                                 1);
  }
  if (batch_.send_trailing_metadata) {
    calld->FreeCachedSendTrailingMetadata();
  }
}

//
// recv_initial_metadata callback handling
//

void RetryFilter::CallData::CallAttempt::BatchData::
    MaybeAddClosureForRecvInitialMetadataCallback(
        grpc_error_handle error, CallCombinerClosureList* closures) {
  // Find pending batch.
  PendingBatch* pending = call_attempt_->calld_->PendingBatchFind(
      "invoking recv_initial_metadata_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_initial_metadata &&
               batch->payload->recv_initial_metadata
                       .recv_initial_metadata_ready != nullptr;
      });
  if (pending == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  // Return metadata.
  *pending->batch->payload->recv_initial_metadata.recv_initial_metadata =
      std::move(call_attempt_->recv_initial_metadata_);
  // Propagate trailing_metadata_available.
  *pending->batch->payload->recv_initial_metadata.trailing_metadata_available =
      call_attempt_->trailing_metadata_available_;
  // Update bookkeeping.
  // Note: Need to do this before invoking the callback, since invoking
  // the callback will result in yielding the call combiner.
  grpc_closure* recv_initial_metadata_ready =
      pending->batch->payload->recv_initial_metadata
          .recv_initial_metadata_ready;
  pending->batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
      nullptr;
  call_attempt_->calld_->MaybeClearPendingBatch(pending);
  // Add callback to closures.
  closures->Add(recv_initial_metadata_ready, error,
                "recv_initial_metadata_ready for pending batch");
}

void RetryFilter::CallData::CallAttempt::BatchData::RecvInitialMetadataReady(
    void* arg, grpc_error_handle error) {
  RefCountedPtr<BatchData> batch_data(static_cast<BatchData*>(arg));
  CallAttempt* call_attempt = batch_data->call_attempt_.get();
  CallData* calld = call_attempt->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p batch_data=%p: "
            "got recv_initial_metadata_ready, error=%s",
            calld->chand_, calld, call_attempt, batch_data.get(),
            grpc_error_std_string(error).c_str());
  }
  call_attempt->completed_recv_initial_metadata_ = true;
  // If this attempt has been abandoned, then we're not going to use the
  // result of this recv_initial_metadata op, so do nothing.
  if (call_attempt->abandoned_) {
    GRPC_CALL_COMBINER_STOP(
        calld->call_combiner_,
        "recv_initial_metadata_ready for abandoned attempt");
    return;
  }
  // Cancel per-attempt recv timer, if any.
  call_attempt->MaybeCancelPerAttemptRecvTimer();
  // If we're not committed, check the response to see if we need to commit.
  if (!calld->retry_committed_) {
    // If we got an error or a Trailers-Only response and have not yet gotten
    // the recv_trailing_metadata_ready callback, then defer propagating this
    // callback back to the surface.  We can evaluate whether to retry when
    // recv_trailing_metadata comes back.
    if (GPR_UNLIKELY((call_attempt->trailing_metadata_available_ ||
                      !GRPC_ERROR_IS_NONE(error)) &&
                     !call_attempt->completed_recv_trailing_metadata_)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p attempt=%p: deferring "
                "recv_initial_metadata_ready (Trailers-Only)",
                calld->chand_, calld, call_attempt);
      }
      call_attempt->recv_initial_metadata_ready_deferred_batch_ =
          std::move(batch_data);
      call_attempt->recv_initial_metadata_error_ = GRPC_ERROR_REF(error);
      CallCombinerClosureList closures;
      if (!GRPC_ERROR_IS_NONE(error)) {
        call_attempt->MaybeAddBatchForCancelOp(GRPC_ERROR_REF(error),
                                               &closures);
      }
      if (!call_attempt->started_recv_trailing_metadata_) {
        // recv_trailing_metadata not yet started by application; start it
        // ourselves to get status.
        call_attempt->AddBatchForInternalRecvTrailingMetadata(&closures);
      }
      closures.RunClosures(calld->call_combiner_);
      return;
    }
    // Received valid initial metadata, so commit the call.
    calld->RetryCommit(call_attempt);
    // If retry state is no longer needed, switch to fast path for
    // subsequent batches.
    call_attempt->MaybeSwitchToFastPath();
  }
  // Invoke the callback to return the result to the surface.
  CallCombinerClosureList closures;
  batch_data->MaybeAddClosureForRecvInitialMetadataCallback(
      GRPC_ERROR_REF(error), &closures);
  closures.RunClosures(calld->call_combiner_);
}

//
// recv_message callback handling
//

void RetryFilter::CallData::CallAttempt::BatchData::
    MaybeAddClosureForRecvMessageCallback(grpc_error_handle error,
                                          CallCombinerClosureList* closures) {
  // Find pending op.
  PendingBatch* pending = call_attempt_->calld_->PendingBatchFind(
      "invoking recv_message_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_message &&
               batch->payload->recv_message.recv_message_ready != nullptr;
      });
  if (pending == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  // Return payload.
  *pending->batch->payload->recv_message.recv_message =
      std::move(call_attempt_->recv_message_);
  *pending->batch->payload->recv_message.flags =
      call_attempt_->recv_message_flags_;
  // Update bookkeeping.
  // Note: Need to do this before invoking the callback, since invoking
  // the callback will result in yielding the call combiner.
  grpc_closure* recv_message_ready =
      pending->batch->payload->recv_message.recv_message_ready;
  pending->batch->payload->recv_message.recv_message_ready = nullptr;
  call_attempt_->calld_->MaybeClearPendingBatch(pending);
  // Add callback to closures.
  closures->Add(recv_message_ready, error,
                "recv_message_ready for pending batch");
}

void RetryFilter::CallData::CallAttempt::BatchData::RecvMessageReady(
    void* arg, grpc_error_handle error) {
  RefCountedPtr<BatchData> batch_data(static_cast<BatchData*>(arg));
  CallAttempt* call_attempt = batch_data->call_attempt_.get();
  CallData* calld = call_attempt->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p batch_data=%p: "
            "got recv_message_ready, error=%s",
            calld->chand_, calld, call_attempt, batch_data.get(),
            grpc_error_std_string(error).c_str());
  }
  ++call_attempt->completed_recv_message_count_;
  // If this attempt has been abandoned, then we're not going to use the
  // result of this recv_message op, so do nothing.
  if (call_attempt->abandoned_) {
    // The transport will not invoke recv_trailing_metadata_ready until the byte
    // stream for any recv_message op is orphaned, so we do that here to ensure
    // that any pending recv_trailing_metadata op can complete.
    call_attempt->recv_message_.reset();
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                            "recv_message_ready for abandoned attempt");
    return;
  }
  // Cancel per-attempt recv timer, if any.
  call_attempt->MaybeCancelPerAttemptRecvTimer();
  // If we're not committed, check the response to see if we need to commit.
  if (!calld->retry_committed_) {
    // If we got an error or the payload was nullptr and we have not yet gotten
    // the recv_trailing_metadata_ready callback, then defer propagating this
    // callback back to the surface.  We can evaluate whether to retry when
    // recv_trailing_metadata comes back.
    if (GPR_UNLIKELY((!call_attempt->recv_message_.has_value() ||
                      !GRPC_ERROR_IS_NONE(error)) &&
                     !call_attempt->completed_recv_trailing_metadata_)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p attempt=%p: deferring recv_message_ready "
                "(nullptr message and recv_trailing_metadata pending)",
                calld->chand_, calld, call_attempt);
      }
      call_attempt->recv_message_ready_deferred_batch_ = std::move(batch_data);
      call_attempt->recv_message_error_ = GRPC_ERROR_REF(error);
      CallCombinerClosureList closures;
      if (!GRPC_ERROR_IS_NONE(error)) {
        call_attempt->MaybeAddBatchForCancelOp(GRPC_ERROR_REF(error),
                                               &closures);
      }
      if (!call_attempt->started_recv_trailing_metadata_) {
        // recv_trailing_metadata not yet started by application; start it
        // ourselves to get status.
        call_attempt->AddBatchForInternalRecvTrailingMetadata(&closures);
      }
      closures.RunClosures(calld->call_combiner_);
      return;
    }
    // Received a valid message, so commit the call.
    calld->RetryCommit(call_attempt);
    // If retry state is no longer needed, switch to fast path for
    // subsequent batches.
    call_attempt->MaybeSwitchToFastPath();
  }
  // Invoke the callback to return the result to the surface.
  CallCombinerClosureList closures;
  batch_data->MaybeAddClosureForRecvMessageCallback(GRPC_ERROR_REF(error),
                                                    &closures);
  closures.RunClosures(calld->call_combiner_);
}

//
// recv_trailing_metadata handling
//

namespace {

// Sets *status, *server_pushback, and *is_lb_drop based on md_batch
// and error.
void GetCallStatus(
    Timestamp deadline, grpc_metadata_batch* md_batch, grpc_error_handle error,
    grpc_status_code* status, absl::optional<Duration>* server_pushback,
    bool* is_lb_drop,
    absl::optional<GrpcStreamNetworkState::ValueType>* stream_network_state) {
  if (!GRPC_ERROR_IS_NONE(error)) {
    grpc_error_get_status(error, deadline, status, nullptr, nullptr, nullptr);
    intptr_t value = 0;
    if (grpc_error_get_int(error, GRPC_ERROR_INT_LB_POLICY_DROP, &value) &&
        value != 0) {
      *is_lb_drop = true;
    }
  } else {
    *status = *md_batch->get(GrpcStatusMetadata());
  }
  *server_pushback = md_batch->get(GrpcRetryPushbackMsMetadata());
  *stream_network_state = md_batch->get(GrpcStreamNetworkState());
  GRPC_ERROR_UNREF(error);
}

}  // namespace

void RetryFilter::CallData::CallAttempt::BatchData::
    MaybeAddClosureForRecvTrailingMetadataReady(
        grpc_error_handle error, CallCombinerClosureList* closures) {
  auto* calld = call_attempt_->calld_;
  // Find pending batch.
  PendingBatch* pending = calld->PendingBatchFind(
      "invoking recv_trailing_metadata_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_trailing_metadata &&
               batch->payload->recv_trailing_metadata
                       .recv_trailing_metadata_ready != nullptr;
      });
  // If we generated the recv_trailing_metadata op internally via
  // AddBatchForInternalRecvTrailingMetadata(), then there will be no
  // pending batch.
  if (pending == nullptr) {
    call_attempt_->recv_trailing_metadata_error_ = error;
    return;
  }
  // Copy transport stats to be delivered up to the surface.
  grpc_transport_move_stats(
      &call_attempt_->collect_stats_,
      pending->batch->payload->recv_trailing_metadata.collect_stats);
  // Return metadata.
  *pending->batch->payload->recv_trailing_metadata.recv_trailing_metadata =
      std::move(call_attempt_->recv_trailing_metadata_);
  // Add closure.
  closures->Add(pending->batch->payload->recv_trailing_metadata
                    .recv_trailing_metadata_ready,
                error, "recv_trailing_metadata_ready for pending batch");
  // Update bookkeeping.
  pending->batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      nullptr;
  calld->MaybeClearPendingBatch(pending);
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddClosuresForDeferredCompletionCallbacks(
        CallCombinerClosureList* closures) {
  // Add closure for deferred recv_initial_metadata_ready.
  if (GPR_UNLIKELY(call_attempt_->recv_initial_metadata_ready_deferred_batch_ !=
                   nullptr)) {
    MaybeAddClosureForRecvInitialMetadataCallback(
        call_attempt_->recv_initial_metadata_error_, closures);
    call_attempt_->recv_initial_metadata_ready_deferred_batch_.reset(
        DEBUG_LOCATION, "resuming deferred recv_initial_metadata_ready");
    call_attempt_->recv_initial_metadata_error_ = GRPC_ERROR_NONE;
  }
  // Add closure for deferred recv_message_ready.
  if (GPR_UNLIKELY(call_attempt_->recv_message_ready_deferred_batch_ !=
                   nullptr)) {
    MaybeAddClosureForRecvMessageCallback(call_attempt_->recv_message_error_,
                                          closures);
    call_attempt_->recv_message_ready_deferred_batch_.reset(
        DEBUG_LOCATION, "resuming deferred recv_message_ready");
    call_attempt_->recv_message_error_ = GRPC_ERROR_NONE;
  }
  // Add closures for deferred on_complete callbacks.
  for (auto& on_complete_deferred_batch :
       call_attempt_->on_complete_deferred_batches_) {
    closures->Add(&on_complete_deferred_batch.batch->on_complete_,
                  on_complete_deferred_batch.error, "resuming on_complete");
    on_complete_deferred_batch.batch.release();
  }
  call_attempt_->on_complete_deferred_batches_.clear();
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddClosuresToFailUnstartedPendingBatches(
        grpc_error_handle error, CallCombinerClosureList* closures) {
  auto* calld = call_attempt_->calld_;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches_); ++i) {
    PendingBatch* pending = &calld->pending_batches_[i];
    if (pending->batch == nullptr) continue;
    if (call_attempt_->PendingBatchContainsUnstartedSendOps(pending)) {
      closures->Add(pending->batch->on_complete, GRPC_ERROR_REF(error),
                    "failing on_complete for pending batch");
      pending->batch->on_complete = nullptr;
      calld->MaybeClearPendingBatch(pending);
    }
  }
  GRPC_ERROR_UNREF(error);
}

void RetryFilter::CallData::CallAttempt::BatchData::RunClosuresForCompletedCall(
    grpc_error_handle error) {
  // Construct list of closures to execute.
  CallCombinerClosureList closures;
  // First, add closure for recv_trailing_metadata_ready.
  MaybeAddClosureForRecvTrailingMetadataReady(GRPC_ERROR_REF(error), &closures);
  // If there are deferred batch completion callbacks, add them to closures.
  AddClosuresForDeferredCompletionCallbacks(&closures);
  // Add closures to fail any pending batches that have not yet been started.
  AddClosuresToFailUnstartedPendingBatches(GRPC_ERROR_REF(error), &closures);
  // Schedule all of the closures identified above.
  // Note: This will release the call combiner.
  closures.RunClosures(call_attempt_->calld_->call_combiner_);
  GRPC_ERROR_UNREF(error);
}

void RetryFilter::CallData::CallAttempt::BatchData::RecvTrailingMetadataReady(
    void* arg, grpc_error_handle error) {
  // Add a ref to the outer call stack. This works around a TSAN reported
  // failure that we do not understand, but since we expect the lifetime of
  // this code to be relatively short we think it's OK to work around for now.
  // TSAN failure:
  // https://source.cloud.google.com/results/invocations/5b122974-4977-4862-beb1-dc1af9fbbd1d/targets/%2F%2Ftest%2Fcore%2Fend2end:h2_census_test@max_concurrent_streams@poller%3Dpoll/log
  RefCountedPtr<grpc_call_stack> owning_call_stack =
      static_cast<BatchData*>(arg)->call_attempt_->calld_->owning_call_->Ref();
  RefCountedPtr<BatchData> batch_data(static_cast<BatchData*>(arg));
  CallAttempt* call_attempt = batch_data->call_attempt_.get();
  CallData* calld = call_attempt->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p batch_data=%p: "
            "got recv_trailing_metadata_ready, error=%s",
            calld->chand_, calld, call_attempt, batch_data.get(),
            grpc_error_std_string(error).c_str());
  }
  call_attempt->completed_recv_trailing_metadata_ = true;
  // If this attempt has been abandoned, then we're not going to use the
  // result of this recv_trailing_metadata op, so do nothing.
  if (call_attempt->abandoned_) {
    GRPC_CALL_COMBINER_STOP(
        calld->call_combiner_,
        "recv_trailing_metadata_ready for abandoned attempt");
    return;
  }
  // Cancel per-attempt recv timer, if any.
  call_attempt->MaybeCancelPerAttemptRecvTimer();
  // Get the call's status and check for server pushback metadata.
  grpc_status_code status = GRPC_STATUS_OK;
  absl::optional<Duration> server_pushback;
  bool is_lb_drop = false;
  absl::optional<GrpcStreamNetworkState::ValueType> stream_network_state;
  grpc_metadata_batch* md_batch =
      batch_data->batch_.payload->recv_trailing_metadata.recv_trailing_metadata;
  GetCallStatus(calld->deadline_, md_batch, GRPC_ERROR_REF(error), &status,
                &server_pushback, &is_lb_drop, &stream_network_state);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p: call finished, status=%s "
            "server_pushback=%s is_lb_drop=%d stream_network_state=%s",
            calld->chand_, calld, call_attempt,
            grpc_status_code_to_string(status),
            server_pushback.has_value() ? server_pushback->ToString().c_str()
                                        : "N/A",
            is_lb_drop,
            stream_network_state.has_value()
                ? absl::StrCat(*stream_network_state).c_str()
                : "N/A");
  }
  // Check if we should retry.
  if (!is_lb_drop) {  // Never retry on LB drops.
    enum { kNoRetry, kTransparentRetry, kConfigurableRetry } retry = kNoRetry;
    // Handle transparent retries.
    if (stream_network_state.has_value() && !calld->retry_committed_) {
      // If not sent on wire, then always retry.
      // If sent on wire but not seen by server, retry exactly once.
      if (*stream_network_state == GrpcStreamNetworkState::kNotSentOnWire) {
        retry = kTransparentRetry;
      } else if (*stream_network_state ==
                     GrpcStreamNetworkState::kNotSeenByServer &&
                 !calld->sent_transparent_retry_not_seen_by_server_) {
        calld->sent_transparent_retry_not_seen_by_server_ = true;
        retry = kTransparentRetry;
      }
    }
    // If not transparently retrying, check for configurable retry.
    if (retry == kNoRetry &&
        call_attempt->ShouldRetry(status, server_pushback)) {
      retry = kConfigurableRetry;
    }
    // If we're retrying, do so.
    if (retry != kNoRetry) {
      CallCombinerClosureList closures;
      // Cancel call attempt.
      call_attempt->MaybeAddBatchForCancelOp(
          GRPC_ERROR_IS_NONE(error)
              ? grpc_error_set_int(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING("call attempt failed"),
                    GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_CANCELLED)
              : GRPC_ERROR_REF(error),
          &closures);
      // For transparent retries, add a closure to immediately start a new
      // call attempt.
      // For configurable retries, start retry timer.
      if (retry == kTransparentRetry) {
        calld->AddClosureToStartTransparentRetry(&closures);
      } else {
        calld->StartRetryTimer(server_pushback);
      }
      // Record that this attempt has been abandoned.
      call_attempt->Abandon();
      // Yields call combiner.
      closures.RunClosures(calld->call_combiner_);
      return;
    }
  }
  // Not retrying, so commit the call.
  calld->RetryCommit(call_attempt);
  // If retry state is no longer needed, switch to fast path for
  // subsequent batches.
  call_attempt->MaybeSwitchToFastPath();
  // Run any necessary closures.
  batch_data->RunClosuresForCompletedCall(GRPC_ERROR_REF(error));
}

//
// on_complete callback handling
//

void RetryFilter::CallData::CallAttempt::BatchData::
    AddClosuresForCompletedPendingBatch(grpc_error_handle error,
                                        CallCombinerClosureList* closures) {
  auto* calld = call_attempt_->calld_;
  PendingBatch* pending = calld->PendingBatchFind(
      "completed", [this](grpc_transport_stream_op_batch* batch) {
        // Match the pending batch with the same set of send ops as the
        // batch we've just completed.
        return batch->on_complete != nullptr &&
               batch_.send_initial_metadata == batch->send_initial_metadata &&
               batch_.send_message == batch->send_message &&
               batch_.send_trailing_metadata == batch->send_trailing_metadata;
      });
  // If batch_data is a replay batch, then there will be no pending
  // batch to complete.
  if (pending == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  // Propagate payload.
  if (batch_.send_message) {
    pending->batch->payload->send_message.stream_write_closed =
        batch_.payload->send_message.stream_write_closed;
  }
  // Add closure.
  closures->Add(pending->batch->on_complete, error,
                "on_complete for pending batch");
  pending->batch->on_complete = nullptr;
  calld->MaybeClearPendingBatch(pending);
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddClosuresForReplayOrPendingSendOps(CallCombinerClosureList* closures) {
  auto* calld = call_attempt_->calld_;
  bool have_pending_send_ops = call_attempt_->HaveSendOpsToReplay();
  // We don't check send_initial_metadata here, because that op will always
  // be started as soon as it is received from the surface, so it will
  // never need to be started at this point.
  if (!have_pending_send_ops) {
    for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches_); ++i) {
      PendingBatch* pending = &calld->pending_batches_[i];
      grpc_transport_stream_op_batch* batch = pending->batch;
      if (batch == nullptr || pending->send_ops_cached) continue;
      if (batch->send_message || batch->send_trailing_metadata) {
        have_pending_send_ops = true;
        break;
      }
    }
  }
  if (have_pending_send_ops) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p attempt=%p: starting next batch for pending "
              "send op(s)",
              calld->chand_, calld, call_attempt_.get());
    }
    call_attempt_->AddRetriableBatches(closures);
  }
}

void RetryFilter::CallData::CallAttempt::BatchData::OnComplete(
    void* arg, grpc_error_handle error) {
  RefCountedPtr<BatchData> batch_data(static_cast<BatchData*>(arg));
  CallAttempt* call_attempt = batch_data->call_attempt_.get();
  CallData* calld = call_attempt->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p batch_data=%p: "
            "got on_complete, error=%s, batch=%s",
            calld->chand_, calld, call_attempt, batch_data.get(),
            grpc_error_std_string(error).c_str(),
            grpc_transport_stream_op_batch_string(&batch_data->batch_).c_str());
  }
  // If this attempt has been abandoned, then we're not going to propagate
  // the completion of this batch, so do nothing.
  if (call_attempt->abandoned_) {
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                            "on_complete for abandoned attempt");
    return;
  }
  // If we got an error and have not yet gotten the
  // recv_trailing_metadata_ready callback, then defer propagating this
  // callback back to the surface.  We can evaluate whether to retry when
  // recv_trailing_metadata comes back.
  if (GPR_UNLIKELY(!calld->retry_committed_ && !GRPC_ERROR_IS_NONE(error) &&
                   !call_attempt->completed_recv_trailing_metadata_)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p attempt=%p: deferring on_complete",
              calld->chand_, calld, call_attempt);
    }
    call_attempt->on_complete_deferred_batches_.emplace_back(
        std::move(batch_data), GRPC_ERROR_REF(error));
    CallCombinerClosureList closures;
    call_attempt->MaybeAddBatchForCancelOp(GRPC_ERROR_REF(error), &closures);
    if (!call_attempt->started_recv_trailing_metadata_) {
      // recv_trailing_metadata not yet started by application; start it
      // ourselves to get status.
      call_attempt->AddBatchForInternalRecvTrailingMetadata(&closures);
    }
    closures.RunClosures(calld->call_combiner_);
    return;
  }
  // Update bookkeeping in call_attempt.
  if (batch_data->batch_.send_initial_metadata) {
    call_attempt->completed_send_initial_metadata_ = true;
  }
  if (batch_data->batch_.send_message) {
    ++call_attempt->completed_send_message_count_;
  }
  if (batch_data->batch_.send_trailing_metadata) {
    call_attempt->completed_send_trailing_metadata_ = true;
  }
  // If the call is committed, free cached data for send ops that we've just
  // completed.
  if (calld->retry_committed_) {
    batch_data->FreeCachedSendOpDataForCompletedBatch();
  }
  // Construct list of closures to execute.
  CallCombinerClosureList closures;
  // Add closure for the completed pending batch, if any.
  batch_data->AddClosuresForCompletedPendingBatch(GRPC_ERROR_REF(error),
                                                  &closures);
  // If needed, add a callback to start any replay or pending send ops on
  // the LB call.
  if (!call_attempt->completed_recv_trailing_metadata_) {
    batch_data->AddClosuresForReplayOrPendingSendOps(&closures);
  }
  // If retry state is no longer needed (i.e., we're committed and there
  // are no more send ops to replay), switch to fast path for subsequent
  // batches.
  call_attempt->MaybeSwitchToFastPath();
  // Schedule all of the closures identified above.
  // Note: This yields the call combiner.
  closures.RunClosures(calld->call_combiner_);
}

void RetryFilter::CallData::CallAttempt::BatchData::OnCompleteForCancelOp(
    void* arg, grpc_error_handle error) {
  RefCountedPtr<BatchData> batch_data(static_cast<BatchData*>(arg));
  CallAttempt* call_attempt = batch_data->call_attempt_.get();
  CallData* calld = call_attempt->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p attempt=%p batch_data=%p: "
            "got on_complete for cancel_stream batch, error=%s, batch=%s",
            calld->chand_, calld, call_attempt, batch_data.get(),
            grpc_error_std_string(error).c_str(),
            grpc_transport_stream_op_batch_string(&batch_data->batch_).c_str());
  }
  GRPC_CALL_COMBINER_STOP(
      calld->call_combiner_,
      "on_complete for internally generated cancel_stream op");
}

//
// retriable batch construction
//

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableSendInitialMetadataOp() {
  auto* calld = call_attempt_->calld_;
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  //
  // If we've already completed one or more attempts, add the
  // grpc-retry-attempts header.
  call_attempt_->send_initial_metadata_ = calld->send_initial_metadata_.Copy();
  if (GPR_UNLIKELY(calld->num_attempts_completed_ > 0)) {
    call_attempt_->send_initial_metadata_.Set(GrpcPreviousRpcAttemptsMetadata(),
                                              calld->num_attempts_completed_);
  } else {
    call_attempt_->send_initial_metadata_.Remove(
        GrpcPreviousRpcAttemptsMetadata());
  }
  call_attempt_->started_send_initial_metadata_ = true;
  batch_.send_initial_metadata = true;
  batch_.payload->send_initial_metadata.send_initial_metadata =
      &call_attempt_->send_initial_metadata_;
  batch_.payload->send_initial_metadata.peer_string = calld->peer_string_;
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableSendMessageOp() {
  auto* calld = call_attempt_->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(
        GPR_INFO,
        "chand=%p calld=%p attempt=%p: starting calld->send_messages[%" PRIuPTR
        "]",
        calld->chand_, calld, call_attempt_.get(),
        call_attempt_->started_send_message_count_);
  }
  CachedSendMessage cache =
      calld->send_messages_[call_attempt_->started_send_message_count_];
  ++call_attempt_->started_send_message_count_;
  batch_.send_message = true;
  batch_.payload->send_message.send_message = cache.slices;
  batch_.payload->send_message.flags = cache.flags;
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableSendTrailingMetadataOp() {
  auto* calld = call_attempt_->calld_;
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  call_attempt_->send_trailing_metadata_ =
      calld->send_trailing_metadata_.Copy();
  call_attempt_->started_send_trailing_metadata_ = true;
  batch_.send_trailing_metadata = true;
  batch_.payload->send_trailing_metadata.send_trailing_metadata =
      &call_attempt_->send_trailing_metadata_;
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableRecvInitialMetadataOp() {
  call_attempt_->started_recv_initial_metadata_ = true;
  batch_.recv_initial_metadata = true;
  call_attempt_->recv_initial_metadata_.Clear();
  batch_.payload->recv_initial_metadata.recv_initial_metadata =
      &call_attempt_->recv_initial_metadata_;
  batch_.payload->recv_initial_metadata.trailing_metadata_available =
      &call_attempt_->trailing_metadata_available_;
  GRPC_CLOSURE_INIT(&call_attempt_->recv_initial_metadata_ready_,
                    RecvInitialMetadataReady, this, grpc_schedule_on_exec_ctx);
  batch_.payload->recv_initial_metadata.recv_initial_metadata_ready =
      &call_attempt_->recv_initial_metadata_ready_;
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableRecvMessageOp() {
  ++call_attempt_->started_recv_message_count_;
  batch_.recv_message = true;
  batch_.payload->recv_message.recv_message = &call_attempt_->recv_message_;
  batch_.payload->recv_message.flags = &call_attempt_->recv_message_flags_;
  batch_.payload->recv_message.call_failed_before_recv_message = nullptr;
  GRPC_CLOSURE_INIT(&call_attempt_->recv_message_ready_, RecvMessageReady, this,
                    grpc_schedule_on_exec_ctx);
  batch_.payload->recv_message.recv_message_ready =
      &call_attempt_->recv_message_ready_;
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableRecvTrailingMetadataOp() {
  call_attempt_->started_recv_trailing_metadata_ = true;
  batch_.recv_trailing_metadata = true;
  call_attempt_->recv_trailing_metadata_.Clear();
  batch_.payload->recv_trailing_metadata.recv_trailing_metadata =
      &call_attempt_->recv_trailing_metadata_;
  batch_.payload->recv_trailing_metadata.collect_stats =
      &call_attempt_->collect_stats_;
  GRPC_CLOSURE_INIT(&call_attempt_->recv_trailing_metadata_ready_,
                    RecvTrailingMetadataReady, this, grpc_schedule_on_exec_ctx);
  batch_.payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      &call_attempt_->recv_trailing_metadata_ready_;
}

void RetryFilter::CallData::CallAttempt::BatchData::AddCancelStreamOp(
    grpc_error_handle error) {
  batch_.cancel_stream = true;
  batch_.payload->cancel_stream.cancel_error = error;
  // Override on_complete callback.
  GRPC_CLOSURE_INIT(&on_complete_, OnCompleteForCancelOp, this, nullptr);
}

//
// CallData vtable functions
//

grpc_error_handle RetryFilter::CallData::Init(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  auto* chand = static_cast<RetryFilter*>(elem->channel_data);
  new (elem->call_data) CallData(chand, *args);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: created call", chand,
            elem->call_data);
  }
  return GRPC_ERROR_NONE;
}

void RetryFilter::CallData::Destroy(grpc_call_element* elem,
                                    const grpc_call_final_info* /*final_info*/,
                                    grpc_closure* then_schedule_closure) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  // Save our ref to the CallStackDestructionBarrier until after our
  // dtor is invoked.
  RefCountedPtr<CallStackDestructionBarrier> call_stack_destruction_barrier =
      std::move(calld->call_stack_destruction_barrier_);
  calld->~CallData();
  // Now set the callback in the CallStackDestructionBarrier object,
  // right before we release our ref to it (implicitly upon returning).
  // The callback will be invoked when the CallStackDestructionBarrier
  // is destroyed.
  call_stack_destruction_barrier->set_on_call_stack_destruction(
      then_schedule_closure);
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

const RetryMethodConfig* RetryFilter::GetRetryPolicy(
    const grpc_call_context_element* context) {
  if (context == nullptr) return nullptr;
  auto* svc_cfg_call_data = static_cast<ServiceConfigCallData*>(
      context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
  if (svc_cfg_call_data == nullptr) return nullptr;
  return static_cast<const RetryMethodConfig*>(
      svc_cfg_call_data->GetMethodParsedConfig(service_config_parser_index_));
}

RetryFilter::CallData::CallData(RetryFilter* chand,
                                const grpc_call_element_args& args)
    : chand_(chand),
      retry_throttle_data_(chand->retry_throttle_data_),
      retry_policy_(chand->GetRetryPolicy(args.context)),
      retry_backoff_(
          BackOff::Options()
              .set_initial_backoff(retry_policy_ == nullptr
                                       ? Duration::Zero()
                                       : retry_policy_->initial_backoff())
              .set_multiplier(retry_policy_ == nullptr
                                  ? 0
                                  : retry_policy_->backoff_multiplier())
              .set_jitter(RETRY_BACKOFF_JITTER)
              .set_max_backoff(retry_policy_ == nullptr
                                   ? Duration::Zero()
                                   : retry_policy_->max_backoff())),
      path_(grpc_slice_ref_internal(args.path)),
      deadline_(args.deadline),
      arena_(args.arena),
      owning_call_(args.call_stack),
      call_combiner_(args.call_combiner),
      call_context_(args.context),
      call_stack_destruction_barrier_(
          arena_->New<CallStackDestructionBarrier>()),
      pending_send_initial_metadata_(false),
      pending_send_message_(false),
      pending_send_trailing_metadata_(false),
      retry_committed_(false),
      retry_timer_pending_(false),
      retry_codepath_started_(false),
      sent_transparent_retry_not_seen_by_server_(false) {}

RetryFilter::CallData::~CallData() {
  FreeAllCachedSendOpData();
  grpc_slice_unref_internal(path_);
  // Make sure there are no remaining pending batches.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    GPR_ASSERT(pending_batches_[i].batch == nullptr);
  }
  GRPC_ERROR_UNREF(cancelled_from_surface_);
}

void RetryFilter::CallData::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace) &&
      !GRPC_TRACE_FLAG_ENABLED(grpc_trace_channel)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: batch started from surface: %s",
            chand_, this, grpc_transport_stream_op_batch_string(batch).c_str());
  }
  // If we have an LB call, delegate to the LB call.
  if (committed_call_ != nullptr) {
    // Note: This will release the call combiner.
    committed_call_->StartTransportStreamOpBatch(batch);
    return;
  }
  // If we were previously cancelled from the surface, fail this
  // batch immediately.
  if (!GRPC_ERROR_IS_NONE(cancelled_from_surface_)) {
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(cancelled_from_surface_), call_combiner_);
    return;
  }
  // Handle cancellation.
  if (GPR_UNLIKELY(batch->cancel_stream)) {
    // Save cancel_error in case subsequent batches are started.
    GRPC_ERROR_UNREF(cancelled_from_surface_);
    cancelled_from_surface_ =
        GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: cancelled from surface: %s", chand_,
              this, grpc_error_std_string(cancelled_from_surface_).c_str());
    }
    // Fail any pending batches.
    PendingBatchesFail(GRPC_ERROR_REF(cancelled_from_surface_));
    // If we have a current call attempt, commit the call, then send
    // the cancellation down to that attempt.  When the call fails, it
    // will not be retried, because we have committed it here.
    if (call_attempt_ != nullptr) {
      RetryCommit(call_attempt_.get());
      // TODO(roth): When implementing hedging, this will get more
      // complex, because instead of just passing the batch down to a
      // single call attempt, we'll need to cancel multiple call
      // attempts and wait for the cancellation on_complete from each call
      // attempt before we propagate the on_complete from this batch
      // back to the surface.
      // Note: This will release the call combiner.
      call_attempt_->CancelFromSurface(batch);
      return;
    }
    // Cancel retry timer if needed.
    if (retry_timer_pending_) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: cancelling retry timer", chand_,
                this);
      }
      retry_timer_pending_ = false;  // Lame timer callback.
      grpc_timer_cancel(&retry_timer_);
      FreeAllCachedSendOpData();
    }
    // We have no call attempt, so there's nowhere to send the cancellation
    // batch.  Return it back to the surface immediately.
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(cancelled_from_surface_), call_combiner_);
    return;
  }
  // Add the batch to the pending list.
  PendingBatch* pending = PendingBatchesAdd(batch);
  // If the timer is pending, yield the call combiner and wait for it to
  // run, since we don't want to start another call attempt until it does.
  if (retry_timer_pending_) {
    GRPC_CALL_COMBINER_STOP(call_combiner_,
                            "added pending batch while retry timer pending");
    return;
  }
  // If we do not yet have a call attempt, create one.
  if (call_attempt_ == nullptr) {
    // If this is the first batch and retries are already committed
    // (e.g., if this batch put the call above the buffer size limit), then
    // immediately create an LB call and delegate the batch to it.  This
    // avoids the overhead of unnecessarily allocating a CallAttempt
    // object or caching any of the send op data.
    // Note that we would ideally like to do this also on subsequent
    // attempts (e.g., if a batch puts the call above the buffer size
    // limit since the last attempt was complete), but in practice that's
    // not really worthwhile, because we will almost always have cached and
    // completed at least the send_initial_metadata op on the previous
    // attempt, which means that we'd need special logic to replay the
    // batch anyway, which is exactly what the CallAttempt object provides.
    // We also skip this optimization if perAttemptRecvTimeout is set in the
    // retry policy, because we need the code in CallAttempt to handle
    // the associated timer.
    if (!retry_codepath_started_ && retry_committed_ &&
        (retry_policy_ == nullptr ||
         !retry_policy_->per_attempt_recv_timeout().has_value())) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: retry committed before first attempt; "
                "creating LB call",
                chand_, this);
      }
      PendingBatchClear(pending);
      auto* service_config_call_data =
          static_cast<ClientChannelServiceConfigCallData*>(
              call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
      committed_call_ = CreateLoadBalancedCall(
          service_config_call_data->call_dispatch_controller(),
          /*is_transparent_retry=*/false);
      committed_call_->StartTransportStreamOpBatch(batch);
      return;
    }
    // Otherwise, create a call attempt.
    // The attempt will automatically start any necessary replays or
    // pending batches.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: creating call attempt", chand_,
              this);
    }
    retry_codepath_started_ = true;
    CreateCallAttempt(/*is_transparent_retry=*/false);
    return;
  }
  // Send batches to call attempt.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: starting batch on attempt=%p", chand_,
            this, call_attempt_.get());
  }
  call_attempt_->StartRetriableBatches();
}

OrphanablePtr<ClientChannel::LoadBalancedCall>
RetryFilter::CallData::CreateLoadBalancedCall(
    ConfigSelector::CallDispatchController* call_dispatch_controller,
    bool is_transparent_retry) {
  grpc_call_element_args args = {owning_call_, nullptr,          call_context_,
                                 path_,        /*start_time=*/0, deadline_,
                                 arena_,       call_combiner_};
  return chand_->client_channel_->CreateLoadBalancedCall(
      args, pollent_,
      // This callback holds a ref to the CallStackDestructionBarrier
      // object until the LB call is destroyed.
      call_stack_destruction_barrier_->MakeLbCallDestructionClosure(this),
      call_dispatch_controller, is_transparent_retry);
}

void RetryFilter::CallData::CreateCallAttempt(bool is_transparent_retry) {
  call_attempt_ = MakeRefCounted<CallAttempt>(this, is_transparent_retry);
  call_attempt_->StartRetriableBatches();
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
    grpc_metadata_batch* send_initial_metadata =
        batch->payload->send_initial_metadata.send_initial_metadata;
    send_initial_metadata_ = send_initial_metadata->Copy();
    peer_string_ = batch->payload->send_initial_metadata.peer_string;
  }
  // Set up cache for send_message ops.
  if (batch->send_message) {
    SliceBuffer* cache = arena_->New<SliceBuffer>(std::move(
        *absl::exchange(batch->payload->send_message.send_message, nullptr)));
    send_messages_.push_back({cache, batch->payload->send_message.flags});
  }
  // Save metadata batch for send_trailing_metadata ops.
  if (batch->send_trailing_metadata) {
    seen_send_trailing_metadata_ = true;
    grpc_metadata_batch* send_trailing_metadata =
        batch->payload->send_trailing_metadata.send_trailing_metadata;
    send_trailing_metadata_ = send_trailing_metadata->Copy();
  }
}

void RetryFilter::CallData::FreeCachedSendInitialMetadata() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: destroying send_initial_metadata",
            chand_, this);
  }
  send_initial_metadata_.Clear();
}

void RetryFilter::CallData::FreeCachedSendMessage(size_t idx) {
  if (send_messages_[idx].slices != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: destroying send_messages[%" PRIuPTR "]",
              chand_, this, idx);
    }
    Destruct(absl::exchange(send_messages_[idx].slices, nullptr));
  }
}

void RetryFilter::CallData::FreeCachedSendTrailingMetadata() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: destroying send_trailing_metadata",
            chand_, this);
  }
  send_trailing_metadata_.Clear();
}

void RetryFilter::CallData::FreeAllCachedSendOpData() {
  if (seen_send_initial_metadata_) {
    FreeCachedSendInitialMetadata();
  }
  for (size_t i = 0; i < send_messages_.size(); ++i) {
    FreeCachedSendMessage(i);
  }
  if (seen_send_trailing_metadata_) {
    FreeCachedSendTrailingMetadata();
  }
}

//
// pending_batches management
//

size_t RetryFilter::CallData::GetBatchIndex(
    grpc_transport_stream_op_batch* batch) {
  if (batch->send_initial_metadata) return 0;
  if (batch->send_message) return 1;
  if (batch->send_trailing_metadata) return 2;
  if (batch->recv_initial_metadata) return 3;
  if (batch->recv_message) return 4;
  if (batch->recv_trailing_metadata) return 5;
  GPR_UNREACHABLE_CODE(return (size_t)-1);
}

// This is called via the call combiner, so access to calld is synchronized.
RetryFilter::CallData::PendingBatch* RetryFilter::CallData::PendingBatchesAdd(
    grpc_transport_stream_op_batch* batch) {
  const size_t idx = GetBatchIndex(batch);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: adding pending batch at index %" PRIuPTR,
            chand_, this, idx);
  }
  PendingBatch* pending = &pending_batches_[idx];
  GPR_ASSERT(pending->batch == nullptr);
  pending->batch = batch;
  pending->send_ops_cached = false;
  // Update state in calld about pending batches.
  // Also check if the batch takes us over the retry buffer limit.
  // Note: We don't check the size of trailing metadata here, because
  // gRPC clients do not send trailing metadata.
  if (batch->send_initial_metadata) {
    pending_send_initial_metadata_ = true;
    bytes_buffered_for_retry_ += batch->payload->send_initial_metadata
                                     .send_initial_metadata->TransportSize();
  }
  if (batch->send_message) {
    pending_send_message_ = true;
    bytes_buffered_for_retry_ +=
        batch->payload->send_message.send_message->Length();
  }
  if (batch->send_trailing_metadata) {
    pending_send_trailing_metadata_ = true;
  }
  // TODO(roth): When we implement hedging, if there are currently attempts
  // in flight, we will need to pick the one on which the max number of send
  // ops have already been sent, and we commit to that attempt.
  if (GPR_UNLIKELY(bytes_buffered_for_retry_ >
                   chand_->per_rpc_retry_buffer_size_)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: exceeded retry buffer size, committing",
              chand_, this);
    }
    RetryCommit(call_attempt_.get());
  }
  return pending;
}

void RetryFilter::CallData::PendingBatchClear(PendingBatch* pending) {
  if (pending->batch->send_initial_metadata) {
    pending_send_initial_metadata_ = false;
  }
  if (pending->batch->send_message) {
    pending_send_message_ = false;
  }
  if (pending->batch->send_trailing_metadata) {
    pending_send_trailing_metadata_ = false;
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
void RetryFilter::CallData::FailPendingBatchInCallCombiner(
    void* arg, grpc_error_handle error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  CallData* call = static_cast<CallData*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_transport_stream_op_batch_finish_with_failure(
      batch, GRPC_ERROR_REF(error), call->call_combiner_);
}

// This is called via the call combiner, so access to calld is synchronized.
void RetryFilter::CallData::PendingBatchesFail(grpc_error_handle error) {
  GPR_ASSERT(!GRPC_ERROR_IS_NONE(error));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i].batch != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: failing %" PRIuPTR " pending batches: %s",
            chand_, this, num_batches, grpc_error_std_string(error).c_str());
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
  closures.RunClosuresWithoutYielding(call_combiner_);
  GRPC_ERROR_UNREF(error);
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

void RetryFilter::CallData::RetryCommit(CallAttempt* call_attempt) {
  if (retry_committed_) return;
  retry_committed_ = true;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: committing retries", chand_, this);
  }
  if (call_attempt != nullptr) {
    // If the call attempt's LB call has been committed, inform the call
    // dispatch controller that the call has been committed.
    // Note: If call_attempt is null, this is happening before the first
    // retry attempt is started, in which case we'll just pass the real
    // call dispatch controller down into the LB call, and it won't be
    // our problem anymore.
    if (call_attempt->lb_call_committed()) {
      auto* service_config_call_data =
          static_cast<ClientChannelServiceConfigCallData*>(
              call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
      service_config_call_data->call_dispatch_controller()->Commit();
    }
    // Free cached send ops.
    call_attempt->FreeCachedSendOpDataAfterCommit();
  }
}

void RetryFilter::CallData::StartRetryTimer(
    absl::optional<Duration> server_pushback) {
  // Reset call attempt.
  call_attempt_.reset(DEBUG_LOCATION, "StartRetryTimer");
  // Compute backoff delay.
  Timestamp next_attempt_time;
  if (server_pushback.has_value()) {
    GPR_ASSERT(*server_pushback >= Duration::Zero());
    next_attempt_time = ExecCtx::Get()->Now() + *server_pushback;
    retry_backoff_.Reset();
  } else {
    next_attempt_time = retry_backoff_.NextAttemptTime();
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: retrying failed call in %" PRId64 " ms", chand_,
            this, (next_attempt_time - ExecCtx::Get()->Now()).millis());
  }
  // Schedule retry after computed delay.
  GRPC_CLOSURE_INIT(&retry_closure_, OnRetryTimer, this, nullptr);
  GRPC_CALL_STACK_REF(owning_call_, "OnRetryTimer");
  retry_timer_pending_ = true;
  grpc_timer_init(&retry_timer_, next_attempt_time, &retry_closure_);
}

void RetryFilter::CallData::OnRetryTimer(void* arg, grpc_error_handle error) {
  auto* calld = static_cast<CallData*>(arg);
  GRPC_CLOSURE_INIT(&calld->retry_closure_, OnRetryTimerLocked, calld, nullptr);
  GRPC_CALL_COMBINER_START(calld->call_combiner_, &calld->retry_closure_,
                           GRPC_ERROR_REF(error), "retry timer fired");
}

void RetryFilter::CallData::OnRetryTimerLocked(void* arg,
                                               grpc_error_handle error) {
  auto* calld = static_cast<CallData*>(arg);
  if (GRPC_ERROR_IS_NONE(error) && calld->retry_timer_pending_) {
    calld->retry_timer_pending_ = false;
    calld->CreateCallAttempt(/*is_transparent_retry=*/false);
  } else {
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_, "retry timer cancelled");
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call_, "OnRetryTimer");
}

void RetryFilter::CallData::AddClosureToStartTransparentRetry(
    CallCombinerClosureList* closures) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: scheduling transparent retry", chand_,
            this);
  }
  GRPC_CALL_STACK_REF(owning_call_, "OnRetryTimer");
  GRPC_CLOSURE_INIT(&retry_closure_, StartTransparentRetry, this, nullptr);
  closures->Add(&retry_closure_, GRPC_ERROR_NONE, "start transparent retry");
}

void RetryFilter::CallData::StartTransparentRetry(void* arg,
                                                  grpc_error_handle /*error*/) {
  auto* calld = static_cast<CallData*>(arg);
  if (GRPC_ERROR_IS_NONE(calld->cancelled_from_surface_)) {
    calld->CreateCallAttempt(/*is_transparent_retry=*/true);
  } else {
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                            "call cancelled before transparent retry");
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call_, "OnRetryTimer");
}

}  // namespace

const grpc_channel_filter kRetryFilterVtable = {
    RetryFilter::CallData::StartTransportStreamOpBatch,
    nullptr,
    RetryFilter::StartTransportOp,
    sizeof(RetryFilter::CallData),
    RetryFilter::CallData::Init,
    RetryFilter::CallData::SetPollent,
    RetryFilter::CallData::Destroy,
    sizeof(RetryFilter),
    RetryFilter::Init,
    grpc_channel_stack_no_post_init,
    RetryFilter::Destroy,
    RetryFilter::GetChannelInfo,
    "retry_filter",
};

}  // namespace grpc_core
