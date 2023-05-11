// Copyright 2023 The gRPC Authors
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

#ifdef GPR_APPLE

#include "src/core/lib/event_engine/cf_engine/cfstream_endpoint.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/gprpp/strerror.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

int kDefaultReadBufferSize = 8192;

absl::Status CFErrorToStatus(CFTypeUniqueRef<CFErrorRef> cf_error) {
  if (cf_error == nullptr) {
    return absl::OkStatus();
  }
  CFErrorDomain cf_domain = CFErrorGetDomain((cf_error));
  CFIndex code = CFErrorGetCode((cf_error));
  CFTypeUniqueRef<CFStringRef> cf_desc = CFErrorCopyDescription((cf_error));
  char domain_buf[256];
  char desc_buf[256];
  CFStringGetCString(cf_domain, domain_buf, 256, kCFStringEncodingUTF8);
  CFStringGetCString(cf_desc, desc_buf, 256, kCFStringEncodingUTF8);
  return absl::Status(absl::StatusCode::kUnknown,
                      absl::StrFormat("(domain:%s, code:%ld, description:%s)",
                                      domain_buf, code, desc_buf));
}

absl::StatusOr<EventEngine::ResolvedAddress> CFReadStreamLocallAddress(
    CFReadStreamRef stream) {
  CFTypeUniqueRef<CFDataRef> cf_native_handle = static_cast<CFDataRef>(
      CFReadStreamCopyProperty(stream, kCFStreamPropertySocketNativeHandle));
  CFSocketNativeHandle socket;
  CFDataGetBytes(cf_native_handle, CFRangeMake(0, sizeof(CFSocketNativeHandle)),
                 (UInt8*)&socket);
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (getsockname(socket, const_cast<sockaddr*>(addr.address()), &len) < 0) {
    return absl::InternalError(
        absl::StrCat("getsockname:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

}  // namespace

bool CFStreamEndpointImpl::CancelConnect(absl::Status status) {
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE(
      "CFStreamEndpointImpl::CancelConnect: status: %s, this: %p",
      status.ToString().c_str(), this);

  return open_event_.SetShutdown(std::move(status));
}

void CFStreamEndpointImpl::Connect(
    absl::AnyInvocable<void(absl::Status)> on_connect,
    EventEngine::ResolvedAddress addr) {
  auto addr_uri = ResolvedAddressToURI(addr);

  if (!addr_uri.ok()) {
    on_connect(std::move(addr_uri).status());
    return;
  }

  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("CFStreamEndpointImpl::Connect: %s",
                                   addr_uri.value().c_str());

  peer_address_ = std::move(addr);
  auto host_port = ResolvedAddressToNormalizedString(peer_address_);
  if (!host_port.ok()) {
    on_connect(std::move(host_port).status());
    return;
  }

  peer_address_string_ = host_port.value();
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE(
      "CFStreamEndpointImpl::Connect, host_port: %s", host_port->c_str());

  std::string host_string;
  std::string port_string;
  grpc_core::SplitHostPort(host_port.value(), &host_string, &port_string);
  CFStringRef host = CFStringCreateWithCString(NULL, host_string.c_str(),
                                               kCFStringEncodingUTF8);
  int port = ResolvedAddressGetPort(peer_address_);
  CFStreamCreatePairWithSocketToHost(NULL, host, port, &cf_read_stream_,
                                     &cf_write_stream_);

  CFStreamClientContext cf_context = {0, this, Retain, Release, nullptr};
  CFReadStreamSetClient(
      cf_read_stream_,
      kCFStreamEventOpenCompleted | kCFStreamEventHasBytesAvailable |
          kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered,
      ReadCallback, &cf_context);
  CFWriteStreamSetClient(
      cf_write_stream_,
      kCFStreamEventOpenCompleted | kCFStreamEventCanAcceptBytes |
          kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered,
      WriteCallback, &cf_context);
  CFReadStreamSetDispatchQueue(cf_read_stream_,
                               dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0));
  CFWriteStreamSetDispatchQueue(
      cf_write_stream_, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0));

  if (!CFReadStreamOpen(cf_read_stream_)) {
    auto status = CFErrorToStatus(CFReadStreamCopyError(cf_read_stream_));
    on_connect(std::move(status));
    return;
  }

  if (!CFWriteStreamOpen(cf_write_stream_)) {
    auto status = CFErrorToStatus(CFWriteStreamCopyError(cf_write_stream_));
    on_connect(std::move(status));
    return;
  }

  open_event_.NotifyOn(new PosixEngineClosure(
      [that = Ref(),
       on_connect = std::move(on_connect)](absl::Status status) mutable {
        if (!status.ok()) {
          on_connect(std::move(status));
          return;
        }

        auto local_addr = CFReadStreamLocallAddress(that->cf_read_stream_);
        if (!local_addr.ok()) {
          on_connect(std::move(local_addr).status());
          return;
        }

        that->local_address_ = local_addr.value();
        that->local_address_string_ =
            *ResolvedAddressToURI(that->local_address_);
        on_connect(absl::OkStatus());
      },
      false /* is_permanent */));
}

/* static */ void CFStreamEndpointImpl::ReadCallback(
    CFReadStreamRef stream, CFStreamEventType type,
    void* client_callback_info) {
  auto self = static_cast<CFStreamEndpointImpl*>(client_callback_info);

  GRPC_EVENT_ENGINE_ENDPOINT_TRACE(
      "CFStreamEndpointImpl::ReadCallback, type: %lu, this: %p", type, self);

  switch (type) {
    case kCFStreamEventOpenCompleted:
      // wait for write stream open completed to signal connection ready
      break;
    case kCFStreamEventHasBytesAvailable:
      ABSL_FALLTHROUGH_INTENDED;
    case kCFStreamEventEndEncountered:
      self->read_event_.SetReady();
      break;
    case kCFStreamEventErrorOccurred: {
      auto status = CFErrorToStatus(CFReadStreamCopyError(stream));
      GRPC_EVENT_ENGINE_ENDPOINT_TRACE("CFStream Read error: %s",
                                       status.ToString().c_str());

      self->open_event_.SetShutdown(status);
      self->read_event_.SetShutdown(status);
      self->write_event_.SetShutdown(status);
    } break;
    default:
      GPR_UNREACHABLE_CODE(return);
  }
}

/* static */
void CFStreamEndpointImpl::WriteCallback(CFWriteStreamRef stream,
                                         CFStreamEventType type,
                                         void* client_callback_info) {
  auto self = static_cast<CFStreamEndpointImpl*>(client_callback_info);
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE(
      "CFStreamEndpointImpl::WriteCallback, type: %lu, this: %p", type, self);

  switch (type) {
    case kCFStreamEventOpenCompleted:
      self->open_event_.SetReady();
      break;
    case kCFStreamEventCanAcceptBytes:
      ABSL_FALLTHROUGH_INTENDED;
    case kCFStreamEventEndEncountered:
      self->write_event_.SetReady();
      break;
    case kCFStreamEventErrorOccurred: {
      auto status = CFErrorToStatus(CFWriteStreamCopyError(stream));
      GRPC_EVENT_ENGINE_ENDPOINT_TRACE("CFStream Write error: %s",
                                       status.ToString().c_str());

      self->open_event_.SetShutdown(status);
      self->read_event_.SetShutdown(status);
      self->write_event_.SetShutdown(status);
    } break;
    default:
      GPR_UNREACHABLE_CODE(return);
  }
}

CFStreamEndpointImpl::CFStreamEndpointImpl(
    std::shared_ptr<CFEventEngine> engine, MemoryAllocator memory_allocator)
    : engine_(std::move(engine)),
      memory_allocator_(std::move(memory_allocator)),
      open_event_(engine_.get()),
      read_event_(engine_.get()),
      write_event_(engine_.get()) {
  open_event_.InitEvent();
  read_event_.InitEvent();
  write_event_.InitEvent();
}

CFStreamEndpointImpl::~CFStreamEndpointImpl() {
  open_event_.DestroyEvent();
  read_event_.DestroyEvent();
  write_event_.DestroyEvent();
}

void CFStreamEndpointImpl::Shutdown() {
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("CFStreamEndpointImpl::Shutdown: this: %p",
                                   this);

  auto shutdownStatus =
      absl::Status(absl::StatusCode::kUnknown,
                   absl::StrFormat("Shutting down CFStreamEndpointImpl"));
  open_event_.SetShutdown(shutdownStatus);
  read_event_.SetShutdown(shutdownStatus);
  write_event_.SetShutdown(shutdownStatus);

  CFReadStreamSetClient(cf_read_stream_, kCFStreamEventNone, nullptr, nullptr);
  CFWriteStreamSetClient(cf_write_stream_, kCFStreamEventNone, nullptr,
                         nullptr);
  CFReadStreamClose(cf_read_stream_);
  CFWriteStreamClose(cf_write_stream_);
}

bool CFStreamEndpointImpl::Read(
    absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
    const EventEngine::Endpoint::ReadArgs* /* args */) {
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("CFStreamEndpointImpl::Read, this: %p",
                                   this);

  read_event_.NotifyOn(new PosixEngineClosure(
      [that = Ref(), on_read = std::move(on_read),
       buffer](absl::Status status) mutable {
        if (status.ok()) {
          that->DoRead(std::move(on_read), buffer);
        } else {
          on_read(status);
        }
      },
      false /* is_permanent*/));

  return false;
}

void CFStreamEndpointImpl::DoRead(
    absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer) {
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("CFStreamEndpointImpl::DoRead, this: %p",
                                   this);

  auto buffer_index = buffer->AppendIndexed(
      Slice(memory_allocator_.MakeSlice(kDefaultReadBufferSize)));

  CFIndex read_size = CFReadStreamRead(
      cf_read_stream_,
      internal::SliceCast<MutableSlice>(buffer->MutableSliceAt(buffer_index))
          .begin(),
      kDefaultReadBufferSize);

  if (read_size < 0) {
    auto status = CFErrorToStatus(CFReadStreamCopyError(cf_read_stream_));
    GRPC_EVENT_ENGINE_ENDPOINT_TRACE("CFStream read error: %s, read_size: %ld",
                                     status.ToString().c_str(), read_size);
    on_read(status);
    return;
  }

  buffer->RemoveLastNBytes(buffer->Length() - read_size);
  on_read(absl::OkStatus());
}

bool CFStreamEndpointImpl::Write(
    absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data,
    const EventEngine::Endpoint::WriteArgs* /* args */) {
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("CFStreamEndpointImpl::Write, this: %p",
                                   this);

  write_event_.NotifyOn(new PosixEngineClosure(
      [that = Ref(), on_writable = std::move(on_writable),
       data](absl::Status status) mutable {
        if (status.ok()) {
          that->DoWrite(std::move(on_writable), data);
        } else {
          on_writable(status);
        }
      },
      false /* is_permanent*/));

  return false;
}

void CFStreamEndpointImpl::DoWrite(
    absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data) {
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("CFStreamEndpointImpl::DoWrite, this: %p",
                                   this);

  size_t total_written_size = 0;
  for (size_t i = 0; i < data->Count(); i++) {
    auto slice = data->RefSlice(i);
    size_t written_size =
        CFWriteStreamWrite(cf_write_stream_, slice.begin(), slice.size());

    total_written_size += written_size;
    if (written_size < slice.size()) {
      SliceBuffer written;
      data->MoveFirstNBytesIntoSliceBuffer(total_written_size, written);

      write_event_.NotifyOn(new PosixEngineClosure(
          [that = Ref(), on_writable = std::move(on_writable),
           data](absl::Status status) mutable {
            if (status.ok()) {
              that->DoWrite(std::move(on_writable), data);
            } else {
              on_writable(status);
            }
          },
          false /* is_permanent*/));
      return;
    }
  }
  on_writable(absl::OkStatus());
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_APPLE
