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

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/log_windows.h>

#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/windows/windows_endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/status_helper.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
constexpr size_t kDefaultTargetReadSize = 8192;
constexpr int kMaxWSABUFCount = 16;

void DumpSliceBuffer(SliceBuffer* buffer, absl::string_view context_string) {
  for (size_t i = 0; i < buffer->Count(); i++) {
    auto str = buffer->MutableSliceAt(i).as_string_view();
    GRPC_TRACE_LOG(event_engine_endpoint, INFO)
        << context_string << " [" << i + 1 << "/" << buffer->Count()
        << "]: " << str;
  }
}

}  // namespace

WindowsEndpoint::WindowsEndpoint(
    const EventEngine::ResolvedAddress& peer_address,
    std::unique_ptr<WinSocket> socket, MemoryAllocator&& allocator,
    const EndpointConfig& /* config */, ThreadPool* thread_pool,
    std::shared_ptr<EventEngine> engine)
    : peer_address_(peer_address),
      allocator_(std::move(allocator)),
      io_state_(std::make_shared<AsyncIOState>(
          this, std::move(socket), std::move(engine), thread_pool)) {
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
  GRPC_TRACE_LOG(event_engine_endpoint, INFO) << "~WindowsEndpoint::" << this;
}

void WindowsEndpoint::AsyncIOState::DoTcpRead(SliceBuffer* buffer) {
  GRPC_TRACE_LOG(event_engine_endpoint, INFO)
      << "WindowsEndpoint::" << endpoint << " attempting a read";
  if (socket->IsShutdown()) {
    socket->read_info()->SetErrorStatus(
        absl::InternalError("Socket is shutting down."));
    thread_pool->Run(&handle_read_event);
    return;
  }
  // Prepare the WSABUF struct
  CHECK(buffer->Count() <= kMaxWSABUFCount);
  WSABUF wsa_buffers[kMaxWSABUFCount];
  for (size_t i = 0; i < buffer->Count(); i++) {
    auto& slice = buffer->MutableSliceAt(i);
    wsa_buffers[i].buf = (char*)slice.begin();
    wsa_buffers[i].len = slice.size();
  }
  DWORD bytes_read = 0;
  DWORD flags = 0;
  // First try a synchronous, non-blocking read.
  int status =
      WSARecv(socket->raw_socket(), wsa_buffers, (DWORD)buffer->Count(),
              &bytes_read, &flags, nullptr, nullptr);
  int wsa_error = status == 0 ? 0 : WSAGetLastError();
  if (wsa_error != WSAEWOULDBLOCK) {
    // Data or some error was returned immediately.
    socket->read_info()->SetResult(wsa_error, bytes_read, "WSARecv");
    thread_pool->Run(&handle_read_event);
    return;
  }
  // If the endpoint has already received some data, and the next call would
  // block, return the data in case that is all the data the reader expects.
  if (handle_read_event.MaybeFinishIfDataHasAlreadyBeenRead()) {
    return;
  }
  // Otherwise, let's retry, by queuing a read.
  socket->NotifyOnRead(&handle_read_event);
  status = WSARecv(socket->raw_socket(), wsa_buffers, (DWORD)buffer->Count(),
                   nullptr, &flags, socket->read_info()->overlapped(), nullptr);
  wsa_error = status == 0 ? 0 : WSAGetLastError();
  if (wsa_error != 0 && wsa_error != WSA_IO_PENDING) {
    // The async read attempt returned an error immediately.
    socket->UnregisterReadCallback();
    socket->read_info()->SetResult(
        wsa_error, 0, absl::StrFormat("WindowsEndpoint::%p Read failed", this));
    thread_pool->Run(&handle_read_event);
  }
}

bool WindowsEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                           SliceBuffer* buffer, const ReadArgs* /* args */) {
  if (io_state_->socket->IsShutdown()) {
    io_state_->thread_pool->Run([on_read = std::move(on_read)]() mutable {
      on_read(absl::InternalError("Socket is shutting down."));
    });
    return false;
  }
  buffer->Clear();
  io_state_->handle_read_event.DonateSpareSlices(buffer);
  // TODO(hork): sometimes args->read_hint_bytes is 1, which is not useful.
  // Choose an appropriate size.
  size_t min_read_size = kDefaultTargetReadSize;
  if (buffer->Length() < min_read_size && buffer->Count() < kMaxWSABUFCount) {
    buffer->AppendIndexed(Slice(allocator_.MakeSlice(min_read_size)));
  }
  io_state_->handle_read_event.Prime(io_state_, buffer, std::move(on_read));
  io_state_->DoTcpRead(buffer);
  return false;
}

bool WindowsEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable,
                            SliceBuffer* data, const WriteArgs* /* args */) {
  GRPC_TRACE_LOG(event_engine_endpoint, INFO)
      << "WindowsEndpoint::" << this << " writing";
  if (io_state_->socket->IsShutdown()) {
    io_state_->thread_pool->Run(
        [on_writable = std::move(on_writable)]() mutable {
          on_writable(absl::InternalError("Socket is shutting down."));
        });
    return false;
  }
  if (GRPC_TRACE_FLAG_ENABLED(event_engine_endpoint_data)) {
    for (size_t i = 0; i < data->Count(); i++) {
      auto str = data->RefSlice(i).as_string_view();
      GRPC_TRACE_LOG(event_engine_endpoint, INFO)
          << "WindowsEndpoint::" << this
          << " WRITE (peer=" << peer_address_string_ << "): " << str;
    }
  }
  CHECK(data->Count() <= UINT_MAX);
  absl::InlinedVector<WSABUF, kMaxWSABUFCount> buffers(data->Count());
  for (size_t i = 0; i < data->Count(); i++) {
    auto& slice = data->MutableSliceAt(i);
    CHECK(slice.size() <= ULONG_MAX);
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
      io_state_->thread_pool->Run(
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
      io_state_->thread_pool->Run(
          [cb = std::move(on_writable), wsa_error]() mutable {
            cb(GRPC_WSA_ERROR(wsa_error, "WSASend"));
          });
      return false;
    }
  }
  auto write_info = io_state_->socket->write_info();
  io_state_->handle_write_event.Prime(io_state_, data, std::move(on_writable));
  io_state_->socket->NotifyOnWrite(&io_state_->handle_write_event);
  status =
      WSASend(io_state_->socket->raw_socket(), &buffers[async_buffers_offset],
              (DWORD)(data->Count() - async_buffers_offset), nullptr, 0,
              write_info->overlapped(), nullptr);
  if (status != 0) {
    int wsa_error = WSAGetLastError();
    if (wsa_error != WSA_IO_PENDING) {
      io_state_->socket->UnregisterWriteCallback();
      io_state_->socket->write_info()->SetResult(wsa_error, 0, "WSASend");
      io_state_->thread_pool->Run(&io_state_->handle_write_event);
    }
  }
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

absl::AnyInvocable<void(absl::Status)>
WindowsEndpoint::HandleReadClosure::ResetAndReturnCallback() {
  auto cb = std::move(cb_);
  cb_ = &AbortOnEvent;
  buffer_ = nullptr;
  io_state_.reset();
  return cb;
}

absl::AnyInvocable<void(absl::Status)>
WindowsEndpoint::HandleWriteClosure::ResetAndReturnCallback() {
  auto cb = std::move(cb_);
  cb_ = &AbortOnEvent;
  buffer_ = nullptr;
  io_state_.reset();
  return cb;
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
  // Note that the endpoint may have already been destroyed.
  auto io_state = std::move(io_state_);
  GRPC_TRACE_LOG(event_engine_endpoint, INFO)
      << "WindowsEndpoint::" << io_state->endpoint << " Handling Read Event";
  const auto result = io_state->socket->read_info()->result();
  if (!result.error_status.ok()) {
    buffer_->Clear();
    return ResetAndReturnCallback()(result.error_status);
  }
  absl::Status status;
  if (result.wsa_error != 0) {
    status = GRPC_WSA_ERROR(result.wsa_error, "Async Read Error");
    buffer_->Clear();
    return ResetAndReturnCallback()(status);
  }
  if (result.bytes_transferred == 0) {
    DCHECK_GT(io_state.use_count(), 0);
    // Either the endpoint is shut down or we've seen the end of the stream
    if (GRPC_TRACE_FLAG_ENABLED(event_engine_endpoint_data)) {
      LOG(INFO) << "WindowsEndpoint::" << this << " read 0 bytes.";
      DumpSliceBuffer(
          &last_read_buffer_,
          absl::StrFormat("WindowsEndpoint::%p READ last_read_buffer_: ",
                          io_state->endpoint));
    }
    buffer_->Swap(last_read_buffer_);
    if (buffer_->Length() == 0) {
      // Only send an error if there is no more data to consume. If the endpoint
      // or socket is shut down, the next read will discover that.
      status = absl::InternalError("End of TCP stream");
      grpc_core::StatusSetInt(&status, grpc_core::StatusIntProperty::kRpcStatus,
                              GRPC_STATUS_UNAVAILABLE);
    }
    return ResetAndReturnCallback()(status);
  }
  DCHECK_GT(result.bytes_transferred, 0);
  DCHECK(result.bytes_transferred <= buffer_->Length());
  buffer_->MoveFirstNBytesIntoSliceBuffer(result.bytes_transferred,
                                          last_read_buffer_);
  if (buffer_->Length() == 0) {
    buffer_->Swap(last_read_buffer_);
    return ResetAndReturnCallback()(status);
  }
  // Doing another read. Let's keep the AsyncIOState alive a bit longer.
  io_state_ = std::move(io_state);
  io_state_->DoTcpRead(buffer_);
}

bool WindowsEndpoint::HandleReadClosure::MaybeFinishIfDataHasAlreadyBeenRead() {
  if (last_read_buffer_.Length() > 0) {
    GRPC_TRACE_LOG(event_engine_endpoint, INFO)
        << "WindowsEndpoint::" << io_state_->endpoint
        << " finishing a synchronous read";
    buffer_->Swap(last_read_buffer_);
    if (GRPC_TRACE_FLAG_ENABLED(event_engine_endpoint_data)) {
      DumpSliceBuffer(buffer_, "finishing synchronous read");
    }
    io_state_->thread_pool->Run(
        [cb = ResetAndReturnCallback()]() mutable { cb(absl::OkStatus()); });
    return true;
  }
  return false;
}

void WindowsEndpoint::HandleReadClosure::DonateSpareSlices(
    SliceBuffer* buffer) {
  // Donee buffer must be empty.
  CHECK_EQ(buffer->Length(), 0);
  // HandleReadClosure must be in the reset state.
  CHECK_EQ(buffer_, nullptr);
  buffer->Swap(last_read_buffer_);
}

void WindowsEndpoint::HandleWriteClosure::Run() {
  // Deletes the shared_ptr when this closure returns
  auto io_state = std::move(io_state_);
  GRPC_TRACE_LOG(event_engine_endpoint, INFO)
      << "WindowsEndpoint::" << io_state->endpoint << " Handling Write Event";
  const auto result = io_state->socket->write_info()->result();
  if (!result.error_status.ok()) {
    buffer_->Clear();
    return ResetAndReturnCallback()(result.error_status);
  }
  absl::Status status;
  if (result.wsa_error != 0) {
    status = GRPC_WSA_ERROR(result.wsa_error, "WSASend");
  } else {
    CHECK(result.bytes_transferred == buffer_->Length());
  }
  return ResetAndReturnCallback()(status);
}

// ---- AsyncIOState ----

WindowsEndpoint::AsyncIOState::AsyncIOState(WindowsEndpoint* endpoint,
                                            std::unique_ptr<WinSocket> socket,
                                            std::shared_ptr<EventEngine> engine,
                                            ThreadPool* thread_pool)
    : endpoint(endpoint),
      socket(std::move(socket)),
      engine(std::move(engine)),
      thread_pool(thread_pool) {}

WindowsEndpoint::AsyncIOState::~AsyncIOState() {
  socket->Shutdown(DEBUG_LOCATION, "~AsyncIOState");
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
