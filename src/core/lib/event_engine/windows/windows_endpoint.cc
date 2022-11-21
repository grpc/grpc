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

#include "src/core/lib/event_engine/windows/windows_endpoint.h"

#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include <grpc/support/log_windows.h>

#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/windows/resolved_address.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/status_helper.h"

namespace grpc_event_engine {
namespace experimental {

// DO NOT SUBMIT(hork): we should be careful about the changes to the error
// return types, they were previously all Unavailable.
// TODO(hork): The previous implementation required internal ref counting. Add
// this when it becomes necessary.
// TODO(hork): The previous implementation required a 2-phase shutdown. Add this
// when it becomes necessary.

namespace {
constexpr int kDefaultTargetReadSize = 8192;
constexpr int kMaxWSABUFCount = 16;

void AbortOnEvent(absl::Status) {
  GPR_ASSERT(false &&
             "INTERNAL ERROR: Asked to handle read/write event with an invalid "
             "callback");
}

absl::Status WSAErrorToStatusWithMessage(
    int wsa_error, absl::string_view custom_message,
    const grpc_core::DebugLocation& location) {
  // See
  // https://learn.microsoft.com/en-us/windows/win32/api/Winsock2/nf-winsock2-wsasend
  // https://learn.microsoft.com/en-us/windows/win32/api/Winsock2/nf-winsock2-wsarecv
  char* wsa_message = gpr_format_message(wsa_error);
  std::string message;
  if (!custom_message.empty()) {
    std::string message = absl::StrCat(wsa_message, ": ", custom_message);
  } else {
    std::string message = wsa_message;
  }
  gpr_free(wsa_message);
  switch (wsa_error) {
    case 0:
      return absl::OkStatus();
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSA_OPERATION_ABORTED:
      return StatusCreate(absl::StatusCode::kAborted, message, location, {});
    case WSAETIMEDOUT:
      return StatusCreate(absl::StatusCode::kDeadlineExceeded, message,
                          location, {});
    case WSAEFAULT:
    case WSAEINVAL:
      return StatusCreate(absl::StatusCode::kInvalidArgument, message, location,
                          {});
    case WSAENETDOWN:
    case WSAENOTCONN:
    case WSAESHUTDOWN:
      return StatusCreate(absl::StatusCode::kUnavailable, message, location,
                          {});
    default:
      return StatusCreate(absl::StatusCode::kUnknown, message, location, {});
  }
}

absl::Status WSAErrorToStatus(int wsa_error,
                              const grpc_core::DebugLocation& location) {
  return WSAErrorToStatusWithMessage(wsa_error, "", location);
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
  sockaddr addr;
  int addr_len = sizeof(addr);
  if (getsockname(socket_->socket(), &addr, &addr_len) < 0) {
    GPR_ASSERT(false &&
               "Unrecoverable error: Failed to get local socket name.");
  }
  local_address_ = EventEngine::ResolvedAddress(&addr, addr_len);
  local_address_string_ = *ResolvedAddressToURI(local_address_);
  peer_address_string_ = *ResolvedAddressToURI(peer_address_);
}

WindowsEndpoint::~WindowsEndpoint() {
  socket_->MaybeShutdown(absl::OkStatus());
}

void WindowsEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                           SliceBuffer* buffer, const ReadArgs* /* args */) {
  // TODO(hork): last_read_buffer from iomgr: Is it only garbage, or optimized?
  GRPC_EVENT_ENGINE_TRACE("WindowsEndpoint::%p reading", this);
  // Prepare the WSABUF struct
  WSABUF wsa_buffers[kMaxWSABUFCount];
  // TODO(hork): use read hint instead of the default?
  if (buffer->Length() < kDefaultTargetReadSize &&
      buffer->Count() < kMaxWSABUFCount) {
    buffer->AppendIndexed(Slice(allocator_.MakeSlice(kDefaultTargetReadSize)));
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
                       &bytes_read, &flags, NULL, NULL);
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
              &bytes_read, &flags, socket_->read_info()->overlapped(), NULL);
  wsa_error = status == 0 ? 0 : WSAGetLastError();
  if (wsa_error != 0 && wsa_error != WSA_IO_PENDING) {
    // Async read returned immediately with an error
    executor_->Run([this, on_read = std::move(on_read), wsa_error]() mutable {
      on_read(WSAErrorToStatusWithMessage(
          wsa_error, absl::StrFormat("WindowsEndpont::%p Read failed", this),
          DEBUG_LOCATION));
    });
    return;
  }

  handle_read_event_.SetCallback(std::move(on_read));
  handle_read_event_.SetSliceBuffer(buffer);
  socket_->NotifyOnRead(&handle_read_event_);
}

void WindowsEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable,
                            SliceBuffer* data, const WriteArgs* /* args */) {
  if (grpc_event_engine_trace.enabled()) {
    for (int i = 0; i < data->Count(); i++) {
      gpr_log(GPR_INFO, "WindowsEndpoint::%p WRITE (peer=%s): %s", this,
              peer_address_string_.c_str(),
              data->RefSlice(i).as_string_view().data());
    }
  }
  GPR_ASSERT(data->Count() <= UINT_MAX);
  WSABUF local_buffers[kMaxWSABUFCount];
  LPWSABUF buffers = local_buffers;
  LPWSABUF allocated = nullptr;
  if (data->Count() > kMaxWSABUFCount) {
    allocated = (WSABUF*)gpr_malloc(sizeof(WSABUF) * data->Count());
    buffers = allocated;
  }
  for (int i = 0; i < data->Count(); i++) {
    auto slice = data->RefSlice(i);
    GPR_ASSERT(slice.size() <= ULONG_MAX);
    buffers[i].len = slice.size();
    buffers[i].buf = (char*)slice.begin();
  }
  // First, let's try a synchronous, non-blocking write.
  DWORD bytes_sent;
  int status = WSASend(socket_->socket(), buffers, (DWORD)data->Count(),
                       &bytes_sent, 0, NULL, NULL);
  int wsa_error = status == 0 ? 0 : WSAGetLastError();
  // We would kind of expect to get a WSAEWOULDBLOCK here, especially on a busy
  // connection that has its send queue filled up. But if we don't, then we can
  // avoid doing an async write operation at all.
  if (wsa_error != WSAEWOULDBLOCK) {
    executor_->Run([cb = std::move(on_writable), wsa_error]() mutable {
      cb(WSAErrorToStatusWithMessage(wsa_error, "WSASend", DEBUG_LOCATION));
    });
    return;
  }
  auto write_info = socket_->write_info();
  memset(write_info->overlapped(), 0, sizeof(OVERLAPPED));
  status = WSASend(socket_->socket(), buffers, (DWORD)data->Count(),
                   &bytes_sent, 0, write_info->overlapped(), NULL);
  if (allocated) gpr_free(allocated);

  if (status != 0) {
    int wsa_error = WSAGetLastError();
    if (wsa_error != WSA_IO_PENDING) {
      executor_->Run([cb = std::move(on_writable), wsa_error]() mutable {
        cb(WSAErrorToStatusWithMessage(wsa_error, "WSASend", DEBUG_LOCATION));
      });
      return;
    }
  }
  // As all is now setup, we can now ask for the IOCP notification. It may
  // trigger the callback immediately however, but no matter.
  handle_write_event_.SetCallback(std::move(on_writable));
  handle_write_event_.SetSliceBuffer(data);
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
    : endpoint_(endpoint), cb_(&AbortOnEvent) {}

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
    status = WSAErrorToStatus(read_info->wsa_error(), DEBUG_LOCATION);
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
        gpr_log(GPR_INFO, "WindowsEndpoint::%p READ (peer=%s): %s", this,
                endpoint_->peer_address_string_.c_str(),
                buffer_->RefSlice(i).as_string_view().data());
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
    status = WSAErrorToStatusWithMessage(write_info->wsa_error(),
                                         "Error in WSASend", DEBUG_LOCATION);
  } else {
    GPR_ASSERT(write_info->bytes_transferred() == buffer_->Length());
  }
  endpoint_->executor_->Run(
      [cb = std::move(cb), status = std::move(status)]() mutable {
        cb(status);
      });
}

}  // namespace experimental
}  // namespace grpc_event_engine
