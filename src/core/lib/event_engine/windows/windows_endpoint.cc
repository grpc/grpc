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

// TODO(hork): The previous implementation required internal ref counting. Add
// this when it becomes necessary.
// TODO(hork): The previous implementation required a 2-phase shutdown. Add this
// when it becomes necessary.

namespace {
constexpr int64_t kDefaultTargetReadSize = 8192;
constexpr int kMaxWSABUFCount = 16;

void AbortOnEvent(absl::Status) {
  grpc_core::Crash(
      "INTERNAL ERROR: Asked to handle read/write event with an invalid "
      "callback");
}

}  // namespace

WindowsEndpoint::WindowsEndpoint(
    const EventEngine::ResolvedAddress& peer_address,
    std::unique_ptr<WinSocket> socket, MemoryAllocator&& allocator,
    const EndpointConfig& /* config */, Executor* executor)
    : peer_address_(peer_address),
      socket_(std::move(socket)),
      allocator_(std::move(allocator)),
      handle_read_event_(this),
      handle_write_event_(this),
      executor_(executor) {
  char addr[EventEngine::ResolvedAddress::MAX_SIZE_BYTES];
  int addr_len = sizeof(addr);
  if (getsockname(socket_->socket(), reinterpret_cast<sockaddr*>(addr),
                  &addr_len) < 0) {
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
  socket_->MaybeShutdown(absl::OkStatus());
}

void WindowsEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                           SliceBuffer* buffer, const ReadArgs* args) {
  // TODO(hork): last_read_buffer from iomgr: Is it only garbage, or optimized?
  GRPC_EVENT_ENGINE_TRACE("WindowsEndpoint::%p reading", this);
  // Prepare the WSABUF struct
  WSABUF wsa_buffers[kMaxWSABUFCount];
  int min_read_size = kDefaultTargetReadSize;
  if (args != nullptr && args->read_hint_bytes > 0) {
    min_read_size = args->read_hint_bytes;
  }
  if (buffer->Length() < min_read_size && buffer->Count() < kMaxWSABUFCount) {
    buffer->AppendIndexed(Slice(allocator_.MakeSlice(min_read_size)));
  }
  GPR_ASSERT(buffer->Count() <= kMaxWSABUFCount);
  for (int i = 0; i < buffer->Count(); i++) {
    Slice tmp = buffer->RefSlice(i);
    wsa_buffers[i].buf = (char*)tmp.begin();
    wsa_buffers[i].len = tmp.size();
  }
  DWORD bytes_read = 0;
  DWORD flags = 0;
  // First let's try a synchronous, non-blocking read.
  int status = WSARecv(socket_->socket(), wsa_buffers, (DWORD)buffer->Count(),
                       &bytes_read, &flags, nullptr, nullptr);
  int wsa_error = status == 0 ? 0 : WSAGetLastError();
  // Did we get data immediately ? Yay.
  if (wsa_error != WSAEWOULDBLOCK) {
    // prune slicebuffer
    if (bytes_read != buffer->Length()) {
      buffer->RemoveLastNBytes(buffer->Length() - bytes_read);
    }
    executor_->Run([on_read = std::move(on_read)]() mutable {
      on_read(absl::OkStatus());
    });
    return;
  }
  // Otherwise, let's retry, by queuing a read.
  memset(socket_->read_info()->overlapped(), 0, sizeof(OVERLAPPED));
  status =
      WSARecv(socket_->socket(), wsa_buffers, (DWORD)buffer->Count(),
              &bytes_read, &flags, socket_->read_info()->overlapped(), nullptr);
  wsa_error = status == 0 ? 0 : WSAGetLastError();
  if (wsa_error != 0 && wsa_error != WSA_IO_PENDING) {
    // Async read returned immediately with an error
    executor_->Run([this, on_read = std::move(on_read), wsa_error]() mutable {
      on_read(GRPC_WSA_ERROR(
          wsa_error,
          absl::StrFormat("WindowsEndpont::%p Read failed", this).c_str()));
    });
    return;
  }

  handle_read_event_.Prime(buffer, std::move(on_read));
  socket_->NotifyOnRead(&handle_read_event_);
}

void WindowsEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable,
                            SliceBuffer* data, const WriteArgs* /* args */) {
  if (grpc_event_engine_trace.enabled()) {
    for (int i = 0; i < data->Count(); i++) {
      auto str = data->RefSlice(i).as_string_view();
      gpr_log(GPR_INFO, "WindowsEndpoint::%p WRITE (peer=%s): %.*s", this,
              peer_address_string_.c_str(), str.length(), str.data());
    }
  }
  GPR_ASSERT(data->Count() <= UINT_MAX);
  absl::InlinedVector<WSABUF, kMaxWSABUFCount> buffers(data->Count());
  for (int i = 0; i < data->Count(); i++) {
    auto slice = data->RefSlice(i);
    GPR_ASSERT(slice.size() <= ULONG_MAX);
    buffers[i].len = slice.size();
    buffers[i].buf = (char*)slice.begin();
  }
  // First, let's try a synchronous, non-blocking write.
  DWORD bytes_sent;
  int status = WSASend(socket_->socket(), buffers.data(), (DWORD)buffers.size(),
                       &bytes_sent, 0, nullptr, nullptr);
  size_t async_buffers_offset = 0;
  if (status == 0) {
    if (bytes_sent == data->Length()) {
      // Write completed, exiting early
      executor_->Run(
          [cb = std::move(on_writable)]() mutable { cb(absl::OkStatus()); });
      return;
    }
    // The data was not completely delivered, we should send the rest of it by
    // doing an async write operation.
    for (int i = 0; i < data->Count(); i++) {
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
      return;
    }
  }
  auto write_info = socket_->write_info();
  memset(write_info->overlapped(), 0, sizeof(OVERLAPPED));
  status = WSASend(socket_->socket(), &buffers[async_buffers_offset],
                   (DWORD)(data->Count() - async_buffers_offset), nullptr, 0,
                   write_info->overlapped(), nullptr);

  if (status != 0) {
    int wsa_error = WSAGetLastError();
    if (wsa_error != WSA_IO_PENDING) {
      executor_->Run([cb = std::move(on_writable), wsa_error]() mutable {
        cb(GRPC_WSA_ERROR(wsa_error, "WSASend"));
      });
      return;
    }
  }
  // As all is now setup, we can now ask for the IOCP notification. It may
  // trigger the callback immediately however, but no matter.
  handle_write_event_.Prime(data, std::move(on_writable));
  socket_->NotifyOnWrite(&handle_write_event_);
}
const EventEngine::ResolvedAddress& WindowsEndpoint::GetPeerAddress() const {
  return peer_address_;
}
const EventEngine::ResolvedAddress& WindowsEndpoint::GetLocalAddress() const {
  return local_address_;
}

// ---- Handle{Read|Write}Closure

WindowsEndpoint::BaseEventClosure::BaseEventClosure(WindowsEndpoint* endpoint)
    : cb_(&AbortOnEvent), endpoint_(endpoint) {}

void WindowsEndpoint::HandleReadClosure::Run() {
  GRPC_EVENT_ENGINE_TRACE("WindowsEndpoint::%p Handling Read Event", endpoint_);
  absl::Status status;
  auto* read_info = endpoint_->socket_->read_info();
  auto cb_cleanup = absl::MakeCleanup([this, &status]() {
    auto cb = std::move(cb_);
    cb_ = &AbortOnEvent;
    cb(status);
  });
  if (read_info->wsa_error() != 0) {
    status = GRPC_WSA_ERROR(read_info->wsa_error(), "Async Read Error");
    buffer_->Clear();
    return;
  }
  if (read_info->bytes_transferred() > 0) {
    GPR_ASSERT(read_info->bytes_transferred() <= buffer_->Length());
    if (read_info->bytes_transferred() != buffer_->Length()) {
      buffer_->RemoveLastNBytes(buffer_->Length() -
                                read_info->bytes_transferred());
    }
    GPR_ASSERT(read_info->bytes_transferred() == buffer_->Length());
    if (grpc_event_engine_trace.enabled()) {
      for (int i = 0; i < buffer_->Count(); i++) {
        auto str = buffer_->RefSlice(i).as_string_view();
        gpr_log(GPR_INFO, "WindowsEndpoint::%p READ (peer=%s): %.*s", this,
                endpoint_->peer_address_string_.c_str(), str.length(),
                str.data());
      }
    }
    return;
  }
  // Either the endpoint is shut down or we've seen the end of the stream
  buffer_->Clear();
  // TODO(hork): different error message if shut down
  status = absl::UnavailableError("End of TCP stream");
}

void WindowsEndpoint::HandleWriteClosure::Run() {
  GRPC_EVENT_ENGINE_TRACE("WindowsEndpoint::%p Handling Write Event",
                          endpoint_);
  auto* write_info = endpoint_->socket_->write_info();
  auto cb = std::move(cb_);
  cb_ = &AbortOnEvent;
  absl::Status status;
  if (write_info->wsa_error() != 0) {
    status = GRPC_WSA_ERROR(write_info->wsa_error(), "WSASend");
  } else {
    GPR_ASSERT(write_info->bytes_transferred() == buffer_->Length());
  }
  cb(status);
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
