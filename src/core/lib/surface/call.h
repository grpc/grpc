//
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
//

#ifndef GRPC_SRC_CORE_LIB_SURFACE_CALL_H
#define GRPC_SRC_CORE_LIB_SURFACE_CALL_H

#include <stddef.h>
#include <stdint.h>

#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/impl/compression_types.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/time_precise.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server_interface.h"

typedef void (*grpc_ioreq_completion_func)(grpc_call* call, int success,
                                           void* user_data);

typedef struct grpc_call_create_args {
  grpc_core::RefCountedPtr<grpc_core::Channel> channel;
  grpc_core::ServerInterface* server;

  grpc_call* parent;
  uint32_t propagation_mask;

  grpc_completion_queue* cq;
  // if not NULL, it'll be used in lieu of cq
  grpc_pollset_set* pollset_set_alternative;

  const void* server_transport_data;

  absl::optional<grpc_core::Slice> path;
  absl::optional<grpc_core::Slice> authority;

  grpc_core::Timestamp send_deadline;
  bool registered_method;  // client_only
} grpc_call_create_args;

namespace grpc_core {

class Call : public CppImplOf<Call, grpc_call>,
             public grpc_event_engine::experimental::EventEngine::
                 Closure /* for deadlines */ {
 public:
  Arena* arena() { return arena_; }
  bool is_client() const { return is_client_; }

  virtual void ContextSet(grpc_context_index elem, void* value,
                          void (*destroy)(void* value)) = 0;
  virtual void* ContextGet(grpc_context_index elem) const = 0;
  virtual bool Completed() = 0;
  void CancelWithStatus(grpc_status_code status, const char* description);
  virtual void CancelWithError(grpc_error_handle error) = 0;
  virtual void SetCompletionQueue(grpc_completion_queue* cq) = 0;
  char* GetPeer();
  virtual grpc_call_error StartBatch(const grpc_op* ops, size_t nops,
                                     void* notify_tag,
                                     bool is_notify_tag_closure) = 0;
  virtual bool failed_before_recv_message() const = 0;
  virtual bool is_trailers_only() const = 0;
  virtual absl::string_view GetServerAuthority() const = 0;
  virtual void ExternalRef() = 0;
  virtual void ExternalUnref() = 0;
  virtual void InternalRef(const char* reason) = 0;
  virtual void InternalUnref(const char* reason) = 0;

  void UpdateDeadline(Timestamp deadline) ABSL_LOCKS_EXCLUDED(deadline_mu_);
  void ResetDeadline() ABSL_LOCKS_EXCLUDED(deadline_mu_);
  Timestamp deadline() {
    MutexLock lock(&deadline_mu_);
    return deadline_;
  }

  grpc_compression_algorithm test_only_compression_algorithm() {
    return incoming_compression_algorithm_;
  }
  uint32_t test_only_message_flags() { return test_only_last_message_flags_; }
  CompressionAlgorithmSet encodings_accepted_by_peer() {
    return encodings_accepted_by_peer_;
  }

  // This should return nullptr for the promise stack (and alternative means
  // for that functionality be invented)
  virtual grpc_call_stack* call_stack() = 0;

  // Return the EventEngine used for this call's async execution.
  virtual grpc_event_engine::experimental::EventEngine* event_engine()
      const = 0;

  // Implementation of EventEngine::Closure, called when deadline expires
  void Run() final;

 protected:
  // The maximum number of concurrent batches possible.
  // Based upon the maximum number of individually queueable ops in the batch
  // api:
  //    - initial metadata send
  //    - message send
  //    - status/close send (depending on client/server)
  //    - initial metadata recv
  //    - message recv
  //    - status/close recv (depending on client/server)
  static constexpr size_t kMaxConcurrentBatches = 6;

  struct ParentCall {
    Mutex child_list_mu;
    Call* first_child ABSL_GUARDED_BY(child_list_mu) = nullptr;
  };

  struct ChildCall {
    explicit ChildCall(Call* parent) : parent(parent) {}
    Call* parent;
    /// siblings: children of the same parent form a list, and this list is
    /// protected under
    /// parent->mu
    Call* sibling_next = nullptr;
    Call* sibling_prev = nullptr;
  };

  Call(Arena* arena, bool is_client, Timestamp send_deadline,
       RefCountedPtr<Channel> channel)
      : channel_(std::move(channel)),
        arena_(arena),
        send_deadline_(send_deadline),
        is_client_(is_client) {
    DCHECK_NE(arena_, nullptr);
    DCHECK(channel_ != nullptr);
  }
  ~Call() override = default;

  void DeleteThis();

  ParentCall* GetOrCreateParentCall();
  ParentCall* parent_call();
  Channel* channel() const {
    DCHECK(channel_ != nullptr);
    return channel_.get();
  }

  absl::Status InitParent(Call* parent, uint32_t propagation_mask);
  void PublishToParent(Call* parent);
  void MaybeUnpublishFromParent();
  void PropagateCancellationToChildren();

  Timestamp send_deadline() const { return send_deadline_; }
  void set_send_deadline(Timestamp send_deadline) {
    send_deadline_ = send_deadline;
  }

  Slice GetPeerString() const {
    MutexLock lock(&peer_mu_);
    return peer_string_.Ref();
  }

  void SetPeerString(Slice peer_string) {
    MutexLock lock(&peer_mu_);
    peer_string_ = std::move(peer_string);
  }

  void ClearPeerString() { SetPeerString(Slice(grpc_empty_slice())); }

  // TODO(ctiller): cancel_func is for cancellation of the call - filter stack
  // holds no mutexes here, promise stack does, and so locking is different.
  // Remove this and cancel directly once promise conversion is done.
  void ProcessIncomingInitialMetadata(grpc_metadata_batch& md);
  // Fixup outgoing metadata before sending - adds compression, protects
  // internal headers against external modification.
  void PrepareOutgoingInitialMetadata(const grpc_op& op,
                                      grpc_metadata_batch& md);
  void NoteLastMessageFlags(uint32_t flags) {
    test_only_last_message_flags_ = flags;
  }
  grpc_compression_algorithm incoming_compression_algorithm() const {
    return incoming_compression_algorithm_;
  }

  void HandleCompressionAlgorithmDisabled(
      grpc_compression_algorithm compression_algorithm) GPR_ATTRIBUTE_NOINLINE;
  void HandleCompressionAlgorithmNotAccepted(
      grpc_compression_algorithm compression_algorithm) GPR_ATTRIBUTE_NOINLINE;

  gpr_cycle_counter start_time() const { return start_time_; }

 private:
  RefCountedPtr<Channel> channel_;
  Arena* const arena_;
  std::atomic<ParentCall*> parent_call_{nullptr};
  ChildCall* child_ = nullptr;
  Timestamp send_deadline_;
  const bool is_client_;
  // flag indicating that cancellation is inherited
  bool cancellation_is_inherited_ = false;
  // Compression algorithm for *incoming* data
  grpc_compression_algorithm incoming_compression_algorithm_ =
      GRPC_COMPRESS_NONE;
  // Supported encodings (compression algorithms), a bitset.
  // Always support no compression.
  CompressionAlgorithmSet encodings_accepted_by_peer_{GRPC_COMPRESS_NONE};
  uint32_t test_only_last_message_flags_ = 0;
  // Peer name is protected by a mutex because it can be accessed by the
  // application at the same moment as it is being set by the completion
  // of the recv_initial_metadata op.  The mutex should be mostly uncontended.
  mutable Mutex peer_mu_;
  Slice peer_string_;
  // Current deadline.
  Mutex deadline_mu_;
  Timestamp deadline_ ABSL_GUARDED_BY(deadline_mu_) = Timestamp::InfFuture();
  grpc_event_engine::experimental::EventEngine::TaskHandle ABSL_GUARDED_BY(
      deadline_mu_) deadline_task_;
  gpr_cycle_counter start_time_ = gpr_get_cycle_counter();
};

class BasicPromiseBasedCall;
class ServerPromiseBasedCall;

class ServerCallContext {
 public:
  virtual void PublishInitialMetadata(
      ClientMetadataHandle metadata,
      grpc_metadata_array* publish_initial_metadata) = 0;

  // Construct the top of the server call promise for the v2 filter stack.
  // TODO(ctiller): delete when v3 is available.
  virtual ArenaPromise<ServerMetadataHandle> MakeTopOfServerCallPromise(
      CallArgs call_args, grpc_completion_queue* cq,
      absl::FunctionRef<void(grpc_call* call)> publish) = 0;

  // Server stream data as supplied by the transport (so we can link the
  // transport stream up with the call again).
  // TODO(ctiller): legacy API - once we move transports to promises we'll
  // create the promise directly and not need to pass around this token.
  virtual const void* server_stream_data() = 0;

 protected:
  ~ServerCallContext() = default;
};

// TODO(ctiller): move more call things into this type
class CallContext {
 public:
  explicit CallContext(BasicPromiseBasedCall* call) : call_(call) {}

  // Run some action in the call activity context. This is needed to adapt some
  // legacy systems to promises, and will likely disappear once that conversion
  // is complete.
  void RunInContext(absl::AnyInvocable<void()> fn);

  // TODO(ctiller): remove this once transport APIs are promise based
  void IncrementRefCount(const char* reason = "call_context");

  // TODO(ctiller): remove this once transport APIs are promise based
  void Unref(const char* reason = "call_context");

  RefCountedPtr<CallContext> Ref() {
    IncrementRefCount();
    return RefCountedPtr<CallContext>(this);
  }

  grpc_call_stats* call_stats() { return &call_stats_; }
  gpr_atm* peer_string_atm_ptr();
  gpr_cycle_counter call_start_time() { return start_time_; }

  ServerCallContext* server_call_context();

  void set_traced(bool traced) { traced_ = traced; }
  bool traced() const { return traced_; }

  // TEMPORARY HACK
  // Create a call spine object for this call.
  // Said object should only be created once.
  // Allows interop between the v2 call stack and the v3 (which is required by
  // transports).
  RefCountedPtr<CallSpineInterface> MakeCallSpine(CallArgs call_args);
  grpc_call* c_call();

 private:
  friend class PromiseBasedCall;
  // Call final info.
  grpc_call_stats call_stats_;
  // TODO(ctiller): remove this once transport APIs are promise based and we
  // don't need refcounting here.
  BasicPromiseBasedCall* const call_;
  gpr_cycle_counter start_time_ = gpr_get_cycle_counter();
  // Is this call traced?
  bool traced_ = false;
};

template <>
struct ContextType<CallContext> {};

// TODO(ctiller): remove once call-v3 finalized
RefCountedPtr<CallSpineInterface> MakeServerCall(
    ClientMetadataHandle client_initial_metadata, ServerInterface* server,
    Channel* channel, Arena* arena);

}  // namespace grpc_core

// Create a new call based on \a args.
// Regardless of success or failure, always returns a valid new call into *call
//
grpc_error_handle grpc_call_create(grpc_call_create_args* args,
                                   grpc_call** call);

void grpc_call_set_completion_queue(grpc_call* call, grpc_completion_queue* cq);

grpc_core::Arena* grpc_call_get_arena(grpc_call* call);

grpc_call_stack* grpc_call_get_call_stack(grpc_call* call);

grpc_call_error grpc_call_start_batch_and_execute(grpc_call* call,
                                                  const grpc_op* ops,
                                                  size_t nops,
                                                  grpc_closure* closure);

// gRPC core internal version of grpc_call_cancel that does not create
// exec_ctx.
void grpc_call_cancel_internal(grpc_call* call);

// Given the top call_element, get the call object.
grpc_call* grpc_call_from_top_element(grpc_call_element* surface_element);

void grpc_call_log_batch(const char* file, int line, gpr_log_severity severity,
                         const grpc_op* ops, size_t nops);

// Set a context pointer.
// No thread safety guarantees are made wrt this value.
// TODO(#9731): add exec_ctx to destroy
void grpc_call_context_set(grpc_call* call, grpc_context_index elem,
                           void* value, void (*destroy)(void* value));
// Get a context pointer.
void* grpc_call_context_get(grpc_call* call, grpc_context_index elem);

#define GRPC_CALL_LOG_BATCH(sev, ops, nops)        \
  do {                                             \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_api_trace)) { \
      grpc_call_log_batch(sev, ops, nops);         \
    }                                              \
  } while (0)

uint8_t grpc_call_is_client(grpc_call* call);

// Get the estimated memory size for a call BESIDES the call stack. Combined
// with the size of the call stack, it helps estimate the arena size for the
// initial call.
size_t grpc_call_get_initial_size_estimate();

// Return an appropriate compression algorithm for the requested compression \a
// level in the context of \a call.
grpc_compression_algorithm grpc_call_compression_for_level(
    grpc_call* call, grpc_compression_level level);

// Did this client call receive a trailers-only response
// TODO(markdroth): This is currently available only to the C++ API.
//                  Move to surface API if requested by other languages.
bool grpc_call_is_trailers_only(const grpc_call* call);

// Returns the authority for the call, as seen on the server side.
absl::string_view grpc_call_server_authority(const grpc_call* call);

extern grpc_core::TraceFlag grpc_call_error_trace;
extern grpc_core::TraceFlag grpc_compression_trace;

#endif  // GRPC_SRC_CORE_LIB_SURFACE_CALL_H
