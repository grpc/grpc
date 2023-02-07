//
//
// Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_CFSTREAM_ENDPOINT

#import <CoreFoundation/CoreFoundation.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/cfstream_handle.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#import "src/core/lib/iomgr/endpoint_cfstream.h"
#include "src/core/lib/iomgr/error_cfstream.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

struct CFStreamEndpoint {
  grpc_endpoint base;
  gpr_refcount refcount;

  CFReadStreamRef read_stream;
  CFWriteStreamRef write_stream;
  CFStreamHandle* stream_sync;

  grpc_closure* read_cb;
  grpc_closure* write_cb;
  grpc_slice_buffer* read_slices;
  grpc_slice_buffer* write_slices;

  grpc_closure read_action;
  grpc_closure write_action;

  std::string peer_string;
  std::string local_address;
};
static void CFStreamFree(CFStreamEndpoint* ep) {
  CFRelease(ep->read_stream);
  CFRelease(ep->write_stream);
  CFSTREAM_HANDLE_UNREF(ep->stream_sync, "free");
  delete ep;
}

#ifndef NDEBUG
#define EP_REF(ep, reason) CFStreamRef((ep), (reason), __FILE__, __LINE__)
#define EP_UNREF(ep, reason) CFStreamUnref((ep), (reason), __FILE__, __LINE__)
static void CFStreamUnref(CFStreamEndpoint* ep, const char* reason,
                          const char* file, int line) {
  if (grpc_tcp_trace.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&ep->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "CFStream endpoint unref %p : %s %" PRIdPTR " -> %" PRIdPTR, ep,
            reason, val, val - 1);
  }
  if (gpr_unref(&ep->refcount)) {
    CFStreamFree(ep);
  }
}
static void CFStreamRef(CFStreamEndpoint* ep, const char* reason,
                        const char* file, int line) {
  if (grpc_tcp_trace.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&ep->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "CFStream endpoint ref %p : %s %" PRIdPTR " -> %" PRIdPTR, ep,
            reason, val, val + 1);
  }
  gpr_ref(&ep->refcount);
}
#else
#define EP_REF(ep, reason) CFStreamRef((ep))
#define EP_UNREF(ep, reason) CFStreamUnref((ep))
static void CFStreamUnref(CFStreamEndpoint* ep) {
  if (gpr_unref(&ep->refcount)) {
    CFStreamFree(ep);
  }
}
static void CFStreamRef(CFStreamEndpoint* ep) { gpr_ref(&ep->refcount); }
#endif

static grpc_error_handle CFStreamAnnotateError(grpc_error_handle src_error,
                                               CFStreamEndpoint* ep) {
  return grpc_error_set_str(
      grpc_error_set_int(src_error, grpc_core::StatusIntProperty::kRpcStatus,
                         GRPC_STATUS_UNAVAILABLE),
      grpc_core::StatusStrProperty::kTargetAddress, ep->peer_string);
}

static void CallReadCb(CFStreamEndpoint* ep, grpc_error_handle error) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    gpr_log(GPR_DEBUG, "CFStream endpoint:%p call_read_cb %p %p:%p", ep,
            ep->read_cb, ep->read_cb->cb, ep->read_cb->cb_arg);
    size_t i;
    gpr_log(GPR_DEBUG, "read: error=%s",
            grpc_core::StatusToString(error).c_str());

    for (i = 0; i < ep->read_slices->count; i++) {
      char* dump = grpc_dump_slice(ep->read_slices->slices[i],
                                   GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "READ %p (peer=%s): %s", ep, ep->peer_string.c_str(),
              dump);
      gpr_free(dump);
    }
  }
  grpc_closure* cb = ep->read_cb;
  ep->read_cb = nullptr;
  ep->read_slices = nullptr;
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
}

static void CallWriteCb(CFStreamEndpoint* ep, grpc_error_handle error) {
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CFStream endpoint:%p call_write_cb %p %p:%p", ep,
            ep->write_cb, ep->write_cb->cb, ep->write_cb->cb_arg);
    gpr_log(GPR_DEBUG, "write: error=%s",
            grpc_core::StatusToString(error).c_str());
  }
  grpc_closure* cb = ep->write_cb;
  ep->write_cb = nullptr;
  ep->write_slices = nullptr;
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
}

static void ReadAction(void* arg, grpc_error_handle error) {
  CFStreamEndpoint* ep = static_cast<CFStreamEndpoint*>(arg);
  GPR_ASSERT(ep->read_cb != nullptr);
  if (!error.ok()) {
    grpc_slice_buffer_reset_and_unref(ep->read_slices);
    CallReadCb(ep, error);
    EP_UNREF(ep, "read");
    return;
  }

  GPR_ASSERT(ep->read_slices->count == 1);
  grpc_slice slice = ep->read_slices->slices[0];
  size_t len = GRPC_SLICE_LENGTH(slice);
  CFIndex read_size =
      CFReadStreamRead(ep->read_stream, GRPC_SLICE_START_PTR(slice), len);
  if (read_size == -1) {
    grpc_slice_buffer_reset_and_unref(ep->read_slices);
    CFErrorRef stream_error = CFReadStreamCopyError(ep->read_stream);
    if (stream_error != nullptr) {
      error = CFStreamAnnotateError(
          GRPC_ERROR_CREATE_FROM_CFERROR(stream_error, "Read error"), ep);
      CFRelease(stream_error);
    } else {
      error = GRPC_ERROR_CREATE("Read error");
    }
    CallReadCb(ep, error);
    EP_UNREF(ep, "read");
  } else if (read_size == 0) {
    grpc_slice_buffer_reset_and_unref(ep->read_slices);
    CallReadCb(ep,
               CFStreamAnnotateError(GRPC_ERROR_CREATE("Socket closed"), ep));
    EP_UNREF(ep, "read");
  } else {
    if (read_size < static_cast<CFIndex>(len)) {
      grpc_slice_buffer_trim_end(ep->read_slices, len - read_size, nullptr);
    }
    CallReadCb(ep, absl::OkStatus());
    EP_UNREF(ep, "read");
  }
}

static void WriteAction(void* arg, grpc_error_handle error) {
  CFStreamEndpoint* ep = static_cast<CFStreamEndpoint*>(arg);
  GPR_ASSERT(ep->write_cb != nullptr);
  if (!error.ok()) {
    grpc_slice_buffer_reset_and_unref(ep->write_slices);
    CallWriteCb(ep, error);
    EP_UNREF(ep, "write");
    return;
  }
  grpc_slice slice = grpc_slice_buffer_take_first(ep->write_slices);
  size_t slice_len = GRPC_SLICE_LENGTH(slice);
  CFIndex write_size = CFWriteStreamWrite(
      ep->write_stream, GRPC_SLICE_START_PTR(slice), slice_len);
  if (write_size == -1) {
    grpc_slice_buffer_reset_and_unref(ep->write_slices);
    CFErrorRef stream_error = CFWriteStreamCopyError(ep->write_stream);
    if (stream_error != nullptr) {
      error = CFStreamAnnotateError(
          GRPC_ERROR_CREATE_FROM_CFERROR(stream_error, "write failed."), ep);
      CFRelease(stream_error);
    } else {
      error = GRPC_ERROR_CREATE("write failed.");
    }
    CallWriteCb(ep, error);
    EP_UNREF(ep, "write");
  } else {
    if (write_size < static_cast<CFIndex>(GRPC_SLICE_LENGTH(slice))) {
      grpc_slice_buffer_undo_take_first(
          ep->write_slices, grpc_slice_sub(slice, write_size, slice_len));
    }
    if (ep->write_slices->length > 0) {
      ep->stream_sync->NotifyOnWrite(&ep->write_action);
    } else {
      CallWriteCb(ep, absl::OkStatus());
      EP_UNREF(ep, "write");
    }

    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace) &&
        gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
      grpc_slice trace_slice = grpc_slice_sub(slice, 0, write_size);
      char* dump = grpc_dump_slice(trace_slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "WRITE %p (peer=%s): %s", ep, ep->peer_string.c_str(),
              dump);
      gpr_free(dump);
      grpc_core::CSliceUnref(trace_slice);
    }
  }
  grpc_core::CSliceUnref(slice);
}

static void CFStreamRead(grpc_endpoint* ep, grpc_slice_buffer* slices,
                         grpc_closure* cb, bool urgent,
                         int /*min_progress_size*/) {
  CFStreamEndpoint* ep_impl = reinterpret_cast<CFStreamEndpoint*>(ep);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CFStream endpoint:%p read (%p, %p) length:%zu", ep_impl,
            slices, cb, slices->length);
  }
  GPR_ASSERT(ep_impl->read_cb == nullptr);
  ep_impl->read_cb = cb;
  ep_impl->read_slices = slices;
  grpc_slice_buffer_reset_and_unref(slices);
  grpc_slice_buffer_add_indexed(
      slices, GRPC_SLICE_MALLOC(GRPC_TCP_DEFAULT_READ_SLICE_SIZE));
  EP_REF(ep_impl, "read");
  ep_impl->stream_sync->NotifyOnRead(&ep_impl->read_action);
}

static void CFStreamWrite(grpc_endpoint* ep, grpc_slice_buffer* slices,
                          grpc_closure* cb, void* arg, int /*max_frame_size*/) {
  CFStreamEndpoint* ep_impl = reinterpret_cast<CFStreamEndpoint*>(ep);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CFStream endpoint:%p write (%p, %p) length:%zu",
            ep_impl, slices, cb, slices->length);
  }
  GPR_ASSERT(ep_impl->write_cb == nullptr);
  ep_impl->write_cb = cb;
  ep_impl->write_slices = slices;
  EP_REF(ep_impl, "write");
  ep_impl->stream_sync->NotifyOnWrite(&ep_impl->write_action);
}

void CFStreamShutdown(grpc_endpoint* ep, grpc_error_handle why) {
  CFStreamEndpoint* ep_impl = reinterpret_cast<CFStreamEndpoint*>(ep);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CFStream endpoint:%p shutdown (%s)", ep_impl,
            grpc_core::StatusToString(why).c_str());
  }
  CFReadStreamClose(ep_impl->read_stream);
  CFWriteStreamClose(ep_impl->write_stream);
  ep_impl->stream_sync->Shutdown(why);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CFStream endpoint:%p shutdown DONE (%s)", ep_impl,
            grpc_core::StatusToString(why).c_str());
  }
}

void CFStreamDestroy(grpc_endpoint* ep) {
  CFStreamEndpoint* ep_impl = reinterpret_cast<CFStreamEndpoint*>(ep);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CFStream endpoint:%p destroy", ep_impl);
  }
  EP_UNREF(ep_impl, "destroy");
}

absl::string_view CFStreamGetPeer(grpc_endpoint* ep) {
  CFStreamEndpoint* ep_impl = reinterpret_cast<CFStreamEndpoint*>(ep);
  return ep_impl->peer_string;
}

absl::string_view CFStreamGetLocalAddress(grpc_endpoint* ep) {
  CFStreamEndpoint* ep_impl = reinterpret_cast<CFStreamEndpoint*>(ep);
  return ep_impl->local_address;
}

int CFStreamGetFD(grpc_endpoint* ep) { return 0; }

bool CFStreamCanTrackErr(grpc_endpoint* ep) { return false; }

void CFStreamAddToPollset(grpc_endpoint* ep, grpc_pollset* pollset) {}
void CFStreamAddToPollsetSet(grpc_endpoint* ep, grpc_pollset_set* pollset) {}
void CFStreamDeleteFromPollsetSet(grpc_endpoint* ep,
                                  grpc_pollset_set* pollset) {}

static const grpc_endpoint_vtable vtable = {CFStreamRead,
                                            CFStreamWrite,
                                            CFStreamAddToPollset,
                                            CFStreamAddToPollsetSet,
                                            CFStreamDeleteFromPollsetSet,
                                            CFStreamShutdown,
                                            CFStreamDestroy,
                                            CFStreamGetPeer,
                                            CFStreamGetLocalAddress,
                                            CFStreamGetFD,
                                            CFStreamCanTrackErr};

grpc_endpoint* grpc_cfstream_endpoint_create(CFReadStreamRef read_stream,
                                             CFWriteStreamRef write_stream,
                                             const char* peer_string,
                                             CFStreamHandle* stream_sync) {
  CFStreamEndpoint* ep_impl = new CFStreamEndpoint;
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "CFStream endpoint:%p create readStream:%p writeStream: %p",
            ep_impl, read_stream, write_stream);
  }
  ep_impl->base.vtable = &vtable;
  gpr_ref_init(&ep_impl->refcount, 1);
  ep_impl->read_stream = read_stream;
  ep_impl->write_stream = write_stream;
  CFRetain(read_stream);
  CFRetain(write_stream);
  ep_impl->stream_sync = stream_sync;
  CFSTREAM_HANDLE_REF(ep_impl->stream_sync, "endpoint create");

  ep_impl->peer_string = peer_string;
  grpc_resolved_address resolved_local_addr;
  resolved_local_addr.len = sizeof(resolved_local_addr.addr);
  CFDataRef native_handle = static_cast<CFDataRef>(CFReadStreamCopyProperty(
      ep_impl->read_stream, kCFStreamPropertySocketNativeHandle));
  CFSocketNativeHandle sockfd;
  CFDataGetBytes(native_handle, CFRangeMake(0, sizeof(CFSocketNativeHandle)),
                 (UInt8*)&sockfd);
  if (native_handle) {
    CFRelease(native_handle);
  }
  absl::StatusOr<std::string> addr_uri;
  if (getsockname(sockfd, reinterpret_cast<sockaddr*>(resolved_local_addr.addr),
                  &resolved_local_addr.len) < 0 ||
      !(addr_uri = grpc_sockaddr_to_uri(&resolved_local_addr)).ok()) {
    ep_impl->local_address = "";
  } else {
    ep_impl->local_address = addr_uri.value();
  }
  ep_impl->read_cb = nil;
  ep_impl->write_cb = nil;
  ep_impl->read_slices = nil;
  ep_impl->write_slices = nil;
  GRPC_CLOSURE_INIT(&ep_impl->read_action, ReadAction,
                    static_cast<void*>(ep_impl), grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&ep_impl->write_action, WriteAction,
                    static_cast<void*>(ep_impl), grpc_schedule_on_exec_ctx);

  return &ep_impl->base;
}

#endif  // GRPC_CFSTREAM_ENDPOINT
