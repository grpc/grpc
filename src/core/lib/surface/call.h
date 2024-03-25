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

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/impl/compression_types.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server_interface.h"
#include "src/core/lib/transport/call_spine.h"

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

class Call : public CppImplOf<Call, grpc_call> {
 public:
  grpc_compression_algorithm test_only_compression_algorithm() {
    return incoming_compression_algorithm();
  }
  uint32_t test_only_message_flags() { return test_only_last_message_flags_; }

  virtual Arena* arena() = 0;
  virtual void ContextSet(grpc_context_index elem, void* value,
                          void (*destroy)(void* value)) = 0;
  virtual void* ContextGet(grpc_context_index elem) const = 0;
  virtual bool Completed() = 0;
  void CancelWithStatus(grpc_status_code status, const char* description);
  virtual void CancelWithError(grpc_error_handle error) = 0;
  virtual void SetCompletionQueue(grpc_completion_queue* cq) = 0;
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
  virtual char* GetPeer() = 0;

  void ResetDeadline();

  // Return the EventEngine used for this call's async execution.
  virtual grpc_event_engine::experimental::EventEngine* event_engine()
      const = 0;

  CompressionAlgorithmSet encodings_accepted_by_peer() {
    return encodings_accepted_by_peer_;
  }

 protected:
  void NoteLastMessageFlags(uint32_t flags) {
    test_only_last_message_flags_ = flags;
  }
  grpc_compression_algorithm incoming_compression_algorithm() const {
    return incoming_compression_algorithm_;
  }
  void set_incoming_compression_algorithm(
      grpc_compression_algorithm algorithm) {
    incoming_compression_algorithm_ = algorithm;
  }
  void set_encodings_accepted_by_peer(CompressionAlgorithmSet encodings) {
    encodings_accepted_by_peer_ = encodings;
  }
  CompressionAlgorithmSet encodings_accepted_by_peer() const {
    return encodings_accepted_by_peer_;
  }

  void PrepareOutgoingInitialMetadata(const grpc_op& op,
                                      const grpc_compression_options& copt,
                                      bool is_client, grpc_metadata_batch& md);

 private:
  // Compression algorithm for *incoming* data
  grpc_compression_algorithm incoming_compression_algorithm_ =
      GRPC_COMPRESS_NONE;
  // Supported encodings (compression algorithms), a bitset.
  // Always support no compression.
  CompressionAlgorithmSet encodings_accepted_by_peer_{GRPC_COMPRESS_NONE};
  uint32_t test_only_last_message_flags_ = 0;
};

class ClientCall : public Call {};

class ServerCall final : public Call {
 public:
  static ServerCall* MakeServerCall(CallHandler call_handler,
                                    ServerInterface* server);

  Arena* arena() override { return call_handler_.arena(); }
  void ContextSet(grpc_context_index elem, void* value,
                  void (*destroy)(void* value)) override {
    call_handler_.legacy_context(elem) =
        grpc_call_context_element{value, destroy};
  }
  void* ContextGet(grpc_context_index elem) const override {
    return call_handler_.legacy_context(elem).value;
  }
  bool Completed() override;
  void CancelWithError(grpc_error_handle error) override;
  void SetCompletionQueue(grpc_completion_queue* cq) override { cq_ = cq; }
  grpc_call_error StartBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                             bool is_notify_tag_closure) override;
  bool failed_before_recv_message() const override;
  bool is_trailers_only() const override;
  absl::string_view GetServerAuthority() const override;
  void ExternalRef() override { ref_count_.Ref(); }
  void ExternalUnref() override {
    if (ref_count_.Unref()) delete this;
  }
  void InternalRef(const char*) override { ExternalRef(); }
  void InternalUnref(const char*) override { ExternalUnref(); }
  char* GetPeer() override { Crash("Not implemented"); }

  // Return the EventEngine used for this call's async execution.
  grpc_event_engine::experimental::EventEngine* event_engine() const override;

  void PublishInitialMetadata(ClientMetadataHandle md,
                              grpc_metadata_array* md_array);

 private:
  ServerCall(CallHandler call_handler, ServerInterface* server);

  static grpc_call_error ValidateBatch(const grpc_op* ops, size_t nops);
  void CommitBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                   bool is_notify_tag_closure);
  StatusFlag FinishRecvMessage(CallFilters::NextMessage result);
  std::string DebugTag() { return call_handler_.DebugTag(); }

  ServerInterface* const server_;
  grpc_byte_buffer** recv_message_ = nullptr;
  ClientMetadataHandle client_initial_metadata_stored_;
  CallHandler call_handler_;
  RefCount ref_count_;
  grpc_completion_queue* cq_;
};

template <>
struct ContextType<Call> {};

template <>
struct ContextSubclass<ServerCall> {
  using Base = Call;
};

template <>
struct ContextSubclass<ClientCall> {
  using Base = Call;
};

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
