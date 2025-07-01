// Copyright 2024 gRPC authors.
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

#include "test/core/test_util/passthrough_endpoint.h"

namespace grpc_event_engine {
namespace experimental {

class PassthroughEndpoint::CallbackHelper {
 public:
  CallbackHelper(EventEngine* event_engine, bool allow_inline_callbacks)
      : event_engine_(allow_inline_callbacks ? nullptr : event_engine) {}

  template <typename F>
  void AddCallback(F&& callback) {
    if (event_engine_ != nullptr) {
      event_engine_->Run(std::forward<F>(callback));
    } else {
      callbacks_.emplace_back(std::forward<F>(callback));
    }
  }

  ~CallbackHelper() {
    for (auto& callback : callbacks_) {
      callback();
    }
  }

 private:
  EventEngine* event_engine_;
  absl::InlinedVector<absl::AnyInvocable<void()>, 4> callbacks_;
};

PassthroughEndpoint::PassthroughEndpointPair
PassthroughEndpoint::MakePassthroughEndpoint(int client_port, int server_port,
                                             bool allow_inline_callbacks) {
  auto send_middle =
      grpc_core::MakeRefCounted<PassthroughEndpoint::Middle>(client_port);
  auto recv_middle =
      grpc_core::MakeRefCounted<PassthroughEndpoint::Middle>(server_port);
  auto client = std::unique_ptr<PassthroughEndpoint>(new PassthroughEndpoint(
      send_middle, recv_middle, allow_inline_callbacks));
  auto server = std::unique_ptr<PassthroughEndpoint>(new PassthroughEndpoint(
      recv_middle, send_middle, allow_inline_callbacks));
  return {std::move(client), std::move(server)};
}

PassthroughEndpoint::~PassthroughEndpoint() {
  CallbackHelper callback_helper(event_engine_.get(), allow_inline_callbacks_);
  send_middle_->Close(callback_helper);
  recv_middle_->Close(callback_helper);
}

bool PassthroughEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                               SliceBuffer* buffer, ReadArgs) {
  CallbackHelper callback_helper(event_engine_.get(), allow_inline_callbacks_);
  grpc_core::MutexLock lock(&recv_middle_->mu);
  if (recv_middle_->closed) {
    callback_helper.AddCallback([on_read = std::move(on_read)]() mutable {
      on_read(absl::CancelledError());
    });
    return false;
  }
  if (recv_middle_->on_write != nullptr) {
    *buffer = std::move(*recv_middle_->write_buffer);
    callback_helper.AddCallback(
        [on_write = std::move(recv_middle_->on_write)]() mutable {
          on_write(absl::OkStatus());
        });
    recv_middle_->on_write = nullptr;
    return true;
  }
  recv_middle_->read_buffer = buffer;
  recv_middle_->on_read = std::move(on_read);
  return false;
}

bool PassthroughEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_write,
                                SliceBuffer* buffer, WriteArgs) {
  CallbackHelper callback_helper(event_engine_.get(), allow_inline_callbacks_);
  grpc_core::MutexLock lock(&send_middle_->mu);
  if (send_middle_->closed) {
    callback_helper.AddCallback([on_write = std::move(on_write)]() mutable {
      on_write(absl::CancelledError());
    });
    return false;
  }
  if (send_middle_->on_read != nullptr) {
    *send_middle_->read_buffer = std::move(*buffer);
    callback_helper.AddCallback(
        [on_read = std::move(send_middle_->on_read)]() mutable {
          on_read(absl::OkStatus());
        });
    send_middle_->on_read = nullptr;
    return true;
  }
  send_middle_->write_buffer = buffer;
  send_middle_->on_write = std::move(on_write);
  return false;
}

void PassthroughEndpoint::Middle::Close(CallbackHelper& callback_helper) {
  grpc_core::MutexLock lock(&mu);
  closed = true;
  if (on_read != nullptr) {
    callback_helper.AddCallback([on_read = std::move(on_read)]() mutable {
      on_read(absl::CancelledError());
    });
    on_read = nullptr;
  }
  if (on_write != nullptr) {
    callback_helper.AddCallback([on_write = std::move(on_write)]() mutable {
      on_write(absl::CancelledError());
    });
    on_write = nullptr;
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine