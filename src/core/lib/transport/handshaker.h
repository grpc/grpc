//
//
// Copyright 2016 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_HANDSHAKER_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_HANDSHAKER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/container/inlined_vector.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/tcp_server.h"

namespace grpc_core {

/// Handshakers are used to perform initial handshakes on a connection
/// before the client sends the initial request.  Some examples of what
/// a handshaker can be used for includes support for HTTP CONNECT on
/// the client side and various types of security initialization.
///
/// In general, handshakers should be used via a handshake manager.

/// Arguments passed through handshakers and to the on_handshake_done callback.
///
/// For handshakers, all members are input/output parameters; for
/// example, a handshaker may read from or write to \a endpoint and
/// then later replace it with a wrapped endpoint.  Similarly, a
/// handshaker may modify \a args.
///
/// A handshaker takes ownership of the members while a handshake is in
/// progress.  Upon failure or shutdown of an in-progress handshaker,
/// the handshaker is responsible for destroying the members and setting
/// them to NULL before invoking the on_handshake_done callback.
///
/// For the on_handshake_done callback, all members are input arguments,
/// which the callback takes ownership of.
struct HandshakerArgs {
  grpc_endpoint* endpoint = nullptr;
  ChannelArgs args;
  grpc_slice_buffer* read_buffer = nullptr;
  // A handshaker may set this to true before invoking on_handshake_done
  // to indicate that subsequent handshakers should be skipped.
  bool exit_early = false;
  // User data passed through the handshake manager.  Not used by
  // individual handshakers.
  void* user_data = nullptr;
  // Deadline associated with the handshake.
  // TODO(anramach): Move this out of handshake args after EventEngine
  // is the default.
  Timestamp deadline;
};

///
/// Handshaker
///

class Handshaker : public RefCounted<Handshaker> {
 public:
  ~Handshaker() override = default;
  virtual void Shutdown(grpc_error_handle why) = 0;
  virtual void DoHandshake(grpc_tcp_server_acceptor* acceptor,
                           grpc_closure* on_handshake_done,
                           HandshakerArgs* args) = 0;
  virtual const char* name() const = 0;
};

//
// HandshakeManager
//

class HandshakeManager : public RefCounted<HandshakeManager> {
 public:
  HandshakeManager();
  ~HandshakeManager() override;

  /// Adds a handshaker to the handshake manager.
  /// Takes ownership of \a handshaker.
  void Add(RefCountedPtr<Handshaker> handshaker) ABSL_LOCKS_EXCLUDED(mu_);

  /// Shuts down the handshake manager (e.g., to clean up when the operation is
  /// aborted in the middle).
  void Shutdown(grpc_error_handle why) ABSL_LOCKS_EXCLUDED(mu_);

  /// Invokes handshakers in the order they were added.
  /// Takes ownership of \a endpoint, and then passes that ownership to
  /// the \a on_handshake_done callback.
  /// Does NOT take ownership of \a channel_args.  Instead, makes a copy before
  /// invoking the first handshaker.
  /// \a acceptor will be nullptr for client-side handshakers.
  ///
  /// When done, invokes \a on_handshake_done with a HandshakerArgs
  /// object as its argument.  If the callback is invoked with error !=
  /// absl::OkStatus(), then handshaking failed and the handshaker has done
  /// the necessary clean-up.  Otherwise, the callback takes ownership of
  /// the arguments.
  void DoHandshake(grpc_endpoint* endpoint, const ChannelArgs& channel_args,
                   Timestamp deadline, grpc_tcp_server_acceptor* acceptor,
                   grpc_iomgr_cb_func on_handshake_done, void* user_data)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  bool CallNextHandshakerLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // A function used as the handshaker-done callback when chaining
  // handshakers together.
  static void CallNextHandshakerFn(void* arg, grpc_error_handle error)
      ABSL_LOCKS_EXCLUDED(mu_);

  static const size_t HANDSHAKERS_INIT_SIZE = 2;

  Mutex mu_;
  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
  // An array of handshakers added via grpc_handshake_manager_add().
  absl::InlinedVector<RefCountedPtr<Handshaker>, HANDSHAKERS_INIT_SIZE>
      handshakers_ ABSL_GUARDED_BY(mu_);
  // The index of the handshaker to invoke next and closure to invoke it.
  size_t index_ ABSL_GUARDED_BY(mu_) = 0;
  grpc_closure call_next_handshaker_ ABSL_GUARDED_BY(mu_);
  // The acceptor to call the handshakers with.
  grpc_tcp_server_acceptor* acceptor_ ABSL_GUARDED_BY(mu_);
  // The final callback and user_data to invoke after the last handshaker.
  grpc_closure on_handshake_done_ ABSL_GUARDED_BY(mu_);
  // Handshaker args.
  HandshakerArgs args_ ABSL_GUARDED_BY(mu_);
  // Deadline timer across all handshakers.
  grpc_event_engine::experimental::EventEngine::TaskHandle
      deadline_timer_handle_ ABSL_GUARDED_BY(mu_);
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_
      ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_HANDSHAKER_H
