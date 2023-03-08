// Copyright 2022 gRPC authors.
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
#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/log_windows.h>

#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/windows/windows_endpoint.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
constexpr size_t kDefaultTargetReadSize = 8192;
constexpr int kMaxWSABUFCount = 16;

}  // namespace

WindowsEndpoint::WindowsEndpoint(
    const EventEngine::ResolvedAddress& peer_address,
    std::unique_ptr<WinSocket> socket, MemoryAllocator&& allocator,
    const EndpointConfig& /* config */, Executor* executor,
    std::shared_ptr<EventEngine> engine)
    : peer_address_(peer_address),
      allocator_(std::move(allocator)),
      executor_(executor),
      io_state_(std::make_shared<AsyncIOState>(this, std::move(socket))),
      engine_(engine) {
  char addr[EventEngine::ResolvedAddress::MAX_SIZE_BYTES];
  int addr_len = sizeof(addr);
  if (getsockname(io_state_->socket->raw_socket(),
                  reinterpret_cast<sockaddr*>(addr), &addr_len) < 0) {
    grpc_core::Crash(absl::StrFormat(
        "Unrecoverable error: Failed to get local socket name. %s",
        GRPC_WSA_ERROR(WSAGetLastError(), "getsockname").ToString().c_str()));
  }
  local_address_ =
      EventEngine::ResolvedAddress(reinterpret_cast<sockaddr*>(addr), addr_len);
  local_address_string_ = *ResolvedAddressToURI(local_address_);
  peer_address_string_ = *ResolvedAddressToURI(peer_address_);
}

WindowsEndpoint::~WindowsEndpoint() {
  io_state_->socket->Shutdown(DEBUG_LOCATION, "~WindowsEndpoint");
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("WindowsEndpoint::%p destroyed", this);
}

bool WindowsEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                           SliceBuffer* buffer, const ReadArgs* /* args */) {
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("WindowsEndpoint::%p reading", this);
  if (io_state_->socket->IsShutdown()) {
    executor_->Run([on_read = std::move(on_read)]() mutable {
      on_read(absl::UnavailableError("Socket is shutting down."));
    });
    return false;
  }
  // Prepare the WSABUF struct
  WSABUF wsa_buffers[kMaxWSABUFCount];
  // TODO(hork): introduce a last_read_buffer to save unused sliced.
  buffer->Clear();
  // TODO(hork): sometimes args->read_hint_bytes is 1, which is not useful.
  // Choose an appropriate size.
  size_t min_read_size = kDefaultTargetReadSize;
  if (buffer->Length() < min_read_size && buffer->Count() < kMaxWSABUFCount) {
    buffer->AppendIndexed(Slice(allocator_.MakeSlice(min_read_size)));
  }
  GPR_ASSERT(buffer->Count() <= kMaxWSABUFCount);
  for (size_t i = 0; i < buffer->Count(); i++) {
    auto& slice = buffer->MutableSliceAt(i);
    wsa_buffers[i].buf = (char*)slice.begin();
    wsa_buffers[i].len = slice.size();
  }
  DWORD bytes_read = 0;
  DWORD flags = 0;
  // First let's try a synchronous, non-blocking read.
  int status =
      WSARecv(io_state_->socket->raw_socket(), wsa_buffers,
              (DWORD)buffer->Count(), &bytes_read, &flags, nullptr, nullptr);
  int wsa_error = status == 0 ? 0 : WSAGetLastError();
  // Did we get data immediately ? Yay.
  if (wsa_error != WSAEWOULDBLOCK) {
    io_state_->socket->read_info()->SetResult(
        {/*wsa_error=*/wsa_error, /*bytes_read=*/bytes_read});
    absl::Status result;
    if (bytes_read == 0) {
      result = absl::UnavailableError("End of TCP stream");
      grpc_core::StatusSetInt(&result, grpc_core::StatusIntProperty::kRpcStatus,
                              GRPC_STATUS_UNAVAILABLE);
      buffer->Clear();
    } else {
      result = absl::OkStatus();
      // prune slicebuffer
      if (bytes_read != buffer->Length()) {
        buffer->RemoveLastNBytes(buffer->Length() - bytes_read);
      }
    }
    executor_->Run(
        [result, on_read = std::move(on_read)]() mutable { on_read(result); });
    return false;
  }
  // Otherwise, let's retry, by queuing a read.
  memset(io_state_->socket->read_info()->overlapped(), 0, sizeof(OVERLAPPED));
  status = WSARecv(io_state_->socket->raw_socket(), wsa_buffers,
                   (DWORD)buffer->Count(), &bytes_read, &flags,
                   io_state_->socket->read_info()->overlapped(), nullptr);
  wsa_error = status == 0 ? 0 : WSAGetLastError();
  if (wsa_error != 0 && wsa_error != WSA_IO_PENDING) {
    // Async read returned immediately with an error
    executor_->Run([this, on_read = std::move(on_read), wsa_error]() mutable {
      on_read(GRPC_WSA_ERROR(
          wsa_error,
          absl::StrFormat("WindowsEndpont::%p Read failed", this).c_str()));
    });
    return false;
  }
  io_state_->handle_read_event.Prime(io_state_, buffer, std::move(on_read));
  io_state_->socket->NotifyOnRead(&io_state_->handle_read_event);
  return false;
}

bool WindowsEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable,
                            SliceBuffer* data, const WriteArgs* /* args */) {
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("WindowsEndpoint::%p writing", this);
  if (io_state_->socket->IsShutdown()) {
    executor_->Run([on_writable = std::move(on_writable)]() mutable {
      on_writable(absl::UnavailableError("Socket is shutting down."));
    });
    return false;
  }
  if (grpc_event_engine_endpoint_data_trace.enabled()) {
    for (size_t i = 0; i < data->Count(); i++) {
      auto str = data->RefSlice(i).as_string_view();
      gpr_log(GPR_INFO, "WindowsEndpoint::%p WRITE (peer=%s): %.*s", this,
              peer_address_string_.c_str(), str.length(), str.data());
    }
  }
  GPR_ASSERT(data->Count() <= UINT_MAX);
  absl::InlinedVector<WSABUF, kMaxWSABUFCount> buffers(data->Count());
  for (size_t i = 0; i < data->Count(); i++) {
    auto& slice = data->MutableSliceAt(i);
    GPR_ASSERT(slice.size() <= ULONG_MAX);
    buffers[i].len = slice.size();
    buffers[i].buf = (char*)slice.begin();
  }
  // First, let's try a synchronous, non-blocking write.
  DWORD bytes_sent;
  int status = WSASend(io_state_->socket->raw_socket(), buffers.data(),
                       (DWORD)buffers.size(), &bytes_sent, 0, nullptr, nullptr);
  size_t async_buffers_offset = 0;
  if (status == 0) {
    if (bytes_sent == data->Length()) {
      // Write completed, exiting early
      executor_->Run(
          [cb = std::move(on_writable)]() mutable { cb(absl::OkStatus()); });
      return false;
    }
    // The data was not completely delivered, we should send the rest of it by
    // doing an async write operation.
    for (size_t i = 0; i < data->Count(); i++) {
      if (buffers[i].len > bytes_sent) {
        buffers[i].buf += bytes_sent;
        buffers[i].len -= bytes_sent;
        break;
      }
      bytes_sent -= buffers[i].len;
      async_buffers_offset++;
    }
  } else {
    // We would kind of expect to get a WSAEWOULDBLOCK here, especially on a
    // busy connection that has its send queue filled up. But if we don't,
    // then we can avoid doing an async write operation at all.
    int wsa_error = WSAGetLastError();
    if (wsa_error != WSAEWOULDBLOCK) {
      executor_->Run([cb = std::move(on_writable), wsa_error]() mutable {
        cb(GRPC_WSA_ERROR(wsa_error, "WSASend"));
      });
      return false;
    }
  }
  auto write_info = io_state_->socket->write_info();
  memset(write_info->overlapped(), 0, sizeof(OVERLAPPED));
  status =
      WSASend(io_state_->socket->raw_socket(), &buffers[async_buffers_offset],
              (DWORD)(data->Count() - async_buffers_offset), nullptr, 0,
              write_info->overlapped(), nullptr);

  if (status != 0) {
    int wsa_error = WSAGetLastError();
    if (wsa_error != WSA_IO_PENDING) {
      executor_->Run([cb = std::move(on_writable), wsa_error]() mutable {
        cb(GRPC_WSA_ERROR(wsa_error, "WSASend"));
      });
      return false;
    }
  }
  // As all is now setup, we can now ask for the IOCP notification. It may
  // trigger the callback immediately however, but no matter.
  io_state_->handle_write_event.Prime(io_state_, data, std::move(on_writable));
  io_state_->socket->NotifyOnWrite(&io_state_->handle_write_event);
  return false;
}
const EventEngine::ResolvedAddress& WindowsEndpoint::GetPeerAddress() const {
  return peer_address_;
}
const EventEngine::ResolvedAddress& WindowsEndpoint::GetLocalAddress() const {
  return local_address_;
}

// ---- Handle{Read|Write}Closure ----
namespace {
void AbortOnEvent(absl::Status) {
  grpc_core::Crash(
      "INTERNAL ERROR: Asked to handle read/write event with an invalid "
      "callback");
}
}  // namespace

void WindowsEndpoint::HandleReadClosure::Reset() {
  cb_ = &AbortOnEvent;
  buffer_ = nullptr;
}

void WindowsEndpoint::HandleWriteClosure::Reset() {
  cb_ = &AbortOnEvent;
  buffer_ = nullptr;
}

void WindowsEndpoint::HandleReadClosure::Prime(
    std::shared_ptr<AsyncIOState> io_state, SliceBuffer* buffer,
    absl::AnyInvocable<void(absl::Status)> cb) {
  io_state_ = std::move(io_state);
  cb_ = std::move(cb);
  buffer_ = buffer;
}

void WindowsEndpoint::HandleWriteClosure::Prime(
    std::shared_ptr<AsyncIOState> io_state, SliceBuffer* buffer,
    absl::AnyInvocable<void(absl::Status)> cb) {
  io_state_ = std::move(io_state);
  cb_ = std::move(cb);
  buffer_ = buffer;
}

void WindowsEndpoint::HandleReadClosure::Run() {
  // Deletes the shared_ptr when this closure returns
  auto io_state = std::move(io_state_);
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("WindowsEndpoint::%p Handling Read Event",
                                   io_state->endpoint);
  absl::Status status;
  auto cb_cleanup = absl::MakeCleanup([this, &status]() {
    auto cb = std::move(cb_);
    Reset();
    cb(status);
  });
  const auto result = io_state->socket->read_info()->result();
  if (result.wsa_error != 0) {
    status = GRPC_WSA_ERROR(result.wsa_error, "Async Read Error");
    buffer_->Clear();
    return;
  }
  if (result.bytes_transferred > 0) {
    GPR_ASSERT(result.bytes_transferred <= buffer_->Length());
    if (result.bytes_transferred != buffer_->Length()) {
      buffer_->RemoveLastNBytes(buffer_->Length() - result.bytes_transferred);
    }
    GPR_ASSERT(result.bytes_transferred == buffer_->Length());
    if (grpc_event_engine_endpoint_data_trace.enabled()) {
      for (size_t i = 0; i < buffer_->Count(); i++) {
        auto str = buffer_->RefSlice(i).as_string_view();
        gpr_log(GPR_INFO, "WindowsEndpoint::%p READ (peer=%s): %.*s",
                io_state->endpoint,
                io_state->endpoint->peer_address_string_.c_str(), str.length(),
                str.data());
      }
    }
    return;
  }
  // Either the endpoint is shut down or we've seen the end of the stream
  buffer_->Clear();
  status = absl::UnavailableError("End of TCP stream");
}

void WindowsEndpoint::HandleWriteClosure::Run() {
  // Deletes the shared_ptr when this closure returns
  auto io_state = std::move(io_state_);
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("WindowsEndpoint::%p Handling Write Event",
                                   io_state->endpoint);
  auto cb = std::move(cb_);
  const auto result = io_state->socket->write_info()->result();
  absl::Status status;
  if (result.wsa_error != 0) {
    status = GRPC_WSA_ERROR(result.wsa_error, "WSASend");
  } else {
    GPR_ASSERT(result.bytes_transferred == buffer_->Length());
  }
  Reset();
  cb(status);
}

// ---- AsyncIOState ----

WindowsEndpoint::AsyncIOState::AsyncIOState(WindowsEndpoint* endpoint,
                                            std::unique_ptr<WinSocket> socket)
    : endpoint(endpoint), socket(std::move(socket)) {}

WindowsEndpoint::AsyncIOState::~AsyncIOState() {
  socket->Shutdown(DEBUG_LOCATION, "~AsyncIOState");
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
