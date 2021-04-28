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
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* /*final_info*/,
                      grpc_closure* then_schedule_closure);
  static void StartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);
  static void SetPollent(grpc_call_element* elem, grpc_polling_entity* pollent);

 private:
  class Canceller;
  class CallStackDestructionBarrier;

  // Pending batches stored in call data.
  struct PendingBatch {
    // The pending batch.  If nullptr, this slot is empty.
    grpc_transport_stream_op_batch* batch = nullptr;
    // Indicates whether payload for send ops has been cached in CallData.
    bool send_ops_cached = false;
  };

  // State associated with each call attempt.
  // Allocated on the arena.
  class CallAttempt
      : public RefCounted<CallAttempt, PolymorphicRefCount, kUnrefCallDtor> {
   public:
    explicit CallAttempt(CallData* calld);

    ClientChannel::LoadBalancedCall* lb_call() const { return lb_call_.get(); }

    // Constructs and starts whatever batches are needed on this call
    // attempt.
    void StartRetriableBatches();

    // Frees cached send ops that have already been completed after
    // committing the call.
    void FreeCachedSendOpDataAfterCommit();

   private:
    // State used for starting a retryable batch on the call attempt's LB call.
    // This provides its own grpc_transport_stream_op_batch and other data
    // structures needed to populate the ops in the batch.
    // We allocate one struct on the arena for each attempt at starting a
    // batch on a given LB call.
    class BatchData
        : public RefCounted<CallAttempt, PolymorphicRefCount, kUnrefCallDtor> {
     public:
      BatchData(RefCountedPtr<CallAttempt> call_attempt, int refcount,
                bool set_on_complete);
      ~BatchData() override;

      grpc_transport_stream_op_batch* batch() { return &batch_; }

      // Adds retriable send_initial_metadata op to batch_data.
      void AddRetriableSendInitialMetadataOp();
      // Adds retriable send_message op to batch_data.
      void AddRetriableSendMessageOp();
      // Adds retriable send_trailing_metadata op to batch_data.
      void AddRetriableSendTrailingMetadataOp();
      // Adds retriable recv_initial_metadata op to batch_data.
      void AddRetriableRecvInitialMetadataOp();
      // Adds retriable recv_message op to batch_data.
      void AddRetriableRecvMessageOp();
      // Adds retriable recv_trailing_metadata op to batch_data.
      void AddRetriableRecvTrailingMetadataOp();

     private:
      // Returns true if the call is being retried.
      bool MaybeRetry(grpc_status_code status, grpc_mdelem* server_pushback_md,
                      bool is_lb_drop);

      // Frees cached send ops that were completed by the completed batch in
      // batch_data.  Used when batches are completed after the call is
      // committed.
      void FreeCachedSendOpDataForCompletedBatch();

      // Invokes recv_initial_metadata_ready for a batch.
      static void InvokeRecvInitialMetadataCallback(void* arg,
                                                    grpc_error_handle error);
      // Intercepts recv_initial_metadata_ready callback for retries.
      // Commits the call and returns the initial metadata up the stack.
      static void RecvInitialMetadataReady(void* arg, grpc_error_handle error);

      // Invokes recv_message_ready for a batch.
      static void InvokeRecvMessageCallback(void* arg, grpc_error_handle error);
      // Intercepts recv_message_ready callback for retries.
      // Commits the call and returns the message up the stack.
      static void RecvMessageReady(void* arg, grpc_error_handle error);

      // Adds recv_trailing_metadata_ready closure to closures.
      void AddClosureForRecvTrailingMetadataReady(
          grpc_error_handle error, CallCombinerClosureList* closures);
      // Adds any necessary closures for deferred recv_initial_metadata and
      // recv_message callbacks to closures.
      void AddClosuresForDeferredRecvCallbacks(
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

      RefCountedPtr<CallAttempt> call_attempt_;
      // The batch to use in the LB call.
      // Its payload field points to CallAttempt::batch_payload_.
      grpc_transport_stream_op_batch batch_;
      // For intercepting on_complete.
      grpc_closure on_complete_;
    };

    // Creates a BatchData object on the call's arena with the
    // specified refcount.  If set_on_complete is true, the batch's
    // on_complete callback will be set to point to on_complete();
    // otherwise, the batch's on_complete callback will be null.
    BatchData* CreateBatch(int refcount, bool set_on_complete) {
      return calld_->arena_->New<BatchData>(Ref(), refcount, set_on_complete);
    }

    // If there are any cached send ops that need to be replayed on this
    // call attempt, creates and returns a new batch to replay those ops.
    // Otherwise, returns nullptr.
    BatchData* MaybeCreateBatchForReplay();

    // Adds batches for pending batches to closures.
    void AddBatchesForPendingBatches(CallCombinerClosureList* closures);

    // Adds whatever batches are needed on this attempt to closures.
    void AddRetriableBatches(CallCombinerClosureList* closures);

    // Returns true if any op in the batch was not yet started on this attempt.
    bool PendingBatchIsUnstarted(PendingBatch* pending);

    // Helper function used to start a recv_trailing_metadata batch.  This
    // is used in the case where a recv_initial_metadata or recv_message
    // op fails in a way that we know the call is over but when the application
    // has not yet started its own recv_trailing_metadata op.
    void StartInternalRecvTrailingMetadata();

    CallData* calld_;
    RefCountedPtr<ClientChannel::LoadBalancedCall> lb_call_;

    // BatchData.batch.payload points to this.
    grpc_transport_stream_op_batch_payload batch_payload_;
    // For send_initial_metadata.
    // Note that we need to make a copy of the initial metadata for each
    // call attempt instead of just referring to the copy in call_data,
    // because filters in the subchannel stack may modify the metadata,
    // so we need to start in a pristine state for each attempt of the call.
    grpc_linked_mdelem* send_initial_metadata_storage_;
    grpc_metadata_batch send_initial_metadata_;
    // For send_message.
    // TODO(roth): Restructure this to eliminate use of ManualConstructor.
    ManualConstructor<ByteStreamCache::CachingByteStream> send_message_;
    // For send_trailing_metadata.
    grpc_linked_mdelem* send_trailing_metadata_storage_;
    grpc_metadata_batch send_trailing_metadata_;
    // For intercepting recv_initial_metadata.
    grpc_metadata_batch recv_initial_metadata_;
    grpc_closure recv_initial_metadata_ready_;
    bool trailing_metadata_available_ = false;
    // For intercepting recv_message.
    grpc_closure recv_message_ready_;
    OrphanablePtr<ByteStream> recv_message_;
    // For intercepting recv_trailing_metadata.
    grpc_metadata_batch recv_trailing_metadata_;
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
    // State for callback processing.
    BatchData* recv_initial_metadata_ready_deferred_batch_ = nullptr;
    grpc_error_handle recv_initial_metadata_error_ = GRPC_ERROR_NONE;
    BatchData* recv_message_ready_deferred_batch_ = nullptr;
    grpc_error_handle recv_message_error_ = GRPC_ERROR_NONE;
    BatchData* recv_trailing_metadata_internal_batch_ = nullptr;
    // NOTE: Do not move this next to the metadata bitfields above. That would
    //       save space but will also result in a data race because compiler
    //       will generate a 2 byte store which overwrites the meta-data
    //       fields upon setting this field.
    bool retry_dispatched_ : 1;
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

  // Starts a retry after appropriate back-off.
  void DoRetry(grpc_millis server_pushback_ms);
  static void OnRetryTimer(void* arg, grpc_error_handle error);

  RefCountedPtr<ClientChannel::LoadBalancedCall> CreateLoadBalancedCall();

  void CreateCallAttempt();

  // Adds a closure to closures that will execute batch in the call combiner.
  void AddClosureForBatch(grpc_transport_stream_op_batch* batch,
                          CallCombinerClosureList* closures);

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

  RefCountedPtr<CallStackDestructionBarrier> call_stack_destruction_barrier_;

  // TODO(roth): As part of implementing hedging, we will need to maintain a
  // list of all pending attempts, so that we can cancel them all if the call
  // gets cancelled.
  RefCountedPtr<CallAttempt> call_attempt_;

  // LB call used when the call is commited before any CallAttempt is
  // created.
  // TODO(roth): Change CallAttempt logic such that once we've committed
  // and all cached send ops have been replayed, we move the LB call
  // from the CallAttempt here, thus creating a fast path for the
  // remainder of the streaming call.
  RefCountedPtr<ClientChannel::LoadBalancedCall> committed_call_;

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
  bool last_attempt_got_server_pushback_ : 1;
  int num_attempts_completed_ = 0;
  Mutex timer_mu_;
  Canceller* canceller_ ABSL_GUARDED_BY(timer_mu_);
  grpc_timer retry_timer_ ABSL_GUARDED_BY(timer_mu_);
  grpc_closure retry_closure_;

  // The number of batches containing send ops that are currently in-flight
  // on any call attempt.
  // We hold a ref to the call stack while this is non-zero, since replay
  // batches may not complete until after all callbacks have been returned
  // to the surface, and we need to make sure that the call is not destroyed
  // until all of these batches have completed.
  // Note that we actually only need to track replay batches, but it's
  // easier to track all batches with send ops.
  int num_in_flight_call_attempt_send_batches_ = 0;

  // Cached data for retrying send ops.
  // send_initial_metadata
  bool seen_send_initial_metadata_ = false;
  grpc_linked_mdelem* send_initial_metadata_storage_ = nullptr;
  grpc_metadata_batch send_initial_metadata_;
  uint32_t send_initial_metadata_flags_;
  // TODO(roth): As part of implementing hedging, we'll probably need to
  // have the LB call set a value in CallAttempt and then propagate it
  // from CallAttempt to the parent call when we commit.  Otherwise, we
  // may leave this with a value for a peer other than the one we
  // actually commit to.
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
// RetryFilter::CallData::Canceller
//

class RetryFilter::CallData::Canceller {
 public:
  explicit Canceller(CallData* calld) : calld_(calld) {
    GRPC_CALL_STACK_REF(calld_->owning_call_, "RetryCanceller");
    GRPC_CLOSURE_INIT(&closure_, &Cancel, this, nullptr);
    calld_->call_combiner_->SetNotifyOnCancel(&closure_);
  }

 private:
  static void Cancel(void* arg, grpc_error_handle error) {
    auto* self = static_cast<Canceller*>(arg);
    auto* calld = self->calld_;
    {
      MutexLock lock(&calld->timer_mu_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "calld=%p: cancelling retry timer: error=%s self=%p "
                "calld->canceller_=%p",
                calld, grpc_error_std_string(error).c_str(), self,
                calld->canceller_);
      }
      if (calld->canceller_ == self && error != GRPC_ERROR_NONE) {
        calld->canceller_ = nullptr;  // Checked by OnRetryTimer().
        grpc_timer_cancel(&calld->retry_timer_);
        calld->FreeAllCachedSendOpData();
        GRPC_CALL_COMBINER_STOP(calld->call_combiner_, "Canceller");
      }
    }
    GRPC_CALL_STACK_UNREF(calld->owning_call_, "RetryCanceller");
    delete self;
  }

  CallData* calld_;
  grpc_closure closure_;
};

//
// RetryFilter::CallData::CallAttempt
//

RetryFilter::CallData::CallAttempt::CallAttempt(CallData* calld)
    : calld_(calld),
      batch_payload_(calld->call_context_),
      started_send_initial_metadata_(false),
      completed_send_initial_metadata_(false),
      started_send_trailing_metadata_(false),
      completed_send_trailing_metadata_(false),
      started_recv_initial_metadata_(false),
      completed_recv_initial_metadata_(false),
      started_recv_trailing_metadata_(false),
      completed_recv_trailing_metadata_(false),
      retry_dispatched_(false) {
  lb_call_ = calld->CreateLoadBalancedCall();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: attempt=%p: create lb_call=%p",
            calld->chand_, calld, this, lb_call_.get());
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

bool RetryFilter::CallData::CallAttempt::PendingBatchIsUnstarted(
    PendingBatch* pending) {
  // Only look at batches containing send ops, since batches containing
  // only recv ops are always started immediately.
  if (pending->batch == nullptr || pending->batch->on_complete == nullptr) {
    return false;
  }
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

void RetryFilter::CallData::CallAttempt::StartInternalRecvTrailingMetadata() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: call failed but recv_trailing_metadata not "
            "started; starting it internally",
            calld_->chand_, calld_);
  }
  // Create batch_data with 2 refs, since this batch will be unreffed twice:
  // once for the recv_trailing_metadata_ready callback when the batch
  // completes, and again when we actually get a recv_trailing_metadata
  // op from the surface.
  BatchData* batch_data = CreateBatch(2, false /* set_on_complete */);
  batch_data->AddRetriableRecvTrailingMetadataOp();
  recv_trailing_metadata_internal_batch_ = batch_data;
  // Note: This will release the call combiner.
  lb_call_->StartTransportStreamOpBatch(batch_data->batch());
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
              "chand=%p calld=%p: replaying previously completed "
              "send_initial_metadata op",
              calld_->chand_, calld_);
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
              "chand=%p calld=%p: replaying previously completed "
              "send_message op",
              calld_->chand_, calld_);
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
              "chand=%p calld=%p: replaying previously completed "
              "send_trailing_metadata op",
              calld_->chand_, calld_);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data = CreateBatch(1, true /* set_on_complete */);
    }
    replay_batch_data->AddRetriableSendTrailingMetadataOp();
  }
  return replay_batch_data;
}

void RetryFilter::CallData::CallAttempt::AddBatchesForPendingBatches(
    CallCombinerClosureList* closures) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld_->pending_batches_); ++i) {
    PendingBatch* pending = &calld_->pending_batches_[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch == nullptr) continue;
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
    if (batch->send_initial_metadata && started_send_initial_metadata_) {
      continue;
    }
    if (batch->send_message &&
        completed_send_message_count_ < started_send_message_count_) {
      continue;
    }
    // Note that we only start send_trailing_metadata if we have no more
    // send_message ops to start, since we can't send down any more
    // send_message ops after send_trailing_metadata.
    if (batch->send_trailing_metadata &&
        (started_send_message_count_ + batch->send_message <
             calld_->send_messages_.size() ||
         started_send_trailing_metadata_)) {
      continue;
    }
    if (batch->recv_initial_metadata && started_recv_initial_metadata_) {
      continue;
    }
    if (batch->recv_message &&
        completed_recv_message_count_ < started_recv_message_count_) {
      continue;
    }
    if (batch->recv_trailing_metadata && started_recv_trailing_metadata_) {
      // If we previously completed a recv_trailing_metadata op
      // initiated by StartInternalRecvTrailingMetadata(), use the
      // result of that instead of trying to re-start this op.
      if (GPR_UNLIKELY(recv_trailing_metadata_internal_batch_ != nullptr)) {
        // If the batch completed, then trigger the completion callback
        // directly, so that we return the previously returned results to
        // the application.  Otherwise, just unref the internally started
        // batch, since we'll propagate the completion when it completes.
        if (completed_recv_trailing_metadata_) {
          // Batches containing recv_trailing_metadata always succeed.
          closures->Add(
              &recv_trailing_metadata_ready_, GRPC_ERROR_NONE,
              "re-executing recv_trailing_metadata_ready to propagate "
              "internally triggered result");
        } else {
          recv_trailing_metadata_internal_batch_->Unref();
        }
        recv_trailing_metadata_internal_batch_ = nullptr;
      }
      continue;
    }
    // If we're already committed, just send the batch as-is.
    if (calld_->retry_committed_) {
      calld_->AddClosureForBatch(batch, closures);
      calld_->PendingBatchClear(pending);
      continue;
    }
    // Create batch with the right number of callbacks.
    const bool has_send_ops = batch->send_initial_metadata ||
                              batch->send_message ||
                              batch->send_trailing_metadata;
    const int num_callbacks = has_send_ops + batch->recv_initial_metadata +
                              batch->recv_message +
                              batch->recv_trailing_metadata;
    CallAttempt::BatchData* batch_data =
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
      // recv_flags is only used on the server side.
      GPR_ASSERT(batch->payload->recv_initial_metadata.recv_flags == nullptr);
      batch_data->AddRetriableRecvInitialMetadataOp();
    }
    // recv_message.
    if (batch->recv_message) {
      batch_data->AddRetriableRecvMessageOp();
    }
    // recv_trailing_metadata.
    if (batch->recv_trailing_metadata) {
      batch_data->AddRetriableRecvTrailingMetadataOp();
    }
    calld_->AddClosureForBatch(batch_data->batch(), closures);
    // Track number of in-flight send batches.
    // If this is the first one, take a ref to the call stack.
    if (batch->send_initial_metadata || batch->send_message ||
        batch->send_trailing_metadata) {
      if (calld_->num_in_flight_call_attempt_send_batches_ == 0) {
        GRPC_CALL_STACK_REF(calld_->owning_call_, "retriable_send_batches");
      }
      ++calld_->num_in_flight_call_attempt_send_batches_;
    }
  }
}

void RetryFilter::CallData::CallAttempt::AddRetriableBatches(
    CallCombinerClosureList* closures) {
  // Replay previously-returned send_* ops if needed.
  BatchData* replay_batch_data = MaybeCreateBatchForReplay();
  if (replay_batch_data != nullptr) {
    calld_->AddClosureForBatch(replay_batch_data->batch(), closures);
    // Track number of pending send batches.
    // If this is the first one, take a ref to the call stack.
    if (calld_->num_in_flight_call_attempt_send_batches_ == 0) {
      GRPC_CALL_STACK_REF(calld_->owning_call_, "retriable_send_batches");
    }
    ++calld_->num_in_flight_call_attempt_send_batches_;
  }
  // Now add pending batches.
  AddBatchesForPendingBatches(closures);
}

void RetryFilter::CallData::CallAttempt::StartRetriableBatches() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: constructing retriable batches",
            calld_->chand_, calld_);
  }
  // Construct list of closures to execute, one for each pending batch.
  CallCombinerClosureList closures;
  AddRetriableBatches(&closures);
  // Note: This will yield the call combiner.
  // Start batches on LB call.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " retriable batches on lb_call=%p",
            calld_->chand_, calld_, closures.size(), lb_call());
  }
  closures.RunClosures(calld_->call_combiner_);
}

//
// RetryFilter::CallData::CallAttempt::BatchData
//

RetryFilter::CallData::CallAttempt::BatchData::BatchData(
    RefCountedPtr<CallAttempt> attempt, int refcount, bool set_on_complete)
    : RefCounted(nullptr, refcount), call_attempt_(std::move(attempt)) {
  // TODO(roth): Consider holding this ref on the call stack in
  // CallAttempt instead of here in BatchData.  This would eliminate the
  // need for CallData::num_in_flight_call_attempt_send_batches_.
  // But it would require having a way to unref CallAttempt when it is
  // no longer needed (i.e., when the call is committed and all cached
  // send ops have been replayed and the LB call is moved into
  // CallData::committed_call_).
  GRPC_CALL_STACK_REF(call_attempt_->calld_->owning_call_, "CallAttempt");
  batch_.payload = &call_attempt_->batch_payload_;
  if (set_on_complete) {
    GRPC_CLOSURE_INIT(&on_complete_, OnComplete, this,
                      grpc_schedule_on_exec_ctx);
    batch_.on_complete = &on_complete_;
  }
}

RetryFilter::CallData::CallAttempt::BatchData::~BatchData() {
  if (batch_.send_initial_metadata) {
    grpc_metadata_batch_destroy(&call_attempt_->send_initial_metadata_);
  }
  if (batch_.send_trailing_metadata) {
    grpc_metadata_batch_destroy(&call_attempt_->send_trailing_metadata_);
  }
  if (batch_.recv_initial_metadata) {
    grpc_metadata_batch_destroy(&call_attempt_->recv_initial_metadata_);
  }
  if (batch_.recv_trailing_metadata) {
    grpc_metadata_batch_destroy(&call_attempt_->recv_trailing_metadata_);
  }
  GRPC_CALL_STACK_UNREF(call_attempt_->calld_->owning_call_, "CallAttempt");
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

bool RetryFilter::CallData::CallAttempt::BatchData::MaybeRetry(
    grpc_status_code status, grpc_mdelem* server_pushback_md, bool is_lb_drop) {
  auto* calld = call_attempt_->calld_;
  // LB drops always inhibit retries.
  if (is_lb_drop) return false;
  // Get retry policy.
  if (calld->retry_policy_ == nullptr) return false;
  // If we've already dispatched a retry from this call, return true.
  // This catches the case where the batch has multiple callbacks
  // (i.e., it includes either recv_message or recv_initial_metadata).
  if (call_attempt_->retry_dispatched_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retry already dispatched",
              calld->chand_, calld);
    }
    return true;
  }
  // Check status.
  if (GPR_LIKELY(status == GRPC_STATUS_OK)) {
    if (calld->retry_throttle_data_ != nullptr) {
      calld->retry_throttle_data_->RecordSuccess();
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: call succeeded", calld->chand_,
              calld);
    }
    return false;
  }
  // Status is not OK.  Check whether the status is retryable.
  if (!calld->retry_policy_->retryable_status_codes().Contains(status)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: status %s not configured as retryable",
              calld->chand_, calld, grpc_status_code_to_string(status));
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
  if (calld->retry_throttle_data_ != nullptr &&
      !calld->retry_throttle_data_->RecordFailure()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retries throttled", calld->chand_,
              calld);
    }
    return false;
  }
  // Check whether the call is committed.
  if (calld->retry_committed_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retries already committed",
              calld->chand_, calld);
    }
    return false;
  }
  // Check whether we have retries remaining.
  ++calld->num_attempts_completed_;
  if (calld->num_attempts_completed_ >= calld->retry_policy_->max_attempts()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: exceeded %d retry attempts",
              calld->chand_, calld, calld->retry_policy_->max_attempts());
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
                calld->chand_, calld);
      }
      return false;
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: server push-back: retry in %u ms",
                calld->chand_, calld, ms);
      }
      server_pushback_ms = static_cast<grpc_millis>(ms);
    }
  }
  // Do retry.
  call_attempt_->retry_dispatched_ = true;
  calld->DoRetry(server_pushback_ms);
  return true;
}

//
// recv_initial_metadata callback handling
//

void RetryFilter::CallData::CallAttempt::BatchData::
    InvokeRecvInitialMetadataCallback(void* arg, grpc_error_handle error) {
  auto* batch_data = static_cast<CallAttempt::BatchData*>(arg);
  auto* call_attempt = batch_data->call_attempt_.get();
  // Find pending batch.
  PendingBatch* pending = call_attempt->calld_->PendingBatchFind(
      "invoking recv_initial_metadata_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_initial_metadata &&
               batch->payload->recv_initial_metadata
                       .recv_initial_metadata_ready != nullptr;
      });
  GPR_ASSERT(pending != nullptr);
  // Return metadata.
  grpc_metadata_batch_move(
      &call_attempt->recv_initial_metadata_,
      pending->batch->payload->recv_initial_metadata.recv_initial_metadata);
  // Update bookkeeping.
  // Note: Need to do this before invoking the callback, since invoking
  // the callback will result in yielding the call combiner.
  grpc_closure* recv_initial_metadata_ready =
      pending->batch->payload->recv_initial_metadata
          .recv_initial_metadata_ready;
  pending->batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
      nullptr;
  call_attempt->calld_->MaybeClearPendingBatch(pending);
  batch_data->Unref();
  // Invoke callback.
  Closure::Run(DEBUG_LOCATION, recv_initial_metadata_ready,
               GRPC_ERROR_REF(error));
}

void RetryFilter::CallData::CallAttempt::BatchData::RecvInitialMetadataReady(
    void* arg, grpc_error_handle error) {
  CallAttempt::BatchData* batch_data =
      static_cast<CallAttempt::BatchData*>(arg);
  CallAttempt* call_attempt = batch_data->call_attempt_.get();
  CallData* calld = call_attempt->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: got recv_initial_metadata_ready, error=%s",
            calld->chand_, calld, grpc_error_std_string(error).c_str());
  }
  call_attempt->completed_recv_initial_metadata_ = true;
  // If a retry was already dispatched, then we're not going to use the
  // result of this recv_initial_metadata op, so do nothing.
  if (call_attempt->retry_dispatched_) {
    GRPC_CALL_COMBINER_STOP(
        calld->call_combiner_,
        "recv_initial_metadata_ready after retry dispatched");
    return;
  }
  if (!calld->retry_committed_) {
    // If we got an error or a Trailers-Only response and have not yet gotten
    // the recv_trailing_metadata_ready callback, then defer propagating this
    // callback back to the surface.  We can evaluate whether to retry when
    // recv_trailing_metadata comes back.
    if (GPR_UNLIKELY((call_attempt->trailing_metadata_available_ ||
                      error != GRPC_ERROR_NONE) &&
                     !call_attempt->completed_recv_trailing_metadata_)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: deferring recv_initial_metadata_ready "
                "(Trailers-Only)",
                calld->chand_, calld);
      }
      call_attempt->recv_initial_metadata_ready_deferred_batch_ = batch_data;
      call_attempt->recv_initial_metadata_error_ = GRPC_ERROR_REF(error);
      if (!call_attempt->started_recv_trailing_metadata_) {
        // recv_trailing_metadata not yet started by application; start it
        // ourselves to get status.
        call_attempt->StartInternalRecvTrailingMetadata();
      } else {
        GRPC_CALL_COMBINER_STOP(
            calld->call_combiner_,
            "recv_initial_metadata_ready trailers-only or error");
      }
      return;
    }
    // Received valid initial metadata, so commit the call.
    calld->RetryCommit(call_attempt);
  }
  // Invoke the callback to return the result to the surface.
  // Manually invoking a callback function; it does not take ownership of error.
  InvokeRecvInitialMetadataCallback(batch_data, error);
}

//
// recv_message callback handling
//

void RetryFilter::CallData::CallAttempt::BatchData::InvokeRecvMessageCallback(
    void* arg, grpc_error_handle error) {
  CallAttempt::BatchData* batch_data =
      static_cast<CallAttempt::BatchData*>(arg);
  CallAttempt* call_attempt = batch_data->call_attempt_.get();
  CallData* calld = call_attempt->calld_;
  // Find pending op.
  PendingBatch* pending = calld->PendingBatchFind(
      "invoking recv_message_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_message &&
               batch->payload->recv_message.recv_message_ready != nullptr;
      });
  GPR_ASSERT(pending != nullptr);
  // Return payload.
  *pending->batch->payload->recv_message.recv_message =
      std::move(call_attempt->recv_message_);
  // Update bookkeeping.
  // Note: Need to do this before invoking the callback, since invoking
  // the callback will result in yielding the call combiner.
  grpc_closure* recv_message_ready =
      pending->batch->payload->recv_message.recv_message_ready;
  pending->batch->payload->recv_message.recv_message_ready = nullptr;
  calld->MaybeClearPendingBatch(pending);
  batch_data->Unref();
  // Invoke callback.
  Closure::Run(DEBUG_LOCATION, recv_message_ready, GRPC_ERROR_REF(error));
}

void RetryFilter::CallData::CallAttempt::BatchData::RecvMessageReady(
    void* arg, grpc_error_handle error) {
  CallAttempt::BatchData* batch_data =
      static_cast<CallAttempt::BatchData*>(arg);
  CallAttempt* call_attempt = batch_data->call_attempt_.get();
  CallData* calld = call_attempt->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: got recv_message_ready, error=%s",
            calld->chand_, calld, grpc_error_std_string(error).c_str());
  }
  ++call_attempt->completed_recv_message_count_;
  // If a retry was already dispatched, then we're not going to use the
  // result of this recv_message op, so do nothing.
  if (call_attempt->retry_dispatched_) {
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                            "recv_message_ready after retry dispatched");
    return;
  }
  if (!calld->retry_committed_) {
    // If we got an error or the payload was nullptr and we have not yet gotten
    // the recv_trailing_metadata_ready callback, then defer propagating this
    // callback back to the surface.  We can evaluate whether to retry when
    // recv_trailing_metadata comes back.
    if (GPR_UNLIKELY((call_attempt->recv_message_ == nullptr ||
                      error != GRPC_ERROR_NONE) &&
                     !call_attempt->completed_recv_trailing_metadata_)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: deferring recv_message_ready (nullptr "
                "message and recv_trailing_metadata pending)",
                calld->chand_, calld);
      }
      call_attempt->recv_message_ready_deferred_batch_ = batch_data;
      call_attempt->recv_message_error_ = GRPC_ERROR_REF(error);
      if (!call_attempt->started_recv_trailing_metadata_) {
        // recv_trailing_metadata not yet started by application; start it
        // ourselves to get status.
        call_attempt->StartInternalRecvTrailingMetadata();
      } else {
        GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                                "recv_message_ready null");
      }
      return;
    }
    // Received a valid message, so commit the call.
    calld->RetryCommit(call_attempt);
  }
  // Invoke the callback to return the result to the surface.
  // Manually invoking a callback function; it does not take ownership of error.
  InvokeRecvMessageCallback(batch_data, error);
}

//
// recv_trailing_metadata handling
//

namespace {

// Sets *status, *server_pushback_md, and *is_lb_drop based on md_batch
// and error.
void GetCallStatus(grpc_millis deadline, grpc_metadata_batch* md_batch,
                   grpc_error_handle error, grpc_status_code* status,
                   grpc_mdelem** server_pushback_md, bool* is_lb_drop) {
  if (error != GRPC_ERROR_NONE) {
    grpc_error_get_status(error, deadline, status, nullptr, nullptr, nullptr);
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

}  // namespace

void RetryFilter::CallData::CallAttempt::BatchData::
    AddClosureForRecvTrailingMetadataReady(grpc_error_handle error,
                                           CallCombinerClosureList* closures) {
  auto* calld = call_attempt_->calld_;
  // Find pending batch.
  PendingBatch* pending = calld->PendingBatchFind(
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
  grpc_metadata_batch_move(
      &call_attempt_->recv_trailing_metadata_,
      pending->batch->payload->recv_trailing_metadata.recv_trailing_metadata);
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
    AddClosuresForDeferredRecvCallbacks(CallCombinerClosureList* closures) {
  if (batch_.recv_trailing_metadata) {
    // Add closure for deferred recv_initial_metadata_ready.
    if (GPR_UNLIKELY(
            call_attempt_->recv_initial_metadata_ready_deferred_batch_ !=
            nullptr)) {
      GRPC_CLOSURE_INIT(
          &call_attempt_->recv_initial_metadata_ready_,
          InvokeRecvInitialMetadataCallback,
          call_attempt_->recv_initial_metadata_ready_deferred_batch_,
          grpc_schedule_on_exec_ctx);
      closures->Add(&call_attempt_->recv_initial_metadata_ready_,
                    call_attempt_->recv_initial_metadata_error_,
                    "resuming recv_initial_metadata_ready");
      call_attempt_->recv_initial_metadata_ready_deferred_batch_ = nullptr;
    }
    // Add closure for deferred recv_message_ready.
    if (GPR_UNLIKELY(call_attempt_->recv_message_ready_deferred_batch_ !=
                     nullptr)) {
      GRPC_CLOSURE_INIT(&call_attempt_->recv_message_ready_,
                        InvokeRecvMessageCallback,
                        call_attempt_->recv_message_ready_deferred_batch_,
                        grpc_schedule_on_exec_ctx);
      closures->Add(&call_attempt_->recv_message_ready_,
                    call_attempt_->recv_message_error_,
                    "resuming recv_message_ready");
      call_attempt_->recv_message_ready_deferred_batch_ = nullptr;
    }
  }
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddClosuresToFailUnstartedPendingBatches(
        grpc_error_handle error, CallCombinerClosureList* closures) {
  auto* calld = call_attempt_->calld_;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches_); ++i) {
    PendingBatch* pending = &calld->pending_batches_[i];
    if (call_attempt_->PendingBatchIsUnstarted(pending)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: failing unstarted pending batch at "
                "index %" PRIuPTR,
                calld->chand_, calld, i);
      }
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
  AddClosureForRecvTrailingMetadataReady(GRPC_ERROR_REF(error), &closures);
  // If there are deferred recv_initial_metadata_ready or recv_message_ready
  // callbacks, add them to closures.
  AddClosuresForDeferredRecvCallbacks(&closures);
  // Add closures to fail any pending batches that have not yet been started.
  AddClosuresToFailUnstartedPendingBatches(GRPC_ERROR_REF(error), &closures);
  // Schedule all of the closures identified above.
  // Note: This will release the call combiner.
  closures.RunClosures(call_attempt_->calld_->call_combiner_);
  // Don't need batch_data anymore.
  Unref();
  GRPC_ERROR_UNREF(error);
}

void RetryFilter::CallData::CallAttempt::BatchData::RecvTrailingMetadataReady(
    void* arg, grpc_error_handle error) {
  CallAttempt::BatchData* batch_data =
      static_cast<CallAttempt::BatchData*>(arg);
  CallAttempt* call_attempt = batch_data->call_attempt_.get();
  CallData* calld = call_attempt->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: got recv_trailing_metadata_ready, error=%s",
            calld->chand_, calld, grpc_error_std_string(error).c_str());
  }
  call_attempt->completed_recv_trailing_metadata_ = true;
  // Get the call's status and check for server pushback metadata.
  grpc_status_code status = GRPC_STATUS_OK;
  grpc_mdelem* server_pushback_md = nullptr;
  grpc_metadata_batch* md_batch =
      batch_data->batch_.payload->recv_trailing_metadata.recv_trailing_metadata;
  bool is_lb_drop = false;
  GetCallStatus(calld->deadline_, md_batch, GRPC_ERROR_REF(error), &status,
                &server_pushback_md, &is_lb_drop);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(
        GPR_INFO, "chand=%p calld=%p: call finished, status=%s is_lb_drop=%d",
        calld->chand_, calld, grpc_status_code_to_string(status), is_lb_drop);
  }
  // Check if we should retry.
  if (batch_data->MaybeRetry(status, server_pushback_md, is_lb_drop)) {
    // Unref batch_data for deferred recv_initial_metadata_ready or
    // recv_message_ready callbacks, if any.
    if (call_attempt->recv_initial_metadata_ready_deferred_batch_ != nullptr) {
      GRPC_ERROR_UNREF(call_attempt->recv_initial_metadata_error_);
      batch_data->Unref();
    }
    if (call_attempt->recv_message_ready_deferred_batch_ != nullptr) {
      GRPC_ERROR_UNREF(call_attempt->recv_message_error_);
      batch_data->Unref();
    }
    batch_data->Unref();
    return;
  }
  // Not retrying, so commit the call.
  calld->RetryCommit(call_attempt);
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
  // Add closure.
  closures->Add(pending->batch->on_complete, error,
                "on_complete for pending batch");
  pending->batch->on_complete = nullptr;
  calld->MaybeClearPendingBatch(pending);
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddClosuresForReplayOrPendingSendOps(CallCombinerClosureList* closures) {
  auto* calld = call_attempt_->calld_;
  // We don't check send_initial_metadata here, because that op will always
  // be started as soon as it is received from the surface, so it will
  // never need to be started at this point.
  bool have_pending_send_message_ops =
      call_attempt_->started_send_message_count_ < calld->send_messages_.size();
  bool have_pending_send_trailing_metadata_op =
      calld->seen_send_trailing_metadata_ &&
      !call_attempt_->started_send_trailing_metadata_;
  if (!have_pending_send_message_ops &&
      !have_pending_send_trailing_metadata_op) {
    for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches_); ++i) {
      PendingBatch* pending = &calld->pending_batches_[i];
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
              calld->chand_, calld);
    }
    call_attempt_->AddRetriableBatches(closures);
  }
}

void RetryFilter::CallData::CallAttempt::BatchData::OnComplete(
    void* arg, grpc_error_handle error) {
  CallAttempt::BatchData* batch_data =
      static_cast<CallAttempt::BatchData*>(arg);
  CallAttempt* call_attempt = batch_data->call_attempt_.get();
  CallData* calld = call_attempt->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: got on_complete, error=%s, batch=%s",
            calld->chand_, calld, grpc_error_std_string(error).c_str(),
            grpc_transport_stream_op_batch_string(&batch_data->batch_).c_str());
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
  // If a retry was already dispatched, that means we saw
  // recv_trailing_metadata before this, so we do nothing here.
  // Otherwise, invoke the callback to return the result to the surface.
  if (!call_attempt->retry_dispatched_) {
    // Add closure for the completed pending batch, if any.
    batch_data->AddClosuresForCompletedPendingBatch(GRPC_ERROR_REF(error),
                                                    &closures);
    // If needed, add a callback to start any replay or pending send ops on
    // the LB call.
    if (!call_attempt->completed_recv_trailing_metadata_) {
      batch_data->AddClosuresForReplayOrPendingSendOps(&closures);
    }
  }
  // Track number of in-flight send batches and determine if this was the
  // last one.
  --calld->num_in_flight_call_attempt_send_batches_;
  const bool last_send_batch_complete =
      calld->num_in_flight_call_attempt_send_batches_ == 0;
  // Don't need batch_data anymore.
  batch_data->Unref();
  // Schedule all of the closures identified above.
  // Note: This yields the call combiner.
  closures.RunClosures(calld->call_combiner_);
  // If this was the last in-flight send batch, unref the call stack.
  if (last_send_batch_complete) {
    GRPC_CALL_STACK_UNREF(calld->owning_call_, "retriable_send_batches");
  }
}

//
// retriable batch construction
//

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableSendInitialMetadataOp() {
  auto* calld = call_attempt_->calld_;
  // Maps the number of retries to the corresponding metadata value slice.
  const grpc_slice* retry_count_strings[] = {&GRPC_MDSTR_1, &GRPC_MDSTR_2,
                                             &GRPC_MDSTR_3, &GRPC_MDSTR_4};
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  //
  // If we've already completed one or more attempts, add the
  // grpc-retry-attempts header.
  call_attempt_->send_initial_metadata_storage_ =
      static_cast<grpc_linked_mdelem*>(
          calld->arena_->Alloc(sizeof(grpc_linked_mdelem) *
                               (calld->send_initial_metadata_.list.count +
                                (calld->num_attempts_completed_ > 0))));
  grpc_metadata_batch_copy(&calld->send_initial_metadata_,
                           &call_attempt_->send_initial_metadata_,
                           call_attempt_->send_initial_metadata_storage_);
  if (GPR_UNLIKELY(call_attempt_->send_initial_metadata_.idx.named
                       .grpc_previous_rpc_attempts != nullptr)) {
    grpc_metadata_batch_remove(&call_attempt_->send_initial_metadata_,
                               GRPC_BATCH_GRPC_PREVIOUS_RPC_ATTEMPTS);
  }
  if (GPR_UNLIKELY(calld->num_attempts_completed_ > 0)) {
    grpc_mdelem retry_md = grpc_mdelem_create(
        GRPC_MDSTR_GRPC_PREVIOUS_RPC_ATTEMPTS,
        *retry_count_strings[calld->num_attempts_completed_ - 1], nullptr);
    grpc_error_handle error = grpc_metadata_batch_add_tail(
        &call_attempt_->send_initial_metadata_,
        &call_attempt_->send_initial_metadata_storage_
             [calld->send_initial_metadata_.list.count],
        retry_md, GRPC_BATCH_GRPC_PREVIOUS_RPC_ATTEMPTS);
    if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
      gpr_log(GPR_ERROR, "error adding retry metadata: %s",
              grpc_error_std_string(error).c_str());
      GPR_ASSERT(false);
    }
  }
  call_attempt_->started_send_initial_metadata_ = true;
  batch_.send_initial_metadata = true;
  batch_.payload->send_initial_metadata.send_initial_metadata =
      &call_attempt_->send_initial_metadata_;
  batch_.payload->send_initial_metadata.send_initial_metadata_flags =
      calld->send_initial_metadata_flags_;
  batch_.payload->send_initial_metadata.peer_string = calld->peer_string_;
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableSendMessageOp() {
  auto* calld = call_attempt_->calld_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting calld->send_messages[%" PRIuPTR "]",
            calld->chand_, calld, call_attempt_->started_send_message_count_);
  }
  ByteStreamCache* cache =
      calld->send_messages_[call_attempt_->started_send_message_count_];
  ++call_attempt_->started_send_message_count_;
  call_attempt_->send_message_.Init(cache);
  batch_.send_message = true;
  batch_.payload->send_message.send_message.reset(
      call_attempt_->send_message_.get());
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableSendTrailingMetadataOp() {
  auto* calld = call_attempt_->calld_;
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  call_attempt_->send_trailing_metadata_storage_ =
      static_cast<grpc_linked_mdelem*>(
          calld->arena_->Alloc(sizeof(grpc_linked_mdelem) *
                               calld->send_trailing_metadata_.list.count));
  grpc_metadata_batch_copy(&calld->send_trailing_metadata_,
                           &call_attempt_->send_trailing_metadata_,
                           call_attempt_->send_trailing_metadata_storage_);
  call_attempt_->started_send_trailing_metadata_ = true;
  batch_.send_trailing_metadata = true;
  batch_.payload->send_trailing_metadata.send_trailing_metadata =
      &call_attempt_->send_trailing_metadata_;
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableRecvInitialMetadataOp() {
  call_attempt_->started_recv_initial_metadata_ = true;
  batch_.recv_initial_metadata = true;
  grpc_metadata_batch_init(&call_attempt_->recv_initial_metadata_);
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
  GRPC_CLOSURE_INIT(&call_attempt_->recv_message_ready_, RecvMessageReady, this,
                    grpc_schedule_on_exec_ctx);
  batch_.payload->recv_message.recv_message_ready =
      &call_attempt_->recv_message_ready_;
}

void RetryFilter::CallData::CallAttempt::BatchData::
    AddRetriableRecvTrailingMetadataOp() {
  call_attempt_->started_recv_trailing_metadata_ = true;
  batch_.recv_trailing_metadata = true;
  grpc_metadata_batch_init(&call_attempt_->recv_trailing_metadata_);
  batch_.payload->recv_trailing_metadata.recv_trailing_metadata =
      &call_attempt_->recv_trailing_metadata_;
  batch_.payload->recv_trailing_metadata.collect_stats =
      &call_attempt_->collect_stats_;
  GRPC_CLOSURE_INIT(&call_attempt_->recv_trailing_metadata_ready_,
                    RecvTrailingMetadataReady, this, grpc_schedule_on_exec_ctx);
  batch_.payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      &call_attempt_->recv_trailing_metadata_ready_;
}

//
// CallData vtable functions
//

grpc_error_handle RetryFilter::CallData::Init(
    grpc_call_element* elem, const grpc_call_element_args* args) {
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
      call_stack_destruction_barrier_(
          arena_->New<CallStackDestructionBarrier>()),
      pending_send_initial_metadata_(false),
      pending_send_message_(false),
      pending_send_trailing_metadata_(false),
      retry_committed_(false),
      last_attempt_got_server_pushback_(false) {}

RetryFilter::CallData::~CallData() {
  grpc_slice_unref_internal(path_);
  // Make sure there are no remaining pending batches.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    GPR_ASSERT(pending_batches_[i].batch == nullptr);
  }
}

void RetryFilter::CallData::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  // If we have an LB call, delegate to the LB call.
  if (committed_call_ != nullptr) {
    // Note: This will release the call combiner.
    committed_call_->StartTransportStreamOpBatch(batch);
    return;
  }
  // Handle cancellation.
  if (GPR_UNLIKELY(batch->cancel_stream)) {
    grpc_error_handle cancel_error = batch->payload->cancel_stream.cancel_error;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: cancelled from surface: %s", chand_,
              this, grpc_error_std_string(cancel_error).c_str());
    }
    // If we have a current call attempt, commit the call, then send
    // the cancellation down to that attempt.  When the call fails, it
    // will not be retried, because we have committed it here.
    if (call_attempt_ != nullptr) {
      RetryCommit(call_attempt_.get());
      // Note: This will release the call combiner.
      call_attempt_->lb_call()->StartTransportStreamOpBatch(batch);
      return;
    }
    // Fail pending batches.
    PendingBatchesFail(GRPC_ERROR_REF(cancel_error));
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(cancel_error), call_combiner_);
    return;
  }
  // Add the batch to the pending list.
  PendingBatch* pending = PendingBatchesAdd(batch);
  if (call_attempt_ == nullptr) {
    // If this is the first batch and retries are already committed
    // (e.g., if this batch put the call above the buffer size limit), then
    // immediately create an LB call and delegate the batch to it.  This
    // avoids the overhead of unnecessarily allocating a CallAttempt
    // object or caching any of the send op data.
    if (num_attempts_completed_ == 0 && retry_committed_) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: retry committed before first attempt; "
                "creating LB call",
                chand_, this);
      }
      PendingBatchClear(pending);
      committed_call_ = CreateLoadBalancedCall();
      committed_call_->StartTransportStreamOpBatch(batch);
      return;
    }
    // We do not yet have a call attempt, so create one.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: creating call attempt", chand_,
              this);
    }
    CreateCallAttempt();
    return;
  }
  // Send batches to call attempt.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting batch on attempt=%p lb_call=%p",
            chand_, this, call_attempt_.get(), call_attempt_->lb_call());
  }
  call_attempt_->StartRetriableBatches();
}

RefCountedPtr<ClientChannel::LoadBalancedCall>
RetryFilter::CallData::CreateLoadBalancedCall() {
  grpc_call_element_args args = {owning_call_, nullptr,          call_context_,
                                 path_,        call_start_time_, deadline_,
                                 arena_,       call_combiner_};
  return chand_->client_channel_->CreateLoadBalancedCall(
      args, pollent_,
      // This callback holds a ref to the CallStackDestructionBarrier
      // object until the LB call is destroyed.
      call_stack_destruction_barrier_->MakeLbCallDestructionClosure(this));
}

void RetryFilter::CallData::CreateCallAttempt() {
  call_attempt_.reset(arena_->New<CallAttempt>(this));
  call_attempt_->StartRetriableBatches();
  // TODO(roth): When implementing hedging, change this to start a timer
  // for the next hedging attempt.
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

void RetryFilter::CallData::AddClosureForBatch(
    grpc_transport_stream_op_batch* batch, CallCombinerClosureList* closures) {
  batch->handler_private.extra_arg = call_attempt_->lb_call();
  GRPC_CLOSURE_INIT(&batch->handler_private.closure, StartBatchInCallCombiner,
                    batch, grpc_schedule_on_exec_ctx);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: starting batch on LB call: %s",
            chand_, this, grpc_transport_stream_op_batch_string(batch).c_str());
  }
  closures->Add(&batch->handler_private.closure, GRPC_ERROR_NONE,
                "start_batch_on_lb_call");
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
            "chand_=%p calld=%p: adding pending batch at index %" PRIuPTR,
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
  GPR_ASSERT(error != GRPC_ERROR_NONE);
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
    call_attempt->FreeCachedSendOpDataAfterCommit();
  }
}

void RetryFilter::CallData::DoRetry(grpc_millis server_pushback_ms) {
  // Reset call attempt.
  call_attempt_.reset();
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
  GRPC_CLOSURE_INIT(&retry_closure_, OnRetryTimer, this, nullptr);
  GRPC_CALL_STACK_REF(owning_call_, "OnRetryTimer");
  MutexLock lock(&timer_mu_);
  canceller_ = new Canceller(this);
  grpc_timer_init(&retry_timer_, next_attempt_time, &retry_closure_);
}

void RetryFilter::CallData::OnRetryTimer(void* arg, grpc_error_handle error) {
  auto* calld = static_cast<CallData*>(arg);
  if (error == GRPC_ERROR_NONE) {
    bool start_attempt = false;
    {
      MutexLock lock(&calld->timer_mu_);
      if (calld->canceller_ != nullptr) {
        calld->canceller_ = nullptr;
        start_attempt = true;
      }
    }
    if (start_attempt) calld->CreateCallAttempt();
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call_, "OnRetryTimer");
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
